/*
 * Copyright (c) 2017 Dariusz Stojaczyk. All Rights Reserved.
 * The following source code is released under an MIT-style license,
 * that can be found in the LICENSE file.
 */

#include <stdio.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/queue.h>
#include <time.h>
#include "device_handler.h"
#include "event_thread.h"
#include "iputils.h"
#include "ber/snmp.h"
#include "network.h"
#include "data_channel.h"

#define DATA_PORT 54921
#define DEVICE_HANDLER_PORT 49976
#define DEVICE_REGISTER_DURATION_SEC 360
#define BUTTON_HANDLER_PORT 54925
#define SNMP_PORT 161

static char g_local_ip[16];
static uint8_t g_buf[1024];
static uint8_t *const g_buf_end = g_buf + sizeof(g_buf) - 1;

struct device {
    uint8_t buf[1024];
    int conn;
    struct data_channel *channel;
    const char *ip;
    time_t next_register_time;
    TAILQ_ENTRY(device) tailq;
};

struct device_handler {
    TAILQ_HEAD(, device) devices;
};

static uint32_t g_brInfoPrinterUStatusOID[] = { 1, 3, 6, 1, 4, 1, 2435, 2, 3, 9, 4, 2, 1, 5, 5, 6, 0, SNMP_MSG_OID_END };
static uint32_t g_brRegisterKeyInfoOID[] = { 1, 3, 6, 1, 4, 1, 2435, 2, 3, 9, 2, 11, 1, 1, 0, SNMP_MSG_OID_END };

static int
get_scanner_status(int conn)
{
    struct snmp_msg_header msg_header = {0};
    struct snmp_varbind varbind = {0};
    int msg_len;
    uint8_t *out;
    int rc = -1;

    msg_header.snmp_ver = 0;
    msg_header.community = "public";
    msg_header.pdu_type = SNMP_DATA_T_PDU_GET_REQUEST;
    msg_header.request_id = 0;

    varbind.oid = g_brInfoPrinterUStatusOID;
    varbind.value_type = SNMP_DATA_T_NULL;

    out = snmp_encode_msg(g_buf_end, &msg_header, 1, &varbind);
    msg_len = (int) (g_buf_end - out + 1);

    msg_len = network_udp_send(conn, out, msg_len) != 0;
    if (msg_len < 0) {
        perror("sendto");
        goto out;
    }
    
    msg_len = network_udp_receive(conn, g_buf, 1024);
    if (msg_len < 0) {
        perror("recvfrom");
        goto out;
    }
    
    rc = 0;
out:
    return rc;
}

static int
register_scanner_host(int conn)
{
    struct snmp_msg_header msg_header = {0};
    struct snmp_varbind varbind[3] = {0};
    int msg_len;
    uint8_t *out;
    char msg[3][768];
    int i, rc = -1;
    
    const char *host_name = "darsto-br1";
    const char *scan_func[3] = { "IMAGE", "SCAN", "FILE" };
    
    msg_header.snmp_ver = 0;
    msg_header.community = "internal";
    msg_header.pdu_type = SNMP_DATA_T_PDU_SET_REQUEST;
    msg_header.request_id = 0;

    for (i = 0; i < 3; ++i) {
        snprintf(msg[i], sizeof(msg[i]),
                 "TYPE=BR;BUTTON=SCAN;USER=\"%s\";FUNC=%s;HOST=%s:%d;APPNUM=1;DURATION=%d;CC=1;",
                 host_name, scan_func[i], g_local_ip, BUTTON_HANDLER_PORT, DEVICE_REGISTER_DURATION_SEC);
        
        varbind[i].oid = g_brRegisterKeyInfoOID;
        varbind[i].value_type = SNMP_DATA_T_OCTET_STRING;
        varbind[i].value.s = msg[i];
    }

    out = snmp_encode_msg(g_buf_end, &msg_header, 3, varbind);
    msg_len = (int) (g_buf_end - out + 1);

    msg_len = network_udp_send(conn, out, msg_len);
    if (msg_len < 0) {
        perror("sendto");
        goto out;
    }

    msg_len = network_udp_receive(conn, g_buf, 1024);
    if (msg_len < 0) {
        perror("recvfrom");
        goto out;
    }

    rc = 0;
out:
    return rc;
}

static void
device_handler_init_devices(struct device_handler *handler)
{
    struct device *dev;
    const char *ip = "10.0.0.149"; /* FIXME: don't use hardcoded ip, read config file instead */
    int conn, rc;

    conn = network_udp_init_conn(htons(DEVICE_HANDLER_PORT), false);
    if (conn < 0) {
        fprintf(stderr, "Could not setup connection for device at %s.\n", ip);
        return;
    }

    rc = network_udp_connect(conn, inet_addr(ip), htons(SNMP_PORT));
    if (rc != 0) {
        fprintf(stderr, "Could not connect to device at %s.\n", ip);
        network_udp_free(conn);
        return;
    }

    dev = calloc(1, sizeof(*dev));
    if (dev == NULL) {
        fprintf(stderr, "Could not calloc memory for device at %s.\n", ip);
        network_udp_disconnect(conn);
        network_udp_free(conn);
        return;
    }

    dev->conn = conn;
    dev->ip = ip;

    TAILQ_INSERT_TAIL(&handler->devices, dev, tailq);
}

static void
device_handler_loop(void *arg)
{
    struct device_handler *handler = arg;
    struct device *dev;
    time_t time_now;
    int msg_len, status;

    TAILQ_FOREACH(dev, &handler->devices, tailq) {
        time_now = time(NULL);
        if (difftime(time_now, dev->next_register_time) > 0) {
            status = get_scanner_status(dev->conn);
            if (status == 0) {
                register_scanner_host(dev->conn);
            } else {
                fprintf(stderr, "Warn: device at %s is currently unreachable.\n", dev->ip);
                continue;
            }
            dev->next_register_time = time_now + DEVICE_REGISTER_DURATION_SEC;
        }

        msg_len = network_udp_receive(dev->conn, dev->buf, sizeof(dev->buf));
        if (msg_len < 0) {
            continue;
        }

        msg_len = network_udp_send(dev->conn, dev->buf, msg_len);
        if (msg_len < 0) {
            perror("sendto");
            continue;
        }

        if (dev->channel) {
            fprintf(stderr, "WARN: data_channel has already been created for device at %s.\n", dev->ip);
            continue;
        }

        dev->channel = data_channel_create("10.0.0.149", DATA_PORT);
        if (dev->channel == NULL) {
            fprintf(stderr, "Fatal: failed to create data_channel for device %s.\n", dev->ip);
            continue;
        }
    }
    
    sleep(1);
}

static void
device_handler_stop(void *arg)
{
    struct device_handler *handler = arg;
    struct device *dev;

    while ((dev = TAILQ_FIRST(&handler->devices))) {
        TAILQ_REMOVE(&handler->devices, dev, tailq);
        free(dev);
    }
}

void 
device_handler_init(void)
{
    struct device_handler *handler;
    struct event_thread *thread;

    if (iputils_get_local_ip(g_local_ip) != 0) {
        fprintf(stderr, "Fatal: could not get local ip address.\n");
        return;
    }

    handler = calloc(1, sizeof(*handler));
    if (handler == NULL) {
        fprintf(stderr, "Fatal: could not init device_handler thread.\n");
        return;
    }

    TAILQ_INIT(&handler->devices);
    device_handler_init_devices(handler);

    thread = event_thread_create("device_handler", device_handler_loop, device_handler_stop, handler);
    if (thread == NULL) {
        fprintf(stderr, "Fatal: could not init device_handler thread.\n");
        free(handler);
        return;
    }
}

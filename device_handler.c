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
#include <time.h>
#include <memory.h>
#include "device_handler.h"
#include "event_thread.h"
#include "config.h"
#include "network.h"
#include "data_channel.h"
#include "snmp.h"

#define DATA_PORT 54921
#define DEVICE_HANDLER_PORT 49976
#define DEVICE_REGISTER_DURATION_SEC 360
#define DEVICE_KEEPALIVE_DURATION_SEC 5
#define BUTTON_HANDLER_PORT 54925
#define SNMP_PORT 161

static uint8_t g_buf[1024];

static int
register_scanner_host(int conn)
{
    const char *scan_func[4] = { "IMAGE", "OCR", "FILE", "EMAIL" };
    char msg[4][256];
    const char *functions[4];
    int i, rc;

    for (i = 0; i < 4; ++i) {
        rc = snprintf(msg[i], sizeof(msg[i]), ""
                          "TYPE=BR;"
                          "BUTTON=SCAN;"
                          "USER=\"%s\";"
                          "FUNC=%s;"
                          "HOST=%s:%d;"
                          "APPNUM=1;"
                          "DURATION=%d;"
                          "CC=1;",
                      g_config.hostname,
                      scan_func[i],
                      g_config.local_ip,
                      BUTTON_HANDLER_PORT,
                      DEVICE_REGISTER_DURATION_SEC);

        if (rc < 0 || rc == sizeof(msg[i])) {
            return -1;
        }

        functions[i] = msg[i];
    }

    return snmp_register_scanner_host(conn, g_buf, sizeof(g_buf), functions);
}

struct device *
device_handler_add_device(const char *ip)
{
    struct device *dev;
    int conn, button_conn;

    conn = network_init_conn(NETWORK_TYPE_UDP, htons(DEVICE_HANDLER_PORT), inet_addr(ip), htons(SNMP_PORT));
    if (conn < 0) {
        fprintf(stderr, "Could not connect to device at %s.\n", ip);
        return NULL;
    }

    button_conn = network_init_conn(NETWORK_TYPE_UDP, htons(BUTTON_HANDLER_PORT), 0, 0);
    if (conn < 0) {
        fprintf(stderr, "Could not setup button handler connection at %s.\n", ip);
        network_disconnect(conn);
        return NULL;
    }

    dev = calloc(1, sizeof(*dev));
    if (dev == NULL) {
        fprintf(stderr, "Could not calloc memory for device at %s.\n", ip);
        network_disconnect(button_conn);
        network_disconnect(conn);
        return NULL;
    }

    dev->conn = conn;
    dev->button_conn = button_conn;
    dev->ip = strdup(ip);
    dev->channel = data_channel_create(dev->ip, DATA_PORT);
    if (dev->channel == NULL) {
        fprintf(stderr, "Failed to create data_channel for device %s.\n", dev->ip);
        return NULL;
    }

    TAILQ_INSERT_TAIL(&g_config.devices, dev, tailq);
    return dev;
}

static void
device_handler_loop(void *arg)
{
    struct device *dev;
    time_t time_now;
    int msg_len;

    TAILQ_FOREACH(dev, &g_config.devices, tailq) {
        time_now = time(NULL);

        if (difftime(time_now, dev->next_ping_time) > 0) {
            /* only ping once per DEVICE_KEEPALIVE_DURATION_SEC */
            dev->next_ping_time = time_now + DEVICE_KEEPALIVE_DURATION_SEC;
            dev->status = snmp_get_printer_status(dev->conn, g_buf, sizeof(g_buf));
            if (dev->status != 10001) {
                fprintf(stderr, "Warn: device at %s is currently unreachable.\n", dev->ip);
            }
        }

        if (dev->status != 10001) {
            continue;
        }

        if (difftime(time_now, dev->next_register_time) > 0) {
            /* only register once per DEVICE_REGISTER_DURATION_SEC */
            dev->next_register_time = time_now + DEVICE_REGISTER_DURATION_SEC;
            register_scanner_host(dev->conn);
        }

        /* try to receive scan event */
        msg_len = network_receive(dev->button_conn, dev->buf, sizeof(dev->buf));
        if (msg_len < 0) {
            continue;
        }

        msg_len = network_send(dev->button_conn, dev->buf, msg_len);
        if (msg_len < 0) {
            perror("sendto");
            continue;
        }

        data_channel_kick(dev->channel);
    }

    sleep(1);
}

static void
device_handler_stop(void *arg)
{
    struct device *dev;

    while ((dev = TAILQ_FIRST(&g_config.devices))) {
        TAILQ_REMOVE(&g_config.devices, dev, tailq);
        free(dev);
    }
}

void
device_handler_init(const char *config_path)
{
    struct event_thread *thread;

    if (config_init(config_path) != 0) {
        fprintf(stderr, "Fatal: could not init config.\n");
        return;
    }

    thread = event_thread_create("device_handler", device_handler_loop, device_handler_stop, NULL);
    if (thread == NULL) {
        fprintf(stderr, "Fatal: could not init device_handler thread.\n");
        return;
    }
}

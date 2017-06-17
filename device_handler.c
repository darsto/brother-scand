/*
 * Copyright (c) 2017 Dariusz Stojaczyk. All Rights Reserved.
 * The following source code is released under an MIT-style license,
 * that can be found in the LICENSE file.
 */

#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include "device_handler.h"
#include "event_thread.h"
#include "iputils.h"
#include "ber/snmp.h"
#include "log.h"
#include "network.h"

static char g_local_ip[16];
static uint8_t g_buf[1024];
static uint8_t *const g_buf_end = g_buf + sizeof(g_buf) - 1;

static uint32_t brInfoPrinterUStatusOID[] = { 1, 3, 6, 1, 4, 1, 2435, 2, 3, 9, 4, 2, 1, 5, 5, 6, 0, SNMP_MSG_OID_END };

static void
device_handler_loop(void *arg1, void *arg2)
{
    int *conn = arg1;
    struct snmp_msg_header msg_header = {};
    struct snmp_varbind varbind = {};
    int msg_len;
    uint8_t *out;

    msg_header.snmp_ver = 0;
    msg_header.community = "public";
    msg_header.pdu_type = SNMP_DATA_T_PDU_GET_REQUEST;
    msg_header.request_id = 314;

    varbind.value_type = SNMP_DATA_T_NULL;
    varbind.oid = brInfoPrinterUStatusOID;

    out = snmp_encode_msg(g_buf_end, &msg_header, 1, &varbind);
    msg_len = (int) (g_buf_end - out + 1);
    
    hexdump("sending", out, msg_len);
    msg_len = network_udp_send(*conn, out, msg_len) != 0;
    if (msg_len < 0) {
        perror("sendto");
        goto out;
    }
    printf("...sent!\n");

    msg_len = network_udp_receive(*conn, out, 1024);
    if (msg_len < 0) {
        perror("recvfrom");
        goto out;
    }
    printf("...received!\n");
    hexdump("received", out, msg_len);
    
out:
    sleep(5);
}

static void
device_handler_stop(void *arg1, void *arg2)
{
    int *conn = arg1;
    printf("stopping\n");
    network_udp_disconnect(*conn);
    network_udp_free(*conn);
    
    free(conn);
}

void 
device_handler_init(void)
{
    int tid, conn;
    int *conn_p;
    
    if (iputils_get_local_ip(g_local_ip) != 0) {
        fprintf(stderr, "Could not get local ip address.\n");
        return;
    }

    conn = network_udp_init_conn(htons(49976));
    if (conn != 0) {
        fprintf(stderr, "Could not setup connection.\n");
        return;
    }
    
    if (network_udp_connect(conn, inet_addr("10.0.0.149"), htons(161)) != 0) {
        network_udp_free(conn);
        fprintf(stderr, "Could not connect to scanner.\n");
        return;
    }
    
    tid = event_thread_create("device_handler");
    
    conn_p = malloc(sizeof(conn));
    *conn_p = conn;
    event_thread_set_update_cb(tid, device_handler_loop, conn_p, NULL);
    event_thread_set_stop_cb(tid, device_handler_stop, conn_p, NULL);
}
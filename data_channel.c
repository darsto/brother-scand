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
#include "data_channel.h"
#include "event_thread.h"
#include "log.h"
#include "network.h"

static uint8_t g_buf[1024];
static uint8_t *const g_buf_end = g_buf + sizeof(g_buf) - 1;

int status = 0;

static void
data_channel_loop(void *arg1, void *arg2)
{
    int *conn = arg1;
    int msg_len;

    msg_len = network_tcp_receive(*conn, g_buf, 1024);
    if (msg_len < 0) {
        goto out;
    }

    if (status == 0) {
        uint8_t a[] = { 0x1b, 0x4b, 0x0a, 0x80 };
        msg_len = network_tcp_send(*conn, a, sizeof(a));
    } else if (status == 1) {
        uint8_t b[] = { 0x1b, 0x49, 0x0a, 0x52, 0x3d, 0x33, 0x30, 0x30, 0x2c, 0x33, 0x30, 0x30, 0x0a, 0x4d, 0x3d, 0x43, 0x47, 0x52, 0x41, 0x59, 0x0a, 0x44, 0x3d, 0x53, 0x49, 0x4e, 0x0a, 0x80 };
        msg_len = network_tcp_send(*conn, b, sizeof(b));
    } else if (status == 2) {
        uint8_t c[] = { 0x1b, 0x58, 0x0a, 0x52, 0x3d, 0x33, 0x30, 0x30, 0x2c, 0x33, 0x30, 0x30, 0x0a, 0x4d, 0x3d, 0x43, 0x47, 0x52, 0x41, 0x59, 0x0a, 0x43, 0x3d, 0x4a, 0x50, 0x45, 0x47, 0x0a, 0x4a, 0x3d, 0x4d, 0x49, 0x4e, 0x0a, 0x42, 0x3d, 0x35, 0x30, 0x0a, 0x4e, 0x3d, 0x35, 0x30, 0x0a, 0x41, 0x3d, 0x30, 0x2c, 0x30, 0x2c, 0x32, 0x34, 0x36, 0x34, 0x2c, 0x33, 0x34, 0x38, 0x34, 0x0a, 0x44, 0x3d, 0x53, 0x49, 0x4e, 0x0a, 0x47, 0x3d, 0x31, 0x0a, 0x4c, 0x3d, 0x31, 0x32, 0x38, 0x0a, 0x80};
        msg_len = network_tcp_send(*conn, c, sizeof(c));
    } else {
        goto out;
    }
    
    if (msg_len < 0) {
        perror("sendto");
        goto out;
    }
    
    ++status;
out:
    usleep(500 * 1000);
}

static void
data_channel_stop(void *arg1, void *arg2)
{
    int *conn = arg1;

    network_tcp_disconnect(*conn);
    network_tcp_free(*conn);

    free(conn);
}

void
data_channel_create(uint16_t port)
{
    int tid, conn;
    int *conn_p;

    conn = network_tcp_init_conn(htons(port), false);
    if (conn < 0) {
        fprintf(stderr, "Could not setup connection.\n");
        return;
    }

    if (network_tcp_connect(conn, inet_addr("10.0.0.149"), htons(54921)) != 0) {
        network_tcp_free(conn);
        fprintf(stderr, "Could not connect to scanner.\n");
        return;
    }
    
    tid = event_thread_create("data_channel");

    conn_p = malloc(sizeof(conn));
    *conn_p = conn;
    event_thread_set_update_cb(tid, data_channel_loop, conn_p, NULL);
    event_thread_set_stop_cb(tid, data_channel_stop, conn_p, NULL);
}
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

static void
data_channel_loop(void *arg1, void *arg2)
{
    int *conn = arg1;
    int msg_len;

    printf("looping\n");
    msg_len = network_tcp_receive(*conn, g_buf, 1024);
    if (msg_len < 0) {
        //perror("recvfrom");
        goto out;
    }
    hexdump("received", g_buf, msg_len);

/*    msg_len = network_tcp_send(*conn, g_buf, msg_len);
    if (msg_len < 0) {
        perror("sendto");
        goto out;
    }
    hexdump("sending", g_buf, msg_len);*/

out:
    sleep(1);
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
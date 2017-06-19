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
#include "button_handler.h"
#include "event_thread.h"
#include "log.h"
#include "network.h"

static uint8_t g_buf[1024];
static uint8_t *const g_buf_end = g_buf + sizeof(g_buf) - 1;

static void
button_handler_loop(void *arg1, void *arg2)
{
    int *conn = arg1;
    int msg_len;

    msg_len = network_udp_receive(*conn, g_buf, 1024);
    if (msg_len < 0) {
        perror("recvfrom");
        goto out;
    }
    hexdump("received", g_buf, msg_len);

    msg_len = network_udp_send(*conn, g_buf, msg_len);
    if (msg_len < 0) {
        perror("sendto");
        goto out;
    }
    hexdump("sending", g_buf, msg_len);

out:
    sleep(1);
}

static void
button_handler_stop(void *arg1, void *arg2)
{
    int *conn = arg1;

    network_udp_disconnect(*conn);
    network_udp_free(*conn);
    
    free(conn);
}

void
button_handler_run(uint16_t port)
{
    int tid, conn;
    int *conn_p;

    conn = network_udp_init_conn(htons(port), true);
    if (conn < 0) {
        fprintf(stderr, "Could not setup connection.\n");
        return;
    }

    tid = event_thread_create("button_handler");

    conn_p = malloc(sizeof(conn));
    *conn_p = conn;
    event_thread_set_update_cb(tid, button_handler_loop, conn_p, NULL);
    event_thread_set_stop_cb(tid, button_handler_stop, conn_p, NULL);
}
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
#include "data_channel.h"

#define DATA_CHANNEL_PORT 49424

struct button_handler {
    int thread;
    int conn;
    uint8_t buf[1024];
};

static void
button_handler_loop(void *arg1, void *arg2)
{
    struct button_handler *handler = arg1;
    int msg_len;

    msg_len = network_udp_receive(handler->conn, handler->buf, sizeof(handler->buf));
    if (msg_len < 0) {
        goto out;
    }

    msg_len = network_udp_send(handler->conn, handler->buf, msg_len);
    if (msg_len < 0) {
        perror("sendto");
        goto out;
    }

    data_channel_create(DATA_CHANNEL_PORT);
out:
    sleep(1);
}

static void
button_handler_stop(void *arg1, void *arg2)
{
    struct button_handler *handler = arg1;

    network_udp_disconnect(handler->conn);
    network_udp_free(handler->conn);
    
    free(handler);
}

void
button_handler_create(uint16_t port)
{
    int thread, conn;
    struct button_handler *handler;

    conn = network_udp_init_conn(htons(port), true);
    if (conn < 0) {
        fprintf(stderr, "Could not setup connection.\n");
        return;
    }

    thread = event_thread_create("button_handler");
    if (thread < 0) {
        fprintf(stderr, "Could not create button handler thread.\n");
        return;
    }

    handler = calloc(1, sizeof(*handler));
    handler->conn = conn;
    handler->thread = thread;

    event_thread_set_update_cb(thread, button_handler_loop, handler, NULL);
    event_thread_set_stop_cb(thread, button_handler_stop, handler, NULL);
}

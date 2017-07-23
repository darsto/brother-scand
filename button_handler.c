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
#include "network.h"
#include "data_channel.h"

#define DATA_PORT 54921

struct button_handler {
    int conn;
    uint8_t buf[1024];
    struct data_channel *channel;
};

static void
button_handler_loop(void *arg)
{
    struct button_handler *handler = arg;
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

    handler->channel = data_channel_create("10.0.0.149", DATA_PORT);
    if (handler->channel == NULL) {
        fprintf(stderr, "Fatal: failed to create data_channel.\n");
        goto out;
    }

out:
    sleep(1);
}

static void
button_handler_stop(void *arg)
{
    struct button_handler *handler = arg;

    network_udp_disconnect(handler->conn);
    network_udp_free(handler->conn);
    
    free(handler);
}

void
button_handler_create(uint16_t port)
{
    struct button_handler *handler;
    struct event_thread *thread;
    int conn;

    conn = network_udp_init_conn(htons(port), true);
    if (conn < 0) {
        fprintf(stderr, "Fatal: could not setup connection.\n");
        return;
    }

    handler = calloc(1, sizeof(*handler));
    if (handler == NULL) {
        fprintf(stderr, "Fatal: could not calloc button handler.\n");
        network_udp_free(conn);
        return;
    }

    handler->conn = conn;

    thread = event_thread_create("button_handler", button_handler_loop, button_handler_stop, handler);
    if (thread == NULL) {
        fprintf(stderr, "Fatal: could not create button handler thread.\n");
        network_udp_free(conn);
        free(handler);
        return;
    }
}

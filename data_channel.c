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

static uint8_t g_buf[2048];

struct data_channel {
    int conn;
    FILE *file;
    void (*process_cb)(struct data_channel *data_channel, void *arg);
};

static void
exchange_params(struct data_channel *data_channel, void *arg)
{
    int msg_len = 0, retries = 0;

    while (msg_len <= 0 && retries < 10) {
        msg_len = network_tcp_receive(data_channel->conn, g_buf, sizeof(g_buf));
        usleep(1000 * 25);
        ++retries;
    }

    if (retries == 10) {
        fprintf(stderr, "Couldn't receive scan params on data_channel %d\n", data_channel->conn);
        goto err;
    }

    //TODO
    abort();

    data_channel->file = fopen("/tmp/scan.jpg", "ab");
    
err:
    fprintf(stderr, "Failed exchange scan params on data_channel %d\n", data_channel->conn);
    abort();
}

static void 
init_connection(struct data_channel *data_channel, void *arg)
{
    int msg_len = 0, retries = 0;
    
    while (msg_len <= 0 && retries < 10) {
        msg_len = network_tcp_receive(data_channel->conn, g_buf, sizeof(g_buf));
        usleep(1000 * 25);
        ++retries;
    }

    if (retries == 10) {
        fprintf(stderr, "Couldn't receive welcome message on data_channel %d\n", data_channel->conn);
        goto err;
    }
    
    if (g_buf[0] != '+') {
        fprintf(stderr, "Received invalid welcome message on data_channel %d\n", data_channel->conn);
        hexdump("received message", g_buf, msg_len);
        goto err;
    }

    msg_len = 0, retries = 0;
    while (msg_len <= 0 && retries < 10) {
        msg_len = network_tcp_send(data_channel->conn, "\x1b\x4b\x0a\x80", 4);
        usleep(1000 * 25);
        ++retries;
    }

    if (retries == 10) {
        fprintf(stderr, "Couldn't send welcome message on data_channel %d\n", data_channel->conn);
        goto err;
    }

    data_channel->process_cb = exchange_params;
    return;
    
err:
    fprintf(stderr, "Failed to init data_channel %d\n", data_channel->conn);
    abort();
}

static void
data_channel_loop(void *arg1, void *arg2)
{
    struct data_channel *data_channel = arg1;
    
    data_channel->process_cb(data_channel, arg2);
}

static void
data_channel_stop(void *arg1, void *arg2)
{
    struct data_channel *data_channel = arg1;

    if (data_channel->file) {
        fclose(data_channel->file);
    }

    network_tcp_disconnect(data_channel->conn);
    network_tcp_free(data_channel->conn);

    free(data_channel);
}

void
data_channel_create(uint16_t port)
{
    int tid, conn;
    struct data_channel *data_channel;

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

    data_channel = calloc(1, sizeof(*data_channel));
    data_channel->conn = conn;
    data_channel->process_cb = init_connection;
    
    event_thread_set_update_cb(tid, data_channel_loop, data_channel, NULL);
    event_thread_set_stop_cb(tid, data_channel_stop, data_channel, NULL);
}
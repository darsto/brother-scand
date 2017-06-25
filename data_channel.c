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
#include <assert.h>
#include <memory.h>
#include "data_channel.h"
#include "event_thread.h"
#include "log.h"
#include "network.h"

#define DATA_CHANNEL_MAX_PARAMS 16

static uint8_t g_buf[2048];

struct data_channel_param {
    char id;
    char value[16];
};

struct data_channel {
    int conn;
    FILE *file;
    struct data_channel_param params[DATA_CHANNEL_MAX_PARAMS];
    void (*process_cb)(struct data_channel *data_channel, void *arg);
};

static struct data_channel_param *
get_data_channel_param_by_index(struct data_channel *data_channel, uint8_t index)
{
    if (index >= DATA_CHANNEL_MAX_PARAMS) {
        return NULL;
    }

    return (struct data_channel_param *) &data_channel->params + index;
}

static struct data_channel_param *
get_data_channel_param_by_id(struct data_channel *data_channel, uint8_t id)
{
    struct data_channel_param *ret;
    uint8_t i = 0;
    
    do {
        ret = get_data_channel_param_by_index(data_channel, i++);
    } while (ret != NULL && ret->id != id);
    
    return ret;
}

static void
exchange_params2(struct data_channel *data_channel, void *arg)
{    
    // TODO yet unimplemented
    abort();
}

// TODO replace asserts with errors (its actually user input that's being handled)
static void
exchange_params1(struct data_channel *data_channel, void *arg)
{
    int msg_len = 0, retries = 0;
    size_t len, i;
    uint8_t *buf, *buf_end;
    uint8_t option;
    struct data_channel_param *param;

    while (msg_len <= 0 && retries < 10) {
        msg_len = network_tcp_receive(data_channel->conn, g_buf, sizeof(g_buf));
        usleep(1000 * 25);
        ++retries;
    }

    if (retries == 10) {
        fprintf(stderr, "Couldn't receive initial scan params on data_channel %d\n", data_channel->conn);
        goto err;
    }

    /* prepare default data for response */
    param = get_data_channel_param_by_id(data_channel, 'R');
    strcpy(param->value, "300,300");

    param = get_data_channel_param_by_id(data_channel, 'M');
    strcpy(param->value, "CGRAY");

    param = get_data_channel_param_by_id(data_channel, 'D');
    strcpy(param->value, "SIN");
    
    /* process received data */
    assert(g_buf[0] == 0x30); // ??
    assert(g_buf[1] == 0x15); // ??
    assert(g_buf[2] == 0x00); // ??
    
    assert(g_buf[msg_len - 1] == 0x80); // end of message
    assert(g_buf[msg_len - 2] == 0x0a); // end of param
    
    buf = g_buf + 3;
    buf_end = g_buf + msg_len - 2;
    
    while (buf < buf_end) {
        option = *buf++;
        assert(*buf == '=');
        ++buf;
        
        param = get_data_channel_param_by_id(data_channel, option);
        assert(param != NULL);
        
        i = 0;
        while (*buf != 0x0a) {
            param->value[i++] = *buf++;
            if (i >= sizeof(param->value) - 1) {
                fprintf(stderr, "Received value longer than %zu bytes!\n", sizeof(param->value) - 1);
                goto err;
            }
        }
        param->value[i] = 0;
        
        ++buf;
    }
    
    /* prepare a response */
    buf = g_buf;
    *buf++ = 0x1b; // magic sequence
    *buf++ = 0x49; // packet id (?)
    *buf++ = 0x0a; // header end
    
    i = 0;
    while ((param = get_data_channel_param_by_index(data_channel, i++)) != NULL) {
        len = strlen(param->value);
        if (len == 0) {
            continue;
        }
        
        *buf++ = (uint8_t) param->id;
        *buf++ = '=';
        memcpy(buf, param->value, len);
        buf += len;
        *buf++ = 0x0a;
    }
    
    *buf++ = 0x80; // end of message

    msg_len = 0, retries = 0;
    while (msg_len <= 0 && retries < 10) {
        msg_len = network_tcp_send(data_channel->conn, g_buf, buf - g_buf);
        usleep(1000 * 25);
        ++retries;
    }

    if (retries == 10) {
        fprintf(stderr, "Couldn't send initial scan params on data_channel %d\n", data_channel->conn);
        goto err;
    }

    data_channel->process_cb = exchange_params2;
    return;
    
err:
    fprintf(stderr, "Failed to exchange initial scan params on data_channel %d\n", data_channel->conn);
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

    data_channel->process_cb = exchange_params1;
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
    data_channel->params[0].id = 'F';
    data_channel->params[1].id = 'D';
    data_channel->params[2].id = 'E';
    data_channel->params[3].id = 'R';
    data_channel->params[4].id = 'M';
    data_channel->process_cb = init_connection;
    
    event_thread_set_update_cb(tid, data_channel_loop, data_channel, NULL);
    event_thread_set_stop_cb(tid, data_channel_stop, data_channel, NULL);
}
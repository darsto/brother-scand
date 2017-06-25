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
#include <zconf.h>
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

static int
read_data_channel_params(struct data_channel *data_channel, uint8_t *buf, uint8_t *buf_end, const char *whitelist)
{
    struct data_channel_param *param;
    uint8_t id;
    size_t i;
    
    while (buf < buf_end) {
        id = *buf++;
        assert(*buf == '=');
        ++buf;

        param = get_data_channel_param_by_id(data_channel, id);
        assert(param != NULL);

        i = 0;
        while (*buf != 0x0a) {
            if (whitelist == NULL || strchr(whitelist, id) != NULL) {
                param->value[i++] = *buf;
            }
            
            if (i >= sizeof(param->value) - 1) {
                fprintf(stderr, "Received data_channel param longer than %zu bytes!\n", sizeof(param->value) - 1);
                return -1;
            }
            
            ++buf;
        }
        param->value[i] = 0;

        ++buf;
    }
    
    return 0;
}

static uint8_t *
write_data_channel_params(struct data_channel *data_channel, uint8_t *buf, const char *whitelist)
{
    struct data_channel_param *param;
    size_t len, i = 0;
    
    while ((param = get_data_channel_param_by_index(data_channel, i++)) != NULL) {
        len = strlen(param->value);
        if (len == 0) {
            continue;
        }

        if (whitelist != NULL && strchr(whitelist, param->id) == NULL) {
            continue;
        }
        
        *buf++ = (uint8_t) param->id;
        *buf++ = '=';
        memcpy(buf, param->value, len);
        buf += len;
        *buf++ = 0x0a;
    }
    
    return buf;
}

static void
receive_data(struct data_channel *data_channel, void *arg)
{
    int msg_len = 0, retries = 0;
    uint8_t *buf;
    
    while (msg_len <= 0 && retries < 10) {
        msg_len = network_tcp_receive(data_channel->conn, g_buf, sizeof(g_buf));
        usleep(1000 * 25);
        ++retries;
    }

    if (retries == 10) {
        fprintf(stderr, "Couldn't receive first data packet on data_channel %d\n", data_channel->conn);
        goto err;
    }

    buf = g_buf;
    
    /* Even that scanner sends data in realtime, it does so in bulks.
     * The following sequence is a metadata at the beginning of a "bulk" 
     * TODO (not decoded yet, for now JPEG 0xff 0xd9 is checked to detect the end of data) */
    if (*buf == 0x64 && *(buf + 1) == 0x07) {
        buf += 12;
        msg_len -= 12;
    }

    fwrite(buf, sizeof(*buf), (size_t) msg_len, data_channel->file);

    if (buf[msg_len - 2] == 0xff && buf[msg_len - 1] == 0xd9) {
        /* JPEG ending sequence */
        printf("Successfully received an image on data_channel %d\n", data_channel->conn);
        fclose(data_channel->file);
        abort(); // TODO exit gracefully
    }
    
    return;
err:
    abort();
}

static void
exchange_params2(struct data_channel *data_channel, void *arg)
{
    struct data_channel_param *param;
    uint8_t *buf, *buf_end;
    long recv_params[7];
    int msg_len = 0, retries = 0;
    size_t i, len;
    long tmp;

    while (msg_len <= 0 && retries < 10) {
        msg_len = network_tcp_receive(data_channel->conn, g_buf, sizeof(g_buf));
        usleep(1000 * 25);
        ++retries;
    }

    if (retries == 10) {
        fprintf(stderr, "Couldn't receive scan params on data_channel %d\n", data_channel->conn);
        goto err;
    }

    /* process received data */
    assert(g_buf[0] == 0x00); // ??
    assert(g_buf[1] == 0x1d); // ??
    assert(g_buf[2] == 0x00); // ??
    assert(g_buf[msg_len - 1] == 0x00);
    
    i = 0;
    buf_end = buf = g_buf + 3;

    /* process dpi x and y */
    while(i < 2 && *buf_end != 0x00) {
        recv_params[i++] = strtol((char *) buf, (char **) &buf_end, 10);
        buf = ++buf_end;
    }
    
    len = buf_end - g_buf - 4;
    assert(len < 15);
    param = get_data_channel_param_by_id(data_channel, 'R');
    assert(param);

    /* previously sent and just received dpi should match */
    if (strncmp((char *) (g_buf + 3), param->value, len) != 0) {
        printf("Scanner does not support given dpi: %s. %.*s will be used instead\n",
               param->value, (int) len, (char *) (g_buf + 3));
        
        memcpy(param->value, g_buf + 3, len);
        param->value[len] = 0;
    }
    
    while(i < sizeof(recv_params) / sizeof(recv_params[0]) && *buf_end != 0x00) {
        tmp = strtol((char *) buf, (char **) &buf_end, 10);
        assert(tmp > 0 && tmp < USHRT_MAX);
        recv_params[i++] = tmp;
        buf = ++buf_end;
    }
    
    assert(*buf == 0x00);
    
    param = get_data_channel_param_by_id(data_channel, 'A');
    assert(param);
    sprintf(param->value, "0,0,%ld,%ld", recv_params[4], recv_params[6]);

    /* prepare a response */
    buf = g_buf;
    *buf++ = 0x1b; // magic sequence
    *buf++ = 0x58; // packet id (?)
    *buf++ = 0x0a; // header end
    
    buf = write_data_channel_params(data_channel, buf, "RMCJBNADGL");
    if (buf == NULL) {
        fprintf(stderr, "Failed to write scan params on data_channel %d\n", data_channel->conn);
        goto err;
    }

    *buf++ = 0x80; // end of message

    msg_len = 0, retries = 0;
    while (msg_len <= 0 && retries < 10) {
        msg_len = network_tcp_send(data_channel->conn, g_buf, buf - g_buf);
        usleep(1000 * 25);
        ++retries;
    }

    if (retries == 10) {
        fprintf(stderr, "Couldn't send scan params on data_channel %d\n", data_channel->conn);
        goto err;
    }

    data_channel->file = fopen("/tmp/scan.jpg", "wb");
    data_channel->process_cb = receive_data;
    return;
    
err:
    fprintf(stderr, "Failed to exchange scan params on data_channel %d\n", data_channel->conn);
    abort();
}

// TODO replace asserts with errors (its actually user input that's being handled)
static void
exchange_params1(struct data_channel *data_channel, void *arg)
{
    struct data_channel_param *param;
    uint8_t *buf, *buf_end;
    int msg_len = 0, retries = 0;

    while (msg_len <= 0 && retries < 10) {
        msg_len = network_tcp_receive(data_channel->conn, g_buf, sizeof(g_buf));
        usleep(1000 * 25);
        ++retries;
    }

    if (retries == 10) {
        fprintf(stderr, "Couldn't receive initial scan params on data_channel %d\n", data_channel->conn);
        goto err;
    }
    
    /* process received data */
    assert(g_buf[0] == 0x30); // ??
    assert(g_buf[1] == 0x15); // ??
    assert(g_buf[2] == 0x00); // ??
    
    assert(g_buf[msg_len - 1] == 0x80); // end of message
    assert(g_buf[msg_len - 2] == 0x0a); // end of param
    
    buf = g_buf + 3;
    buf_end = g_buf + msg_len - 2;
    
    if (read_data_channel_params(data_channel, buf, buf_end, NULL) != 0) {
        fprintf(stderr, "Failed to process initial scan params on data_channel %d\n", data_channel->conn);
        goto err;
    }
    
    /* prepare a response */
    buf = g_buf;
    *buf++ = 0x1b; // magic sequence
    *buf++ = 0x49; // packet id (?)
    *buf++ = 0x0a; // header end
    
    buf = write_data_channel_params(data_channel, buf, "RMD");
    if (buf == NULL) {
        fprintf(stderr, "Failed to write initial scan params on data_channel %d\n", data_channel->conn);
        goto err;
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

static void
init_data_channel(struct data_channel *data_channel, int conn)
{
    struct data_channel_param *param;
    int i = 0;

    data_channel->conn = conn;
    data_channel->process_cb = init_connection;

#define DATA_CH_PARAM(ID, VAL) \
    param = &data_channel->params[i++]; \
    param->id = ID; \
    strcpy(param->value, VAL); 
    
    DATA_CH_PARAM('F', "");
    DATA_CH_PARAM('D', "SIN");
    DATA_CH_PARAM('E', "");
    DATA_CH_PARAM('R', "300,300");
    DATA_CH_PARAM('M', "CGRAY");
    DATA_CH_PARAM('E', "");
    DATA_CH_PARAM('C', "JPEG");
    DATA_CH_PARAM('J', "");
    DATA_CH_PARAM('B', "50");
    DATA_CH_PARAM('N', "50");
    DATA_CH_PARAM('A', "");
    DATA_CH_PARAM('G', "1");
    DATA_CH_PARAM('L', "128");
    
#undef DATA_CH_PARAM
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
    init_data_channel(data_channel, conn);
    
    event_thread_set_update_cb(tid, data_channel_loop, data_channel, NULL);
    event_thread_set_stop_cb(tid, data_channel_stop, data_channel, NULL);
}
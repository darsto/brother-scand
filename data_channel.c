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
#define DATA_CHANNEL_CHUNK_SIZE 0x10000
#define DATA_CHANNEL_CHUNK_HEADER_SIZE 0xC
#define DATA_CHANNEL_CHUNK_MAX_PROGRESS 0x1000
#define DATA_CHANNEL_PORT 49424

struct data_channel_param {
    char id;
    char value[16];
};

struct data_channel {
    int conn;
    void (*process_cb)(struct data_channel *data_channel);

    FILE *tempfile;

    struct data_channel_page_data {
        int remaining_chunk_bytes;
        bool last_chunk;
    } page_data;

    int scanned_pages;
    struct event_thread *thread;

    struct data_channel_param params[DATA_CHANNEL_MAX_PARAMS];
    uint8_t buf[2048];
};

static void receive_initial_data(struct data_channel *data_channel);

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
    size_t len;
    uint8_t i = 0;

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
receive_data_footer(struct data_channel *data_channel)
{
    int msg_len = 0, retries = 0;
    uint8_t msg[10];

    while (msg_len <= 0 && retries < 10) {
        msg_len = network_receive(data_channel->conn, data_channel->buf, sizeof(data_channel->buf));
        usleep(1000 * 25);
        ++retries;
    }

    if (retries == 10) {
        fprintf(stderr, "Couldn't receive data packet on data_channel %d\n", data_channel->conn);
        goto err;
    }

    if (msg_len != 10) {
        fprintf(stderr, "Received invalid data footer on data_channel %d. length = %d instead of %zu\n", data_channel->conn, msg_len, sizeof(msg));
        goto err;
    }

    memcpy(msg, data_channel->buf, sizeof(msg));
    // 10 bytes of data, usually:
    // 8207 (probably magic number)
    // 0001 (id of just scanned page - starting with 1)
    // 0084 0000 0000 (unknown - seem constant)

    data_channel->process_cb = receive_initial_data;
    return;

err:
    abort();
}

static int
parse_chunk_header(struct data_channel *data_channel)
{
    int progress;
    int total_chunk_size;

    // bytes 0-5 are unknown, but seem constant (64 07 00 01 00 84)
    // 6-7 is overall progress (little endian)
    // 8-9 seem constant (00 00)
    // 10-11 is upcoming chunk size in bytes (little endian)

    progress = (data_channel->buf[6] | (data_channel->buf[7] << 8)) * 100 / DATA_CHANNEL_CHUNK_MAX_PROGRESS;
    printf("data_channel %d: Receiving data: %d%%\n", data_channel->conn, progress);

    data_channel->page_data.remaining_chunk_bytes = (data_channel->buf[10] | (data_channel->buf[11] << 8));
    total_chunk_size = data_channel->page_data.remaining_chunk_bytes + DATA_CHANNEL_CHUNK_HEADER_SIZE;

    if (total_chunk_size > DATA_CHANNEL_CHUNK_SIZE) {
        fprintf(stderr, "Invalid chunk size on data_channel %d\n", data_channel->conn);
        return -1;
    }

    data_channel->page_data.last_chunk = (total_chunk_size < DATA_CHANNEL_CHUNK_SIZE);

    return 0;
}

static int
process_data(struct data_channel *data_channel, uint8_t *buf, int msg_len)
{
    int rc;
    size_t size;
    char filename[64];
    FILE* destfile;

    if (data_channel->page_data.remaining_chunk_bytes == 0) {
        rc = parse_chunk_header(data_channel);
        if (rc != 0) {
            fprintf(stderr, "Couldn't parse header on data_channel %d\n", data_channel->conn);
            return -1;
        }

        buf += DATA_CHANNEL_CHUNK_HEADER_SIZE;
        msg_len -= DATA_CHANNEL_CHUNK_HEADER_SIZE;
    }

    fwrite(buf, sizeof(*buf), (size_t) msg_len, data_channel->tempfile);
    data_channel->page_data.remaining_chunk_bytes -= msg_len;

    if (data_channel->page_data.remaining_chunk_bytes < 0) {
        fprintf(stderr, "Received too much (invalid) data on data_channel %d\n", data_channel->conn);
        return -1;
    }

    if (data_channel->page_data.remaining_chunk_bytes == 0 && data_channel->page_data.last_chunk) {
        printf("Successfully received image data on data_channel %d\n", data_channel->conn);

        assert(data_channel->scanned_pages < INT_MAX);
        sprintf(filename, "scan%d.jpg", data_channel->scanned_pages++);
        destfile = fopen(filename, "w");
        if (destfile == NULL) {
            fprintf(stderr, "Cannot create file '%s' on data_channel %d\n", filename, data_channel->conn);
            return -1;
        }

        fseek(data_channel->tempfile, 0, SEEK_SET);
        while ((size = fread(data_channel->buf, 1, sizeof(data_channel->buf), data_channel->tempfile))) {
            fwrite(data_channel->buf, 1, size, destfile);
        }

        fclose(destfile);
        fclose(data_channel->tempfile);
        data_channel->tempfile = NULL;

        data_channel->process_cb = receive_data_footer;
    }

    return 0;
}

static void
receive_data(struct data_channel *data_channel)
{
    int msg_len = 0, retries = 0;
    int rc;

    while (msg_len <= 0 && retries < 10) {
        msg_len = network_receive(data_channel->conn, data_channel->buf, sizeof(data_channel->buf));
        usleep(1000 * 25);
        ++retries;
    }

    if (retries == 10) {
        fprintf(stderr, "Couldn't receive data packet on data_channel %d\n", data_channel->conn);
        goto err;
    }

    rc = process_data(data_channel, data_channel->buf, msg_len);
    if (rc != 0) {
        fprintf(stderr, "Couldn't process data packet on data_channel %d\n", data_channel->conn);
        goto err;
    }

    return;

err:
    abort();
}

static void
data_channel_reset_page_data(struct data_channel *data_channel)
{
    memset(&data_channel->page_data, 0, sizeof(data_channel->page_data));
}

static void
set_paused(struct data_channel *data_channel)
{
    event_thread_pause(data_channel->thread);
    sleep(1);
}

static void
receive_initial_data(struct data_channel *data_channel)
{
    int msg_len = 0, retries = 0, rc;

    /* 15 seconds timeout */
    while (msg_len <= 0 && retries < 300) {
        msg_len = network_receive(data_channel->conn, data_channel->buf, sizeof(data_channel->buf));
        usleep(1000 * 50);
        ++retries;
    }

    if (retries == 300 || (msg_len == 1 && data_channel->buf[0] == 0x80)) {
        /* no more documents to scan */
        data_channel->process_cb = set_paused;
        return;
    }

    data_channel_reset_page_data(data_channel);
    data_channel->tempfile = tmpfile();
    if (data_channel->tempfile == NULL) {
        fprintf(stderr, "Cannot create temp file on data_channel %d\n", data_channel->conn);
        goto err;
    }

    rc = process_data(data_channel, data_channel->buf, msg_len);
    if (rc != 0) {
        fprintf(stderr, "Couldn't process initial data packet on data_channel %d\n", data_channel->conn);
        goto err;
    }

    data_channel->process_cb = receive_data;
    return;

err:
    abort();
}

static void
exchange_params2(struct data_channel *data_channel)
{
    struct data_channel_param *param;
    uint8_t *buf, *buf_end;
    long recv_params[7];
    int msg_len = 0, retries = 0;
    size_t i, len;
    long tmp;

    while (msg_len <= 0 && retries < 10) {
        msg_len = network_receive(data_channel->conn, data_channel->buf, sizeof(data_channel->buf));
        usleep(1000 * 25);
        ++retries;
    }

    if (retries == 10) {
        fprintf(stderr, "Couldn't receive scan params on data_channel %d\n", data_channel->conn);
        goto err;
    }

    /* process received data */
    assert(data_channel->buf[0] == 0x00); // ??
    assert(data_channel->buf[1] == 0x1d); // ??
    assert(data_channel->buf[2] == 0x00); // ??
    assert(data_channel->buf[msg_len - 1] == 0x00);

    i = 0;
    buf_end = buf = data_channel->buf + 3;

    /* process dpi x and y */
    while(i < 2 && *buf_end != 0x00) {
        recv_params[i++] = strtol((char *) buf, (char **) &buf_end, 10);
        buf = ++buf_end;
    }

    len = buf_end - data_channel->buf - 4;
    assert(len < 15);
    param = get_data_channel_param_by_id(data_channel, 'R');
    assert(param);

    /* previously sent and just received dpi should match */
    if (strncmp((char *) (data_channel->buf + 3), param->value, len) != 0) {
        printf("Scanner does not support given dpi: %s. %.*s will be used instead\n",
               param->value, (int) len, (char *) (data_channel->buf + 3));

        memcpy(param->value, data_channel->buf + 3, len);
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
    buf = data_channel->buf;
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
        msg_len = network_send(data_channel->conn, data_channel->buf, buf - data_channel->buf);
        usleep(1000 * 25);
        ++retries;
    }

    if (retries == 10) {
        fprintf(stderr, "Couldn't send scan params on data_channel %d\n", data_channel->conn);
        goto err;
    }

    data_channel->process_cb = receive_initial_data;
    return;

err:
    fprintf(stderr, "Failed to exchange scan params on data_channel %d\n", data_channel->conn);
    abort();
}

// TODO replace asserts with errors (its actually user input that's being handled)
static void
exchange_params1(struct data_channel *data_channel)
{
    uint8_t *buf, *buf_end;
    int msg_len = 0, retries = 0;

    while (msg_len <= 0 && retries < 10) {
        msg_len = network_receive(data_channel->conn, data_channel->buf, sizeof(data_channel->buf));
        usleep(1000 * 25);
        ++retries;
    }

    if (retries == 10) {
        fprintf(stderr, "Couldn't receive initial scan params on data_channel %d\n", data_channel->conn);
        goto err;
    }

    /* process received data */
    assert(data_channel->buf[0] == 0x30); // ??
    assert(data_channel->buf[1] == 0x15); // ??
    assert(data_channel->buf[2] == 0x00); // ??

    assert(data_channel->buf[msg_len - 1] == 0x80); // end of message
    assert(data_channel->buf[msg_len - 2] == 0x0a); // end of param

    buf = data_channel->buf + 3;
    buf_end = data_channel->buf + msg_len - 2;

    if (read_data_channel_params(data_channel, buf, buf_end, NULL) != 0) {
        fprintf(stderr, "Failed to process initial scan params on data_channel %d\n", data_channel->conn);
        goto err;
    }

    /* prepare a response */
    buf = data_channel->buf;
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
        msg_len = network_send(data_channel->conn, data_channel->buf, buf - data_channel->buf);
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
init_connection(struct data_channel *data_channel)
{
    int msg_len = 0, retries = 0;

    while (msg_len <= 0 && retries < 10) {
        msg_len = network_receive(data_channel->conn, data_channel->buf, sizeof(data_channel->buf));
        usleep(1000 * 25);
        ++retries;
    }

    if (retries == 10) {
        fprintf(stderr, "Couldn't receive welcome message on data_channel %d\n", data_channel->conn);
        goto err;
    }

    if (data_channel->buf[0] != '+') {
        fprintf(stderr, "Received invalid welcome message on data_channel %d\n", data_channel->conn);
        hexdump("received message", data_channel->buf, (size_t) msg_len);
        goto err;
    }

    msg_len = 0, retries = 0;
    while (msg_len <= 0 && retries < 10) {
        msg_len = network_send(data_channel->conn, "\x1b\x4b\x0a\x80", 4);
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
data_channel_loop(void *arg)
{
    struct data_channel *data_channel = arg;

    data_channel->process_cb(data_channel);
}

static void
data_channel_stop(void *arg)
{
    struct data_channel *data_channel = arg;

    if (data_channel->tempfile) {
        fclose(data_channel->tempfile);
    }

    network_disconnect(data_channel->conn);

    free(data_channel);
}

static void
init_data_channel(struct data_channel *data_channel)
{
    struct data_channel_param *param;
    int i = 0;

    data_channel->thread = event_thread_self();
    data_channel->process_cb = set_paused;

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
data_channel_kick_cb(void *arg1, void *arg2)
{
    struct data_channel *data_channel = arg1;

    if (data_channel->process_cb != set_paused) {
        fprintf(stderr, "Trying to kick non-sleeping data_channel %d.\n", data_channel->conn);
        return;
    }

    data_channel->process_cb = init_connection;
}

void
data_channel_kick(struct data_channel *data_channel)
{
    struct event_thread *thread = data_channel->thread;
    int rc;

    rc = event_thread_enqueue_event(thread, data_channel_kick_cb, data_channel, NULL);
    if (rc != 0) {
        goto err;
    }

    rc = event_thread_kick(thread);
    if (rc != 0) {
        goto err;
    }

    return;

err:
    fprintf(stderr, "Failed to kick data_channel %d.\n", data_channel->conn);
}

struct data_channel *
data_channel_create(const char *dest_ip, uint16_t port)
{
    struct data_channel *data_channel;
    struct event_thread *thread;
    int conn;

    conn = network_init_conn(NETWORK_TYPE_TCP, htons(DATA_CHANNEL_PORT), inet_addr(dest_ip), htons(port));
    if (conn < 0) {
        fprintf(stderr, "Could not connect to scanner.\n");
        return NULL;
    }

    data_channel = calloc(1, sizeof(*data_channel));
    if (data_channel == NULL) {
        fprintf(stderr, "Failed to calloc data_channel.\n");
        network_disconnect(conn);
        return NULL;
    }

    data_channel->conn = conn;
    data_channel->process_cb = init_data_channel;

    thread = event_thread_create("data_channel", data_channel_loop, data_channel_stop, data_channel);
    if (thread == NULL) {
        fprintf(stderr, "Failed to create data_channel thread.\n");
        network_disconnect(conn);
        free(data_channel);
        return NULL;
    }

    data_channel->thread = thread;
    return data_channel;
}

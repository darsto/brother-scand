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
        int id;
        int remaining_chunk_bytes;
    } page_data;

    unsigned scanned_pages;
    struct event_thread *thread;

    struct data_channel_param params[DATA_CHANNEL_MAX_PARAMS];
    uint8_t buf[2048];
    char *dest_ip;
    uint16_t dest_port;
};

struct data_packet_header {
    uint8_t id;
    uint16_t magic;
    uint16_t page_id;
    uint8_t unk2;
    uint16_t progress;
    uint16_t unk3;
    uint8_t *payload;
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

static int
process_page_end_header(struct data_channel *data_channel, struct data_packet_header *header,
                        uint32_t payload_len)
{
    FILE* destfile;
    char filename[64];
    size_t size;

    if (header->page_id != data_channel->page_data.id) {
        fprintf(stderr, "data_channel %d: packet page_id mismatch (packet %u != local %u)\n",
                data_channel->conn, header->page_id, data_channel->scanned_pages + 1);
        return -1;
    }

    sprintf(filename, "scan%u.jpg", data_channel->scanned_pages++);
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

    data_channel->process_cb = receive_initial_data;
    printf("data_channel %d: successfully received page %u\n", data_channel->conn, header->page_id);

    return 0;
}

static int
process_chunk_header(struct data_channel *data_channel, struct data_packet_header *header,
                     uint32_t payload_len)
{
    int progress_percent;
    int total_chunk_size;

    if (payload_len < 2) {
        fprintf(stderr, "data_channel %d: payload too small (%u/2 bytes)\n",
                data_channel->conn, payload_len);
        return -1;
    }

    if (data_channel->page_data.id == 0) {
        printf("data_channel %d: now scanning page id %u\n", data_channel->conn, header->page_id);
        data_channel->page_data.id = header->page_id;
    } else if (header->page_id != data_channel->page_data.id) {
        fprintf(stderr, "data_channel %d: packet page_id mismatch (packet %u != local %u)\n",
                data_channel->conn, header->page_id, data_channel->page_data.id);
        return -1;
    }

    progress_percent = header->progress * 100 / DATA_CHANNEL_CHUNK_MAX_PROGRESS;
    printf("data_channel %d: Receiving data: %d%%\n", data_channel->conn, progress_percent);

    data_channel->page_data.remaining_chunk_bytes = header->payload[0] | (header->payload[1] << 8);
    total_chunk_size = data_channel->page_data.remaining_chunk_bytes + DATA_CHANNEL_CHUNK_HEADER_SIZE;

    if (total_chunk_size > DATA_CHANNEL_CHUNK_SIZE) {
        fprintf(stderr, "Invalid chunk size on data_channel %d\n", data_channel->conn);
        return -1;
    }

    return 0;
}

static int
process_header(struct data_channel *data_channel, uint8_t *buf, uint32_t buf_len)
{
    struct data_packet_header header;
    uint32_t payload_len;
    int rc;

    if (buf_len < 10) {
        fprintf(stderr, "data_channel %d: invalid header length (%u/10+ bytes)\n",
                data_channel->conn, buf_len);
        return -1;
    }

    header.id = buf[0];
    header.magic = buf[1] | (buf[2] << 8);
    header.page_id = buf[3] | (buf[4] << 8);
    header.unk2 = buf[5];
    header.progress = buf[6] | (buf[7] << 8);
    header.unk3 = buf[8] | (buf[9] << 8);
    header.payload = &buf[10];
    payload_len = buf_len - 10;

    if (header.magic != 0x07) {
        fprintf(stderr, "data_channel %d: invalid header magic number (%u != 0x07)\n",
                data_channel->conn, header.magic);
        return -1;
    }

    switch (header.id) {
        case 0x64:
            rc = process_chunk_header(data_channel, &header, payload_len);
            break;
        case 0x82:
            rc = process_page_end_header(data_channel, &header, payload_len);
            if (rc == 0) {
                rc = 1;
            }
            break;
        default:
            fprintf(stderr, "data_channel %d: received unsupported header (id = %u)\n",
                    data_channel->conn, header.id);
            rc = -1;
    }

    return rc;
}

static int
process_data(struct data_channel *data_channel, uint8_t *buf, int msg_len)
{
    int old_rem_chunk_bytes, rc;

    if (data_channel->page_data.remaining_chunk_bytes < msg_len) {
        /* chunk header might be somewhere inside this packet */
        if (data_channel->page_data.remaining_chunk_bytes > 0) {
            /* chunk header is in the middle of this packet */
            old_rem_chunk_bytes = data_channel->page_data.remaining_chunk_bytes;

            /* process up to chunk header */
            rc = process_data(data_channel, buf, data_channel->page_data.remaining_chunk_bytes);
            if (rc != 0) {
                return rc;
            }

            buf += old_rem_chunk_bytes;
            msg_len -= old_rem_chunk_bytes;
        }

        rc = process_header(data_channel, buf, msg_len);
        if (rc == 1) {
            return 0;
        } else if (rc != 0) {
            fprintf(stderr, "Couldn't parse header on data_channel %d\n", data_channel->conn);
            return -1;
        }

        buf += DATA_CHANNEL_CHUNK_HEADER_SIZE;
        msg_len -= DATA_CHANNEL_CHUNK_HEADER_SIZE;
    }

    fwrite(buf, sizeof(*buf), (size_t) msg_len, data_channel->tempfile);
    data_channel->page_data.remaining_chunk_bytes -= msg_len;

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
    struct data_channel_param *param;
    uint8_t *buf, *buf_end;
    int msg_len = 0, retries = 0;
    size_t str_len;


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
    //buf[1] == 0x15 or 0x55 (might refer to automatic/manual scan)
    assert(data_channel->buf[2] == 0x00); // ??

    assert(data_channel->buf[msg_len - 1] == 0x80); // end of message
    assert(data_channel->buf[msg_len - 2] == 0x0a); // end of param

    buf = data_channel->buf + 3;
    buf_end = data_channel->buf + msg_len - 2;

    if (read_data_channel_params(data_channel, buf, buf_end, NULL) != 0) {
        fprintf(stderr, "Failed to process initial scan params on data_channel %d\n", data_channel->conn);
        goto err;
    }

    param = get_data_channel_param_by_id(data_channel, 'R');
    if (strchr(param->value, ',') == NULL) {
        str_len = strlen(param->value);
        if (str_len >= sizeof(param->value) - 1) {
            fprintf(stderr, "data_channel %d: received invalid resolution %s\n", data_channel->conn, param->value);
            goto err;
        }

        param->value[str_len] = ',';
        memcpy(&param->value[str_len + 1], param->value, str_len);
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

    if (data_channel->conn >= 0) {
        data_channel->conn = network_reconnect(data_channel->conn);
    } else {
        data_channel->conn = network_init_conn(NETWORK_TYPE_TCP, htons(DATA_CHANNEL_PORT), inet_addr(data_channel->dest_ip), htons(data_channel->dest_port));
    }

    if (data_channel->conn < 0) {
        fprintf(stderr, "Could not connect to scanner.\n");
        goto err;
    }

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

    free(data_channel->dest_ip);
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
    DATA_CH_PARAM('T', "JPEG");
    DATA_CH_PARAM('J', "");
    DATA_CH_PARAM('B', "50");
    DATA_CH_PARAM('N', "50");
    DATA_CH_PARAM('A', "");
    DATA_CH_PARAM('G', "1");
    DATA_CH_PARAM('L', "128");
    DATA_CH_PARAM('P', "A4");

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

    data_channel = calloc(1, sizeof(*data_channel));
    if (data_channel == NULL) {
        fprintf(stderr, "Failed to calloc data_channel.\n");
        return NULL;
    }

    data_channel->conn = -1;
    data_channel->dest_ip = strdup(dest_ip);
    data_channel->dest_port = port;
    data_channel->process_cb = init_data_channel;

    thread = event_thread_create("data_channel", data_channel_loop, data_channel_stop, data_channel);
    if (thread == NULL) {
        fprintf(stderr, "Failed to create data_channel thread.\n");
        free(data_channel);
        return NULL;
    }

    data_channel->thread = thread;
    return data_channel;
}

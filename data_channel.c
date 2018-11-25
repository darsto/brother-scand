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
#include <errno.h>
#include "data_channel.h"

#include "connection.h"
#include "event_thread.h"
#include "log.h"

#define DATA_CHANNEL_CHUNK_MAX_SIZE 0x10000
#define DATA_CHANNEL_CHUNK_HEADER_SIZE 0xC
#define DATA_CHANNEL_CHUNK_MAX_PROGRESS 0x1000
#define DATA_CHANNEL_LOCAL_PORT 49424
#define DATA_CHANNEL_TARGET_PORT 54921

struct data_channel {
    struct brother_conn *conn;
    int (*process_cb)(struct data_channel *data_channel);

    FILE *tempfile;

    struct data_channel_page_data {
        int id;
        int remaining_chunk_bytes;
    } page_data;

    unsigned scanned_pages;
    struct event_thread *thread;

    struct scan_param params[CONFIG_SCAN_MAX_PARAMS];
    uint8_t buf[2048];

    const struct device_config *config;
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

static int receive_initial_data(struct data_channel *data_channel);

static int
set_paused(struct data_channel *data_channel)
{
    event_thread_pause(data_channel->thread);
    sleep(1);
    return 0;
}

static void
data_channel_pause(struct data_channel *data_channel)
{
    LOG_DEBUG("%s: going to sleep.\n", data_channel->config->ip);
    data_channel->process_cb = set_paused;
}

static struct scan_param *
get_scan_param_by_index(struct data_channel *data_channel, uint8_t index)
{
    if (index >= CONFIG_SCAN_MAX_PARAMS) {
        return NULL;
    }

    return data_channel->params + index;
}

static struct scan_param *
get_scan_param_by_id(struct data_channel *data_channel, char id)
{
    struct scan_param *ret;
    uint8_t i = 0;

    do {
        ret = get_scan_param_by_index(data_channel, i++);
    } while (ret != NULL && ret->id != id);

    return ret;
}

static int
read_scan_params(struct data_channel *data_channel, uint8_t *buf, uint8_t *buf_end,
                 const char *whitelist)
{
    struct scan_param *param;
    char id;
    size_t i;

    while (buf < buf_end) {
        id = *buf++;
        if (*buf != '=') {
            LOG_ERR("Received invalid scan param (missing '=' sign).\n");
            return -1;
        }
        ++buf;

        param = get_scan_param_by_id(data_channel, id);
        if (param == NULL) {
            LOG_ERR("Received invalid scan param (unknown id '%c').\n", id);
            return -1;
        }

        i = 0;
        while (*buf != 0x0a) {
            if (whitelist == NULL || strchr(whitelist, id) != NULL) {
                param->value[i++] = *buf;
            }

            if (i >= sizeof(param->value) - 1) {
                LOG_ERR("Received data_channel param longer than %zu bytes!\n",
                        sizeof(param->value) - 1);
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
write_scan_params(struct data_channel *data_channel, uint8_t *buf,
                  const char *whitelist)
{
    struct scan_param *param;
    size_t len;
    uint8_t i = 0;

    while ((param = get_scan_param_by_index(data_channel, i++)) != NULL) {
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
process_page_end_header(struct data_channel *data_channel,
                        struct data_packet_header *header,
                        uint32_t payload_len)
{
    FILE *destfile;
    struct scan_param *param;
    char filename[64];
    size_t size;
    int i, rc;

    if (header->page_id != data_channel->page_data.id) {
        LOG_ERR("%s: packet page_id mismatch (got %u, expected %u)\n",
                data_channel->config->ip, header->page_id,
                data_channel->scanned_pages + 1);
        return -1;
    }

    sprintf(filename, "scan%u.jpg", data_channel->scanned_pages++);
    destfile = fopen(filename, "w");
    if (destfile == NULL) {
        LOG_ERR("Cannot create file '%s' on data_channel %s\n", filename,
                data_channel->config->ip);
        return -1;
    }

    fseek(data_channel->tempfile, 0, SEEK_SET);
    while ((size = fread(data_channel->buf, 1, sizeof(data_channel->buf),
                         data_channel->tempfile))) {
        fwrite(data_channel->buf, 1, size, destfile);
    }

    fclose(destfile);
    fclose(data_channel->tempfile);
    data_channel->tempfile = NULL;

    data_channel->process_cb = receive_initial_data;
    LOG_INFO("%s: successfully received page %u\n",
             data_channel->config->ip, header->page_id);

    param = get_scan_param_by_id(data_channel, 'F');
    for (i = 0; i < CONFIG_SCAN_MAX_FUNCS; ++i) {
        if (strcmp(param->value, g_scan_func_str[i]) == 0) {
            break;
        }
    }

    rc = snprintf((char *) data_channel->buf, sizeof(data_channel->buf), "%s %s %s",
                  data_channel->config->scan_funcs[i], data_channel->config->ip,
                  filename);
    if (rc < 0 || rc == sizeof(data_channel->buf)) {
        LOG_ERR("%s: couldn't execute user hook. snprintf failed: %d\n",
                data_channel->config->ip, errno);
        return -1;
    }

    system((char *) data_channel->buf);

    return 0;
}

static int
process_chunk_header(struct data_channel *data_channel,
                     struct data_packet_header *header,
                     uint32_t payload_len)
{
    int progress_percent;
    int total_chunk_size;

    if (payload_len < 2) {
        LOG_ERR("%s: payload too small (%u/2 bytes)\n",
                data_channel->config->ip, payload_len);
        return -1;
    }

    if (data_channel->page_data.id == 0) {
        LOG_INFO("%s: now scanning page id %u\n", data_channel->config->ip,
                 header->page_id);
        data_channel->page_data.id = header->page_id;
    } else if (header->page_id != data_channel->page_data.id) {
        LOG_ERR("%s: packet page_id mismatch (packet %u != local %u)\n",
                data_channel->config->ip, header->page_id, data_channel->page_data.id);
        return -1;
    }

    progress_percent = header->progress * 100 / DATA_CHANNEL_CHUNK_MAX_PROGRESS;
    LOG_DEBUG("%s: receiving data: %d%%\n", data_channel->config->ip,
              progress_percent);

    data_channel->page_data.remaining_chunk_bytes = header->payload[0] |
            (header->payload[1] << 8);
    total_chunk_size = data_channel->page_data.remaining_chunk_bytes +
                       DATA_CHANNEL_CHUNK_HEADER_SIZE;

    if (total_chunk_size > DATA_CHANNEL_CHUNK_MAX_SIZE) {
        LOG_ERR("%s: invalid chunk size\n", data_channel->config->ip);
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

    if (buf_len == 1) {
        LOG_ERR("%s: device unavailable (error code %u)\n",
                data_channel->config->ip, buf[0]);
        return -1;
    }

    if (buf_len < 10) {
        LOG_ERR("%s: invalid header length (%u/10+ bytes)\n",
                data_channel->config->ip, buf_len);
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
        LOG_ERR("%s: invalid header magic number (%u != 0x07)\n",
                data_channel->config->ip, header.magic);
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
        LOG_ERR("%s: received unsupported header (id = %u)\n",
                data_channel->config->ip, header.id);
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
            rc = process_data(data_channel, buf,
                              data_channel->page_data.remaining_chunk_bytes);
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
            LOG_ERR("%s: couldn't parse header\n",
                    data_channel->config->ip);
            return -1;
        }

        buf += DATA_CHANNEL_CHUNK_HEADER_SIZE;
        msg_len -= DATA_CHANNEL_CHUNK_HEADER_SIZE;
    }

    fwrite(buf, sizeof(*buf), (size_t) msg_len, data_channel->tempfile);
    data_channel->page_data.remaining_chunk_bytes -= msg_len;

    return 0;
}

static int
receive_data(struct data_channel *data_channel)
{
    int msg_len;
    int rc;

    if (data_channel->tempfile != NULL &&
        data_channel->page_data.remaining_chunk_bytes == 0) {
    }

    /* waiting for the sensor rail to return */
    rc = brother_conn_poll(data_channel->conn, data_channel->config->page_finish_timeout);
    if (rc <= 0) {
        LOG_ERR("Couldn't receive final data packet on data_channel %s\n",
                data_channel->config->ip);
        return -1;
    }

    msg_len = brother_conn_receive(data_channel->conn, data_channel->buf,
                                   sizeof(data_channel->buf));
    if (msg_len < 1) {
        LOG_ERR("Failed to receive data packet on data_channel %s\n",
                data_channel->config->ip);
        return -1;
    }

    rc = process_data(data_channel, data_channel->buf, msg_len);
    if (rc != 0) {
        LOG_ERR("Couldn't process data packet on data_channel %s\n",
                data_channel->config->ip);
        return -1;
    }

    return 0;
}

static void
data_channel_reset_page_data(struct data_channel *data_channel)
{
    memset(&data_channel->page_data, 0, sizeof(data_channel->page_data));
}

static int
receive_initial_data(struct data_channel *data_channel)
{
    int msg_len = 0, rc;

    rc = brother_conn_poll(data_channel->conn, data_channel->config->page_init_timeout);
    if (rc <= 0) {
        /* no more documents to scan */
        data_channel_pause(data_channel);
        return -1;
    }

    msg_len = brother_conn_receive(data_channel->conn, data_channel->buf,
                              sizeof(data_channel->buf));
    if (msg_len < 1) {
        LOG_ERR("Couldn't receive initial data packet on data_channel %s\n",
                data_channel->config->ip);
        return -1;
    }

    if (msg_len == 1 && data_channel->buf[0] == 0x80) {
        /* no more documents to scan */
        data_channel_pause(data_channel);
        return 0;
    }

    data_channel_reset_page_data(data_channel);
    data_channel->tempfile = tmpfile();
    if (data_channel->tempfile == NULL) {
        LOG_ERR("Cannot create temp file on data_channel %s\n",
                data_channel->config->ip);
        return -1;
    }

    rc = process_data(data_channel, data_channel->buf, msg_len);
    if (rc != 0) {
        LOG_ERR("Couldn't process initial data packet on data_channel %s\n",
                data_channel->config->ip);
        fclose(data_channel->tempfile);
        data_channel->tempfile = NULL;
        return -1;
    }

    data_channel->process_cb = receive_data;
    return 0;
}

static int
exchange_params2(struct data_channel *data_channel)
{
    struct scan_param *param;
    uint8_t *buf, *buf_end;
    long recv_params[7];
    int msg_len, rc;
    size_t i, len;
    long tmp;

    rc = brother_conn_poll(data_channel->conn, 3);
    if (rc <= 0) {
        LOG_ERR("Couldn't receive scan params on data_channel %s\n",
                data_channel->config->ip);
        return -1;
    }

    msg_len = brother_conn_receive(data_channel->conn, data_channel->buf,
                                  sizeof(data_channel->buf));
    if (msg_len < 5) {
        LOG_ERR("Failed to receive scan params on data_channel %s\n",
                data_channel->config->ip);
        return -1;
    }

    /* process received data */
    if (data_channel->buf[0] != 0x00) {
        LOG_ERR("%s: received invalid exchange params msg (invalid first byte '%c').\n",
                data_channel->config->ip, data_channel->buf[0]);
        return -1;
    }

    if (data_channel->buf[1] != msg_len - 3) {
        LOG_ERR("%s: invalid second exchange params msg (invalid length '%c').\n",
                data_channel->config->ip, data_channel->buf[1]);
        return -1;
    }

    if (data_channel->buf[2] != 0x00) {
        LOG_ERR("%s: received invalid exchange params msg (invalid third byte '%c').\n",
                data_channel->config->ip, data_channel->buf[2]);
        return -1;
    }

    if (data_channel->buf[msg_len - 1] != 0x00) {
        LOG_ERR("%s: received invalid exchange params msg (invalid last byte '%c').\n",
                data_channel->config->ip, data_channel->buf[msg_len - 1]);
        return -1;
    }

    i = 0;
    buf_end = buf = data_channel->buf + 3;

    while (i < sizeof(recv_params) / sizeof(recv_params[0])) {
        tmp = strtol((char *) buf, (char **) &buf_end, 10);
        if (buf_end == buf || *buf_end != ',' ||
            ((tmp == LONG_MIN || tmp == LONG_MAX) && errno == ERANGE)) {
            LOG_ERR("%s: received invalid exchange params msg (invalid params).\n",
                    data_channel->config->ip);
            return -1;
        }

        recv_params[i++] = tmp;
        buf = ++buf_end;
    }

    if (*buf != 0x00) {
        LOG_ERR("%s: received invalid exchange params msg (message too long).\n",
                data_channel->config->ip);
        return -1;
    }

    param = get_scan_param_by_id(data_channel, 'R');
    assert(param);

    /* previously sent and just received dpi should match */
    sprintf((char *) data_channel->buf, "%ld,%ld",
            recv_params[0], recv_params[1]);
    if (strncmp((char *)(data_channel->buf), param->value,
                sizeof(param->value)) != 0) {
        LOG_INFO("Scanner does not support requested dpi: %s."
                 " %s will be used instead\n",
                 param->value, (char *)(data_channel->buf));

        strncpy(param->value, (const char *) data_channel->buf,
                sizeof(param->value));
        param->value[sizeof(param->value)] = 0;
    }

    param = get_scan_param_by_id(data_channel, 'A');
    assert(param);
    sprintf(param->value, "0,0,%ld,%ld", recv_params[4], recv_params[6]);

    /* prepare a response */
    buf = data_channel->buf;
    *buf++ = 0x1b; // magic sequence
    *buf++ = 0x58; // packet id (?)
    *buf++ = 0x0a; // header end

    buf = write_scan_params(data_channel, buf, "RMCJBNADGL");
    if (buf == NULL) {
        LOG_ERR("Failed to write scan params on data_channel %s\n",
                data_channel->config->ip);
        return -1;
    }

    *buf++ = 0x80; // end of message

    msg_len = brother_conn_send(data_channel->conn, data_channel->buf, buf -
                           data_channel->buf);
    if (msg_len < 0 || (unsigned) msg_len != buf - data_channel->buf) {
        LOG_ERR("Couldn't send scan params on data_channel %s\n",
                data_channel->config->ip);
        return -1;
    }

    data_channel->process_cb = receive_initial_data;
    return 0;
}

static int
exchange_params1(struct data_channel *data_channel)
{
    struct scan_param *param;
    uint8_t *buf, *buf_end;
    int msg_len;
    size_t str_len;
    int i, rc;

    rc = brother_conn_poll(data_channel->conn, 2);
    if (rc <= 0) {
        LOG_ERR("%s: couldn't receive initial scan params\n",
                data_channel->config->ip);
        return -1;
    }

    msg_len = brother_conn_receive(data_channel->conn, data_channel->buf,
                              sizeof(data_channel->buf));
    if (msg_len < 5) {
        LOG_ERR("%s: failed to receive initial scan params\n",
                data_channel->config->ip);
        return -1;
    }

    /* process received data */
    if (data_channel->buf[0] != 0x30) {
        LOG_ERR("%s: received invalid initial exchange params msg"
                " (invalid first byte '%c').\n", data_channel->config->ip,
                data_channel->buf[0]);
        return -1;
    }
    //buf[1] == 0x15 or 0x55 (might refer to automatic/manual scan)
    //buf[2] == 0x30 or 0x00 ??

    if (data_channel->buf[msg_len - 2] != 0x0a) { //end of param
        LOG_ERR("%s: received invalid initial exchange params msg"
                " (invalid second-last byte '%c').\n",
                data_channel->config->ip, data_channel->buf[msg_len - 2]);
        return -1;
    }

    if (data_channel->buf[msg_len - 1] != 0x80) { //end of message
        LOG_ERR("%s: received invalid initial exchange params msg"
                " (invalid last byte '%c').\n",
                data_channel->config->ip, data_channel->buf[msg_len - 1]);
        return -1;
    }

    buf = data_channel->buf + 3;
    buf_end = data_channel->buf + msg_len - 2;

    if (read_scan_params(data_channel, buf, buf_end, NULL) != 0) {
        LOG_ERR("%s: failed to process initial scan params\n",
                data_channel->config->ip);
        return -1;
    }

    param = get_scan_param_by_id(data_channel, 'R');
    if (strchr(param->value, ',') == NULL) {
        str_len = strlen(param->value);
        if (str_len >= sizeof(param->value) - 1) {
            LOG_ERR("%s: received invalid resolution %s\n",
                    data_channel->config->ip, param->value);
            return -1;
        }

        param->value[str_len] = ',';
        memcpy(&param->value[str_len + 1], param->value, str_len);
    }

    param = get_scan_param_by_id(data_channel, 'F');
    for (i = 0; i < CONFIG_SCAN_MAX_FUNCS; ++i) {
        if (strcmp(param->value, g_scan_func_str[i]) == 0) {
            break;
        }
    }

    if (i == CONFIG_SCAN_MAX_FUNCS) {
        LOG_ERR("%s: received invalid scan function %s.\n",
                data_channel->config->ip, param->value);
        return -1;
    }

    /* prepare a response */
    buf = data_channel->buf;
    *buf++ = 0x1b; // magic sequence
    *buf++ = 0x49; // packet id (?)
    *buf++ = 0x0a; // header end

    buf = write_scan_params(data_channel, buf, "RMD");
    if (buf == NULL) {
        LOG_ERR("Failed to write initial scan params on data_channel %s\n",
                data_channel->config->ip);
        return -1;
    }

    *buf++ = 0x80; // end of message

    msg_len = brother_conn_send(data_channel->conn, data_channel->buf, buf -
                           data_channel->buf);
    if (msg_len < 0) {
        LOG_ERR("Couldn't send initial scan params on data_channel %s\n",
                data_channel->config->ip);
        return -1;
    }

    data_channel->process_cb = exchange_params2;
    return 0;
}

static int
init_connection(struct data_channel *data_channel)
{
    int rc, msg_len;

    if (brother_conn_reconnect(data_channel->conn, inet_addr(data_channel->config->ip),
                          htons(DATA_CHANNEL_TARGET_PORT)) != 0) {
        LOG_ERR("Could not connect to scanner.\n");
        return -1;
    }

    rc = brother_conn_poll(data_channel->conn, 3);
    if (rc <= 0) {
        LOG_ERR("Couldn't receive welcome message on data_channel %s\n",
                data_channel->config->ip);
        return -1;
    }

    msg_len = brother_conn_receive(data_channel->conn, data_channel->buf,
                              sizeof(data_channel->buf));
    if (msg_len < 1) {
        LOG_ERR("Failed to receive welcome message on data_channel %s\n",
                data_channel->config->ip);
        return -1;
    }

    if (data_channel->buf[0] != '+') {
        LOG_ERR("Received invalid welcome message on data_channel %s\n",
                data_channel->config->ip);
        return -1;
    }

    msg_len = brother_conn_send(data_channel->conn, "\x1b\x4b\x0a\x80", 4);
    if (msg_len < 0) {
        LOG_ERR("Couldn't send welcome message on data_channel %s\n",
                data_channel->config->ip);
        return -1;
    }

    data_channel->process_cb = exchange_params1;
    return 0;
}

static void
data_channel_loop(void *arg)
{
    struct data_channel *data_channel = arg;
    int rc;

    rc = data_channel->process_cb(data_channel);
    if (rc != 0) {
        LOG_ERR("%s: failed to process data. The channel will be closed.\n",
                data_channel->config->ip);

        if (data_channel->tempfile) {
            fclose(data_channel->tempfile);
            data_channel->tempfile = NULL;
        }

        data_channel_pause(data_channel);
    }
}

static void
data_channel_stop(void *arg)
{
    struct data_channel *data_channel = arg;

    if (data_channel->tempfile) {
        fclose(data_channel->tempfile);
        data_channel->tempfile = NULL;
    }

    brother_conn_close(data_channel->conn);
    free(data_channel);
}

static int
init_data_channel(struct data_channel *data_channel)
{
    data_channel->thread = event_thread_self();
    data_channel->process_cb = set_paused;

    data_channel->conn = brother_conn_open(BROTHER_CONNECTION_TYPE_TCP, data_channel->config->timeout);
    if (data_channel->conn == NULL) {
        LOG_ERR("Failed to init a data_channel.\n");
        event_thread_stop(data_channel->thread);
        return 0;
    }

    if (brother_conn_bind(data_channel->conn, htons(DATA_CHANNEL_LOCAL_PORT)) != 0) {
        LOG_ERR("Failed to bind a data_channel to port %d.\n",
                DATA_CHANNEL_LOCAL_PORT);
        event_thread_stop(data_channel->thread);
        return 0;
    }

    memcpy(data_channel->params, data_channel->config->scan_params,
           sizeof(data_channel->config->scan_params));

    return 0;
}

void
data_channel_kick_cb(void *arg1, void *arg2)
{
    struct data_channel *data_channel = arg1;

    if (data_channel->process_cb != set_paused) {
        LOG_ERR("Trying to kick non-sleeping data_channel %s.\n",
                data_channel->config->ip);
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
    LOG_ERR("Failed to kick data_channel %s.\n",
            data_channel->config->ip);
}

struct data_channel *
data_channel_create(struct device_config *config)
{
    struct data_channel *data_channel;
    struct event_thread *thread;

    data_channel = calloc(1, sizeof(*data_channel));
    if (data_channel == NULL) {
        LOG_ERR("Failed to calloc data_channel.\n");
        return NULL;
    }

    data_channel->config = config;
    data_channel->process_cb = init_data_channel;

    thread = event_thread_create("data_channel", data_channel_loop,
                                 data_channel_stop, data_channel);
    if (thread == NULL) {
        LOG_ERR("Failed to create data_channel thread.\n");
        free(data_channel);
        return NULL;
    }

    return data_channel;
}

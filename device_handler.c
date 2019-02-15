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
#include <time.h>
#include <memory.h>
#include <stdatomic.h>
#include "device_handler.h"
#include "event_thread.h"
#include "config.h"
#include "connection.h"
#include "data_channel.h"
#include "snmp.h"
#include "log.h"

#define DEVICE_REGISTER_DURATION_SEC 360
#define DEVICE_KEEPALIVE_DURATION_SEC 5
#define BUTTON_HANDLER_PORT 54925

struct device {
    in_addr_t ip;
    struct data_channel *channel;
    int status;
    char local_ip[16];
    time_t next_ping_time;
    time_t next_register_time;
    const struct device_config *config;
    TAILQ_ENTRY(device) tailq;
};

struct device_handler {
    struct brother_conn *button_conn;
    struct event_thread *thread;
    struct brother_poll_group *devices_poll_group;
    TAILQ_HEAD(, device) devices;
};

#define BUTTON_HANDLER_NETWORK_TIMEOUT 3

static atomic_int g_appnum;
static struct device_handler g_dev_handler;

static char
digit_to_hex(int n)
{
    const char *trans_table = "0123456789ABCDEF";

    return trans_table[n & 0xf];
}

static int
encode_password(const char *pass, char *buf)
{
    const uint8_t g_pass_shuffle_table[] = {
        0x05, 0x0A, 0x1F, 0x18, 0x08, 0x1E, 0x1C, 0x01,
        0x11, 0x0D, 0x0C, 0x0E, 0x1B, 0x03, 0x15, 0x16,
        0x1D, 0x14, 0x00, 0x07, 0x10, 0x0B, 0x19, 0x04,
        0x13, 0x12, 0x06, 0x1A, 0x09, 0x02, 0x0F, 0x17
    };
    const uint8_t g_pass_key[] = { 0xCA, 0xFE, 0x28, 0xA9 };
    uint8_t tmp_buf[32] = {0};
    char tmp;
    int i, j;

    for (i = 0; i < 4; ++i) {
        tmp = pass[i];
        for (j = 0; j < 8; ++j) {
            tmp_buf[g_pass_shuffle_table[8 * i + j] >> 3] |=
                ((tmp & 1) << (g_pass_shuffle_table[8 * i + j] & 7));
            tmp >>= 1;
        }
    }

    for (i = 0; i < 4; ++i) {
        tmp_buf[i] ^= g_pass_key[i];
    }

    for (i = 0; i < 4; ++i) {
        *buf++ = digit_to_hex((unsigned char) tmp_buf[i] >> 4);
        *buf++ = digit_to_hex(tmp_buf[i] & 0xF);
    }

    *buf = 0;

    return 0;
}

static int
register_scanner_driver(struct device *dev, char local_ip[16], bool enabled)
{
  const char *functions[CONFIG_SCAN_DEVICE_FUNCS] = {0};
  char msg[CONFIG_SCAN_DEVICE_FUNCS][256];
  char pass_buf[9] = {0};
  uint8_t buf[1024];
  int num_funcs = 0, i, rc;
  struct item_config *item;

  TAILQ_FOREACH(item, &dev->config->items, tailq) {
    if (item->password != NULL && strlen(item->password) == 4) {
      encode_password(item->password, pass_buf);
    }

    rc = snprintf(msg[num_funcs], sizeof(msg),
                  "TYPE=BR;"
                  "BUTTON=SCAN;"
                  "USER=\"%s\";"
                  "FUNC=%s;"
                  "HOST=%s:%d;"
                  "APPNUM=%d;"
                  "DURATION=%d;"
                  "BRID=%s;"
                  "CC=1;",
                  item->hostname, g_scan_func_str[item->scan_func], local_ip,
                  BUTTON_HANDLER_PORT, atomic_fetch_add(&g_appnum, 1),
                  DEVICE_REGISTER_DURATION_SEC, pass_buf);

    if (rc < 0 || rc == 255) {
      return -1;
    }

    functions[num_funcs] = msg[num_funcs];
    ++num_funcs;
    if (num_funcs == CONFIG_SCAN_DEVICE_FUNCS) {
      LOG_ERR("Too many functions defined for device %s", local_ip);
      return -1;
    }
  }

  return snmp_register_scanner_driver(g_dev_handler.button_conn, enabled, buf,
                                      sizeof(buf), functions, dev->ip);
}

struct device *
device_handler_add_device(struct device_config *config)
{
    struct device *dev;
    struct brother_conn *conn;
    uint8_t buf[1024];
    int status, rc, i;
    char local_ip[16];

    conn = brother_conn_open(BROTHER_CONNECTION_TYPE_UDP,
                           BUTTON_HANDLER_NETWORK_TIMEOUT);
    if (conn == NULL) {
        LOG_ERR("Cannot open an UDP socket for device %s.\n", config->ip);
        return NULL;
    }

    rc = brother_conn_reconnect(conn, inet_addr(config->ip), htons(161));
    if (rc != 0) {
        LOG_ERR("Can't connect to %s:161.\n", config->ip);
        brother_conn_close(conn);
        return NULL;
    }

    rc = brother_conn_get_local_ip(conn, local_ip);
    brother_conn_close(conn);
    if (rc != 0) {
        LOG_ERR("Can't get the local ip address that connected to %s:161.\n",
                config->ip);
        return NULL;

    }

    status = snmp_get_printer_status(g_dev_handler.button_conn, buf,
                                     sizeof(buf), inet_addr(config->ip));

    if (status != 10001) {
        LOG_ERR("Error: device at %s is unreachable.\n", config->ip);
        return NULL;
    }

    dev = calloc(1, sizeof(*dev));
    if (dev == NULL) {
        LOG_ERR("Could not calloc memory for device at %s.\n", config->ip);
        return NULL;
    }

    dev->ip = inet_addr(config->ip);
    snprintf(dev->local_ip, sizeof(dev->local_ip), "%s", local_ip);
    dev->config = config;
    dev->channel = data_channel_create(config);
    if (dev->channel == NULL) {
        LOG_ERR("Failed to create data_channel for device %s.\n", config->ip);
        free(dev);
        return NULL;
    }

    TAILQ_INSERT_TAIL(&g_dev_handler.devices, dev, tailq);
    return dev;
}

static char *device_handler_extract_string(const char *buf, char *before,
                                           char *after) {
  char *user = strstr(buf, before);
  if (!user) {
    return NULL;
  }
  user += 6;
  char *endDelim = strstr(user, after);
  if (!endDelim) {
    return NULL;
  }
  return strndup(user, endDelim - user);
}

static char *device_handler_extract_hostname(const char *buf) {
  return device_handler_extract_string(buf, "USER=\"", "\"");
}

static enum scan_func device_handler_extract_func(const char *buf) {
  char *scan_func_name = device_handler_extract_string(buf, "FUNC=", ";");
  enum scan_func scan_func = config_get_scan_func_by_name(scan_func_name);
  free(scan_func_name);
  return scan_func;
}

static void
device_handler_loop(void *arg)
{
    struct device *dev;
    time_t time_now;
    char client_ip[16];
    uint8_t buf[1024];
    int msg_len, rc;

    TAILQ_FOREACH(dev, &g_dev_handler.devices, tailq) {
        time_now = time(NULL);

        if (difftime(time_now, dev->next_ping_time) > 0) {
            /* only ping once per DEVICE_KEEPALIVE_DURATION_SEC */
            dev->next_ping_time = time_now + DEVICE_KEEPALIVE_DURATION_SEC;
            dev->status = snmp_get_printer_status(g_dev_handler.button_conn,
                                                  buf, sizeof(buf), dev->ip);
            if (dev->status != 10001) {
                LOG_WARN("Warn: device at %s is currently unreachable.\n",
                         dev->config->ip);
            }
        }

        if (dev->status != 10001) {
            continue;
        }

        if (difftime(time_now, dev->next_register_time) > 0) {
            /* only register once per DEVICE_REGISTER_DURATION_SEC */
            dev->next_register_time = time_now + DEVICE_REGISTER_DURATION_SEC;
            register_scanner_driver(dev, dev->local_ip, true);
        }
    }

    rc = brother_conn_poll(g_dev_handler.button_conn, 1);
    if (rc <= 0) {
        return;
    }

    /* try to receive scan event */
    msg_len = brother_conn_receive(g_dev_handler.button_conn, buf, sizeof(buf));
    if (msg_len < 0) {
        return;
    }

    rc = brother_conn_get_client_ip(g_dev_handler.button_conn, client_ip);
    if (rc < 0) {
        LOG_ERR("Invalid client IP. (IPv6 not supported yet)\n");
        return;
    }

    char *hostname = device_handler_extract_hostname((char *)(buf + 4));
    enum scan_func scan_func = device_handler_extract_func((char *)(buf + 4));
    if (scan_func < 0) {
      return;
    }

    // Find the device and associated item_config based on ip, scan_func and
    // hostname.
    TAILQ_FOREACH(dev, &g_dev_handler.devices, tailq) {
        if (strncmp(dev->config->ip, client_ip, 16) == 0) {
          const struct item_config *item =
              config_find_by_func_and_name(dev->config, scan_func, hostname);
          if (item != NULL) {
            msg_len =
                brother_conn_send(g_dev_handler.button_conn, buf, msg_len);
            if (msg_len < 0) {
              perror("sendto");
              goto out;
            }
            data_channel_set_item(dev->channel, item);
            data_channel_kick(dev->channel);
            goto out;
          }
        }
    }

    LOG_WARN("Received scan button event from unknown device %s hostname:%s.\n",
             client_ip, hostname);
out:
  free(hostname);
}

static void
device_handler_stop(void *arg)
{
    struct device *dev;
    char ip[16];
    uint8_t buf[1024];

    while ((dev = TAILQ_FIRST(&g_dev_handler.devices))) {
        TAILQ_REMOVE(&g_dev_handler.devices, dev, tailq);
        snmp_get_printer_status(g_dev_handler.button_conn, buf, sizeof(buf),
                                dev->ip);
        brother_conn_get_local_ip(g_dev_handler.button_conn, ip);
        register_scanner_driver(dev, ip, false);
        free(dev);
    }
}

void device_handler_init(void) {
  struct device_config *dev_config;

  atomic_store(&g_appnum, 1);
  TAILQ_INIT(&g_dev_handler.devices);

  g_dev_handler.button_conn = brother_conn_open(BROTHER_CONNECTION_TYPE_UDP,
                                                BUTTON_HANDLER_NETWORK_TIMEOUT);
  if (g_dev_handler.button_conn == NULL) {
    LOG_FATAL("Failed to open a socket for the button handler.\n");
    return;
  }

  if (brother_conn_bind(g_dev_handler.button_conn,
                        htons(BUTTON_HANDLER_PORT)) != 0) {
    LOG_FATAL("Could not bind to the button handler port %d.\n",
              BUTTON_HANDLER_PORT);
    brother_conn_close(g_dev_handler.button_conn);
    return;
  }

  TAILQ_FOREACH(dev_config, &g_config.devices, tailq) {
    if (device_handler_add_device(dev_config) == NULL) {
      fprintf(stderr, "Error: could not load device '%s'.\n", dev_config->ip);
      return;
    }
  }

  g_dev_handler.thread = event_thread_create(
      "device_handler", device_handler_loop, device_handler_stop, NULL);
  if (g_dev_handler.thread == NULL) {
    LOG_FATAL("Could not init device_handler thread.\n");
    brother_conn_close(g_dev_handler.button_conn);
    return;
  }
}

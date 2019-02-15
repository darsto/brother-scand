/*
 * Copyright (c) 2017 Dariusz Stojaczyk. All Rights Reserved.
 * The following source code is released under an MIT-style license,
 * that can be found in the LICENSE file.
 */

#ifndef BROTHER_DATA_CHANNEL_H
#define BROTHER_DATA_CHANNEL_H

#include <stdio.h>

#include "config.h"

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

  int xdpi;
  int ydpi;
  int width;
  int height;

  const struct device_config *config;
  // The current hostname/scan_func item that's being processed.
  const struct item_config *item;
  // The format of the current file: one of "raw", "rle", "jpeg"
  const char *file_format;
};

int data_channel_set_paused(struct data_channel *data_channel);
struct data_channel *data_channel_create(struct device_config *config);
int data_channel_init_connection(struct data_channel *data_channel);
int data_channel_init(struct data_channel *data_channel);
void data_channel_kick(struct data_channel *data_channel);

// arg is a struct data_channel
void data_channel_loop(void *data_channel);
void data_channel_set_item(struct data_channel *data_channel,
                           const struct item_config *item);

#endif //BROTHER_DATA_CHANNEL_H

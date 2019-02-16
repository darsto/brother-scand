/*
 * Copyright (c) 2019 Hagen Fritsch. All Rights Reserved.
 * The following source code is released under an MIT-style license,
 * that can be found in the LICENSE file.
 */

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <getopt.h>
#include <string.h>

#include "device_handler.h"
#include "event_thread.h"
#include "log.h"
#include "data_channel.h"

static void print_usage(char **argv) {
  printf(
      "Usage: %s [-c path/to/config/file]\n"
      "   -h print this help\n"
      "   -d debug output\n",
      argv[0]);
}

static void print_version(void) {
  printf("Brother scanner cli. Build " __DATE__ " " __TIME__ "\n");
}

int main(int argc, char *argv[]) {
  int option = 0;
  const char *config_path = "brother.config";

  while ((option = getopt(argc, argv, "c:dh")) != -1) {
    switch (option) {
      case 'c':
        config_path = optarg;
        break;
      case 'd':
        log_set_level(LEVEL_DEBUG);
        break;
      case 'h':
        print_version();
        exit(EXIT_SUCCESS);
      default:
        print_usage(argv);
        exit(EXIT_FAILURE);
    }
  }

  if (config_init(config_path) != 0) {
    fprintf(stderr, "Fatal: could not init config.\n");
    return -1;
  }

  struct data_channel data_channel;
  bzero(&data_channel, sizeof(data_channel));
  data_channel.config = TAILQ_FIRST(&g_config.devices);
  // TODO: choose preset by name and func
  data_channel_set_item(&data_channel,
                        TAILQ_FIRST(&data_channel.config->items));
  data_channel_init(&data_channel);
  data_channel_init_connection(&data_channel);
  while (data_channel.process_cb != data_channel_set_paused) {
    data_channel_loop(&data_channel);
  }
}

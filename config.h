/*
 * Copyright (c) 2017 Dariusz Stojaczyk. All Rights Reserved.
 * The following source code is released under an MIT-style license,
 * that can be found in the LICENSE file.
 */

#ifndef BROTHER_CONFIG_H
#define BROTHER_CONFIG_H

#include <sys/queue.h>

struct device {
    uint8_t buf[1024];
    int conn;
    int button_conn;
    struct data_channel *channel;
    const char *ip;
    int status;
    time_t next_ping_time;
    time_t next_register_time;
    TAILQ_ENTRY(device) tailq;
};

struct brother_config {
    char local_ip[16];
    TAILQ_HEAD(, device) devices;
};

struct brother_config g_config;

int config_init(const char *config_path);

#endif //BROTHER_CONFIG_H

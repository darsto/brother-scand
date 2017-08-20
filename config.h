/*
 * Copyright (c) 2017 Dariusz Stojaczyk. All Rights Reserved.
 * The following source code is released under an MIT-style license,
 * that can be found in the LICENSE file.
 */

#ifndef BROTHER_CONFIG_H
#define BROTHER_CONFIG_H

#include <sys/queue.h>

#define CONFIG_HOSTNAME_LENGTH 16
#define CONFIG_NETWORK_DEFAULT_TIMEOUT_SEC 3

struct device_config {
    const char *ip;
    unsigned timeout;
    TAILQ_ENTRY(device_config) tailq;
};

struct brother_config {
    char hostname[CONFIG_HOSTNAME_LENGTH];
    char local_ip[16];
    TAILQ_HEAD(, device_config) devices;
};

struct brother_config g_config;

int config_init(const char *config_path);

#endif //BROTHER_CONFIG_H

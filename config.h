/*
 * Copyright (c) 2017 Dariusz Stojaczyk. All Rights Reserved.
 * The following source code is released under an MIT-style license,
 * that can be found in the LICENSE file.
 */

#ifndef BROTHER_CONFIG_H
#define BROTHER_CONFIG_H

#include <sys/queue.h>

#define CONFIG_HOSTNAME_LENGTH 16
#define CONFIG_MAX_SCAN_PARAMS 16
#define CONFIG_NETWORK_DEFAULT_TIMEOUT_SEC 3
#define CONFIG_NETWORK_DEFAULT_PAGE_INIT_RETRIES 5
#define CONFIG_NETWORK_DEFAULT_PAGE_FINISH_RETRIES 20

struct scan_param {
    char id;
    char value[16];
};

struct device_config {
    const char *ip;
    unsigned timeout;
    unsigned page_init_retries;
    unsigned page_finish_retries;
    struct scan_param scan_params[CONFIG_MAX_SCAN_PARAMS];
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

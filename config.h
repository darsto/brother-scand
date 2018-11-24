/*
 * Copyright (c) 2017 Dariusz Stojaczyk. All Rights Reserved.
 * The following source code is released under an MIT-style license,
 * that can be found in the LICENSE file.
 */

#ifndef BROTHER_CONFIG_H
#define BROTHER_CONFIG_H

#include <sys/queue.h>

#define CONFIG_HOSTNAME_LENGTH 16
#define CONFIG_SCAN_MAX_PARAMS 16
#define CONFIG_SCAN_MAX_FUNCS 4
#define CONFIG_SCAN_FUNC_IMAGE 0
#define CONFIG_SCAN_FUNC_OCR 1
#define CONFIG_SCAN_FUNC_EMAIL 2
#define CONFIG_SCAN_FUNC_FILE 3
#define CONFIG_NETWORK_DEFAULT_TIMEOUT_SEC 3
#define CONFIG_NETWORK_DEFAULT_PAGE_INIT_RETRIES 5
#define CONFIG_NETWORK_DEFAULT_PAGE_FINISH_RETRIES 20

struct scan_param {
    char id;
    char value[16];
};

struct device_config {
    char *ip;
    char *password;
    unsigned timeout;
    unsigned page_init_retries;
    unsigned page_finish_retries;
    struct scan_param scan_params[CONFIG_SCAN_MAX_PARAMS];
    char *scan_funcs[CONFIG_SCAN_MAX_FUNCS];
    TAILQ_ENTRY(device_config) tailq;
};

struct brother_config {
    char hostname[CONFIG_HOSTNAME_LENGTH];
    TAILQ_HEAD(, device_config) devices;
};

struct brother_config g_config;
extern const char *g_scan_func_str[CONFIG_SCAN_MAX_FUNCS];

int config_init(const char *config_path);

#endif //BROTHER_CONFIG_H

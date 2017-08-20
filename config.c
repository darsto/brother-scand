/*
 * Copyright (c) 2017 Dariusz Stojaczyk. All Rights Reserved.
 * The following source code is released under an MIT-style license,
 * that can be found in the LICENSE file.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "config.h"

static int
load_local_ip(void)
{
    struct sockaddr_in serv;
    int rc = -1;

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket");
        return -1;
    }

    memset(&serv, 0, sizeof(serv));
    serv.sin_family = AF_INET;
    serv.sin_addr.s_addr = inet_addr("8.8.8.8"); // google dns server
    serv.sin_port = htons(53);

    if (connect(sock, (const struct sockaddr *) &serv, sizeof(serv)) != 0) {
        perror("connect");
        goto out;
    }

    struct sockaddr_in name;
    socklen_t namelen = sizeof(name);
    if (getsockname(sock, (struct sockaddr *) &name, &namelen) != 0) {
        perror("getsockname");
        goto out;
    }

    const char *ret = inet_ntop(AF_INET, &name.sin_addr, g_config.local_ip, sizeof(g_config.local_ip));

    if (ret) {
        rc = 0;
    } else {
        perror("inet_ntop");
    }

out:
    close(sock);
    return rc;
}

static void
init_default_device_config(struct device_config *dev_config)
{
    struct scan_param *param;
    int i = 0;

    dev_config->timeout = CONFIG_NETWORK_DEFAULT_TIMEOUT_SEC;
    dev_config->page_init_retries = CONFIG_NETWORK_DEFAULT_PAGE_INIT_RETRIES;
    dev_config->page_finish_retries = CONFIG_NETWORK_DEFAULT_PAGE_FINISH_RETRIES;

#define ADD_SCAN_PARAM(ID, VAL) \
    param = &dev_config->scan_params[i++]; \
    param->id = ID; \
    strcpy(param->value, VAL);

    ADD_SCAN_PARAM('A', "");
    ADD_SCAN_PARAM('B', "50");
    ADD_SCAN_PARAM('C', "JPEG");
    ADD_SCAN_PARAM('D', "SIN");
    ADD_SCAN_PARAM('E', "");
    ADD_SCAN_PARAM('F', "");
    ADD_SCAN_PARAM('G', "1");
    ADD_SCAN_PARAM('J', "");
    ADD_SCAN_PARAM('L', "128");
    ADD_SCAN_PARAM('M', "CGRAY");
    ADD_SCAN_PARAM('N', "50");
    ADD_SCAN_PARAM('P', "A4");
    ADD_SCAN_PARAM('R', "300,300");
    ADD_SCAN_PARAM('T', "JPEG");

#undef ADD_SCAN_PARAM
}

int
config_init(const char *config_path)
{
    FILE* config;
    struct device_config *dev_config = NULL;
    char buf[1024];
    char var_str[256];
    char var_char;
    unsigned var_uint;
    int rc = -1, param_count = 0, i;

    TAILQ_INIT(&g_config.devices);
    if (load_local_ip() != 0) {
        fprintf(stderr, "Fatal: could not get local ip address.\n");
        return -1;
    }

    strcpy(g_config.hostname, "brother-open");

    config = fopen(config_path, "r");
    if (config == NULL) {
        fprintf(stderr, "Could not open config file '%s'.\n", config_path);
        abort();
    }

    while (fgets((char *) buf, sizeof(buf), config)) {
        if (sscanf((char *) buf, "hostname %15s", var_str) == 1) {
            memcpy(g_config.hostname, var_str, sizeof(g_config.hostname));
        } else if (sscanf((char *) buf, "ip %64s", var_str) == 1) {
            dev_config = calloc(1, sizeof(*dev_config));
            if (dev_config == NULL) {
                fprintf(stderr, "Error: could not alloc memory for device config '%s'.\n", var_str);
                goto out;
            }

            init_default_device_config(dev_config);
            dev_config->ip = strdup(var_str);
            TAILQ_INSERT_TAIL(&g_config.devices, dev_config, tailq);
        } else if (sscanf((char *) buf, "network.timeout %u", &var_uint) == 1) {
            if (dev_config == NULL) {
                fprintf(stderr, "Error: timeout specified without a device.\n");
                goto out;
            }

            dev_config->timeout = var_uint;
        } else if (sscanf((char *) buf, "network.page.init.retry %u", &var_uint) == 1) {
            if (dev_config == NULL) {
                fprintf(stderr, "Error: network.page.init.retry specified without a device.\n");
                goto out;
            }

            dev_config->page_init_retries = var_uint;
        } else if (sscanf((char *) buf, "network.page.finish.retry %u", &var_uint) == 1) {
            if (dev_config == NULL) {
                fprintf(stderr, "Error: network.page.finish.retry specified without a device.\n");
                goto out;
            }

            dev_config->page_finish_retries = var_uint;
        } else if (sscanf((char *) buf, "scan.param %c %15s", &var_char, var_str) == 2) {
            if (dev_config == NULL) {
                fprintf(stderr, "Error: scan.param specified without a device.\n");
                goto out;
            }

            if (param_count >= CONFIG_MAX_SCAN_PARAMS) {
                fprintf(stderr, "Error: too many scan.params. Max %d.\n",
                        CONFIG_MAX_SCAN_PARAMS);
                goto out;
            }

            for (i = 0; i < CONFIG_MAX_SCAN_PARAMS; ++i) {
                if (dev_config->scan_params[i].id == var_char) {
                    strcpy(dev_config->scan_params[i].value, var_str);
                    break;
                }
            }

            if (i == CONFIG_MAX_SCAN_PARAMS) {
                fprintf(stderr, "Error: invalid scan.param type '%c'.\n", var_char);
                goto out;
            }

            ++param_count;
        }
    }

    rc = 0;
out:
    fclose(config);
    return rc;
}

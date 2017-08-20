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

int
config_init(const char *config_path)
{
    FILE* config;
    struct device_config *dev_config = NULL;
    char buf[1024];
    char var_str[256];
    unsigned var_uint;
    int rc = -1;

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

            dev_config->ip = strdup(var_str);
            dev_config->timeout = CONFIG_NETWORK_DEFAULT_TIMEOUT_SEC;
            TAILQ_INSERT_TAIL(&g_config.devices, dev_config, tailq);
        } else if (sscanf((char *) buf, "network.timeout %u", &var_uint) == 1) {
            if (dev_config == NULL) {
                fprintf(stderr, "Error: timeout specified without a device.\n");
                goto out;
            }

            dev_config->timeout = var_uint;
        }
    }

    rc = 0;
out:
    fclose(config);
    return rc;
}

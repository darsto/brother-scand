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
#include "device_handler.h"

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
    struct device *dev;
    char buf[1024];
    char variable[256];
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
        if (sscanf((char *) buf, "hostname %15s", variable) == 1) {
            memcpy(g_config.hostname, variable, sizeof(g_config.hostname));
        } else if (sscanf((char *) buf, "ip %64s", variable) == 1) {
            dev = device_handler_add_device(variable);
            if (dev == NULL) {
                fprintf(stderr, "Error: could not load device '%s'.\n", variable);
            }
        }
    }

    rc = 0;
out:
    fclose(config);
    return rc;
}

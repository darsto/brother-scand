/*
 * Copyright (c) 2017 Dariusz Stojaczyk. All Rights Reserved.
 * The following source code is released under an MIT-style license,
 * that can be found in the LICENSE file.
 */

#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include "device_handler.h"
#include "log.h"
#include "event_thread.h"
#include "iputils.h"

struct scanner_data_t {
    char my_ip[16]; //just IPv4 for now
    char dest_ip[16];
    char name[16];
    char options_num;
    struct scanner_data_option_t {
        char func[8];
        char appnum;
    } options[8];
};

static int g_sockfd;
static struct scanner_data_t g_scanner_data = {
    .dest_ip = "10.0.0.149",
    .name = "open-source-bro",
    .options_num = 3,
    .options = {
        {.func = "IMAGE", .appnum = 1},
        {.func = "EMAIL", .appnum = 2},
        {.func = "FILE", .appnum = 5}
    }
};

static void
device_handler_dev_register_cb(void *arg1, void *arg2)
{
    unsigned char buf[2048];
    ssize_t sent_len;
    ssize_t recv_len;
    socklen_t slen = sizeof(struct sockaddr_in);
    struct sockaddr_in server;
    struct scanner_data_t *scanner_data = &g_scanner_data;

    server.sin_addr.s_addr = inet_addr(scanner_data->dest_ip);
    server.sin_family = AF_INET;
    server.sin_port = htons(161);

    recv_len = 0; //TODO

    if (recv_len <= 0) {
        fprintf(stderr, "Could not construct welcome message.\n");
        return;
    }

    printf("Sending msg to %s:\n", scanner_data->dest_ip);
    hexdump("payload", buf, (size_t) recv_len);
    sent_len = sendto(g_sockfd, buf, recv_len, 0, (struct sockaddr *) &server, slen);
    if (sent_len < 0) {
        perror("sendto");
        return;
    }

    printf("Message sent. (%zd/%zd). Waiting for the reply...\n", recv_len, sent_len);

    recv_len = recvfrom(g_sockfd, buf, 2048, 0, (struct sockaddr *) &server, &slen);
    if (recv_len < 0) {
        perror("recvfrom");
        return;
    }

    printf("Received reply from %s:\n", scanner_data->dest_ip);
    hexdump("payload", buf, (size_t) recv_len);
}

static void
device_handler_update_cb(void *arg1, void *arg2)
{
}

void 
device_handler_init()
{
    struct sockaddr_in sin;
    
    g_sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (g_sockfd == -1) {
        perror("socket");
        return;
    }

    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = htonl(INADDR_ANY);
    sin.sin_port = 49976;

    if (bind(g_sockfd, (struct sockaddr *) &sin, sizeof(sin)) == -1) {
        perror("bind");
        fprintf(stderr, "Could not start device handler.\n");
        goto socket_err;
    }
    
    iputils_get_local_ip(g_scanner_data.my_ip);
    
    event_thread_create("device_handler", device_handler_update_cb, NULL, NULL);
    return;
    
socket_err:
    close(g_sockfd);
}
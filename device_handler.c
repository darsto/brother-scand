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
#include "event_thread.h"
#include "iputils.h"
#include "ber/snmp.h"
#include "log.h"

static int g_sockfd;
static char g_local_ip[16];
static uint8_t g_buf[1024];
static uint8_t *const g_buf_end = g_buf + sizeof(g_buf) - 1;

static uint32_t brInfoPrinterUStatusOID[] = { 1, 3, 6, 1, 4, 1, 2435, 2, 3, 9, 4, 2, 1, 5, 5, 6, 0, SNMP_MSG_OID_END };

static void
device_handler_loop(void *arg1, void *arg2)
{
    uint8_t *out;

    struct snmp_msg_header msg_header = {};
    struct snmp_varbind varbind = {};

    struct sockaddr_in sin_serv;
    ssize_t recv_len;
    socklen_t slen = sizeof(sin_serv);
    
    msg_header.snmp_ver = 0;
    msg_header.community = "public";
    msg_header.pdu_type = SNMP_DATA_T_PDU_GET_REQUEST;
    msg_header.request_id = 314;

    varbind.value_type = SNMP_DATA_T_NULL;
    varbind.oid = brInfoPrinterUStatusOID;

    out = snmp_encode_msg(g_buf_end, &msg_header, 1, &varbind);
    recv_len = g_buf_end - out + 1;

    sin_serv.sin_addr.s_addr = inet_addr("10.0.0.149");
    sin_serv.sin_family = AF_INET;
    sin_serv.sin_port = htons(161);
    
    hexdump("sending", out, recv_len);
    if (sendto(g_sockfd, out, recv_len, 0, (struct sockaddr *) &sin_serv, slen) == -1) {
        perror("sendto");
        goto out;
    }
    printf("...sent!\n");

    recv_len = recvfrom(g_sockfd, out, 2048, 0, (struct sockaddr *) &sin_serv, &slen);
    if (recv_len < 0) {
        perror("recvfrom");
        goto out;
    }
    printf("Received packet from %s:%d\n", inet_ntoa(sin_serv.sin_addr), ntohs(sin_serv.sin_port));
    hexdump("received", out, recv_len);
    
out:
    sleep(5);
}

void 
device_handler_init(void)
{
    struct sockaddr_in sin;
    
    if (iputils_get_local_ip(g_local_ip) != 0) {
        fprintf(stderr, "Could not get local ip address.\n");
        return;
    }

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
    
    event_thread_create("device_handler", device_handler_loop, NULL, NULL);
    return;
    
socket_err:
    close(g_sockfd);
}
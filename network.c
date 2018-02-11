/*
 * Copyright (c) 2017 Dariusz Stojaczyk. All Rights Reserved.
 * The following source code is released under an MIT-style license,
 * that can be found in the LICENSE file.
 */

#include <stdlib.h>
#include <stdatomic.h>
#include <stdio.h>
#include <zconf.h>
#include <stdbool.h>
#include <memory.h>
#include <errno.h>
#include <arpa/inet.h>
#include "network.h"
#include "log.h"

#define MAX_NETWORK_CONNECTIONS 32

struct network_conn {
    int fd;
    enum network_type type;
    bool connected;
    bool is_stream;
    struct sockaddr_in sin_me;
    struct sockaddr_in sin_oth;
    struct timeval timeout;
};

static atomic_int g_conn_count;
static struct network_conn g_conns[MAX_NETWORK_CONNECTIONS];

static struct network_conn *
get_network_conn(int conn_id)
{
    struct network_conn *conn = NULL;

    if ((unsigned) conn_id < MAX_NETWORK_CONNECTIONS) {
        conn = &g_conns[conn_id];
    }

    return conn;
}

static int
create_socket(struct network_conn *conn, unsigned timeout_sec)
{
    int one = 1;

    if (conn->type == NETWORK_TYPE_UDP) {
        conn->fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    } else {
        conn->fd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
        conn->is_stream = true;
    }

    if (conn->fd == -1) {
        perror("socket");
        return -1;
    }

    conn->timeout.tv_sec = timeout_sec;
    conn->timeout.tv_usec = 0;

    if (setsockopt(conn->fd, SOL_SOCKET, SO_RCVTIMEO, (char *)&conn->timeout,
                   sizeof(conn->timeout)) != 0) {
        perror("setsockopt recv");
    }

    if (setsockopt(conn->fd, SOL_SOCKET, SO_SNDTIMEO, (char *)&conn->timeout,
                   sizeof(conn->timeout)) != 0) {
        perror("setsockopt send");
    }

    if (setsockopt(conn->fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(int)) != 0) {
        perror("setsockopt");
    }

    return 0;
}

int
network_open(enum network_type type, unsigned timeout_sec)
{
    int conn_id;
    struct network_conn *conn;

    conn_id = atomic_fetch_add(&g_conn_count, 1);
    conn = &g_conns[conn_id];

    conn->type = type;
    if (create_socket(conn, timeout_sec) != 0) {
        return -1;
    }

    return conn_id;
}

int
network_bind(int conn_id, in_port_t local_port)
{
    struct network_conn *conn;

    conn = get_network_conn(conn_id);

    conn->sin_me.sin_family = AF_INET;
    conn->sin_me.sin_addr.s_addr = htonl(INADDR_ANY);
    conn->sin_me.sin_port = local_port;

    if (bind(conn->fd, (struct sockaddr *) &conn->sin_me, sizeof(conn->sin_me)) != 0) {
        perror("bind");
        return -1;
    }

    return 0;
}

int
network_reconnect(int conn_id, in_addr_t dest_addr, in_port_t dest_port)
{
    struct network_conn *conn;
    int retries;

    conn = get_network_conn(conn_id);

    if (conn->connected) {
        network_close(conn_id);

        if (create_socket(conn, (unsigned) conn->timeout.tv_sec) != 0) {
            return -1;
        }

        if (conn->sin_me.sin_port &&
            network_bind(conn_id, conn->sin_me.sin_port) != 0) {
            return -1;
        }
    }

    conn->is_stream = true;
    conn->sin_oth.sin_addr.s_addr = dest_addr;
    conn->sin_oth.sin_family = AF_INET;
    conn->sin_oth.sin_port = dest_port;

    if (conn->type == NETWORK_TYPE_UDP) {
        return 0;
    }

    for (retries = 0; retries < 3; ++retries) {
        usleep(1000 * 25);
        if (connect(conn->fd, (struct sockaddr *) &conn->sin_oth , sizeof(conn->sin_oth)) == 0) {
            break;
        }
    }

    if (retries == 3) {
        perror("connect");
        return -1;
    }

    conn->connected = true;
    return 0;
}

int
network_send(int conn_id, const void *buf, size_t len)
{
    struct network_conn *conn;
    ssize_t sent_bytes;
    char hexdump_line[64];

    conn = get_network_conn(conn_id);
    if (conn->type == NETWORK_TYPE_UDP) {
        sent_bytes = sendto(conn->fd, buf, len, 0, (struct sockaddr *) &conn->sin_oth,
                            sizeof(conn->sin_oth));
    } else {
        sent_bytes = send(conn->fd, buf, len, 0);
    }

    if (sent_bytes < 0) {
        perror("sendto");
    }

    snprintf(hexdump_line, sizeof(hexdump_line), "sent %zd/%zu bytes to %d", sent_bytes,
             len, ntohs(conn->sin_oth.sin_port));
    hexdump(hexdump_line, buf, len);
    
    return (int) sent_bytes;
}

int
network_receive(int conn_id, void *buf, size_t len)
{
    struct network_conn *conn;
    ssize_t recv_bytes;
    struct sockaddr_in sin_oth_tmp;
    socklen_t slen;
    char hexdump_line[64];

    conn = get_network_conn(conn_id);
    slen = sizeof(sin_oth_tmp);

    if (conn->type == NETWORK_TYPE_UDP) {
        recv_bytes = recvfrom(conn->fd, buf, len, 0, (struct sockaddr *) &sin_oth_tmp, &slen);
    } else {
        recv_bytes = recv(conn->fd, buf, len, 0);
    }

    if (recv_bytes < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            perror("recvfrom");
        }
        return -1;
    }

    if (conn->type == NETWORK_TYPE_UDP && !conn->is_stream) {
        memcpy(&conn->sin_oth, &sin_oth_tmp, sizeof(conn->sin_oth));
    }

    snprintf(hexdump_line, sizeof(hexdump_line), "received %zd bytes from %d",
             recv_bytes, ntohs(conn->sin_oth.sin_port));
    hexdump(hexdump_line, buf, recv_bytes);

    return (int) recv_bytes;
}

int
network_get_client_ip(int conn_id, char ip[16])
{
    struct network_conn *conn;
    const char *ret;

    conn = get_network_conn(conn_id);

    ret = inet_ntop(AF_INET, &conn->sin_oth.sin_addr, ip, 16);
    return ret != NULL ? 0 : -1;
}


int
network_close(int conn_id)
{
    struct network_conn *conn;

    conn = get_network_conn(conn_id);
    close(conn->fd);
    conn->connected = false;
    return 0;
}

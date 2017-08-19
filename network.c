/*
 * Copyright (c) 2017 Dariusz Stojaczyk. All Rights Reserved.
 * The following source code is released under an MIT-style license,
 * that can be found in the LICENSE file.
 */

#include <stdlib.h>
#include <stdatomic.h>
#include <stdio.h>
#include <zconf.h>
#include <assert.h>
#include <stdbool.h>
#include <memory.h>
#include <errno.h>
#include <arpa/inet.h>
#include "network.h"
#include "log.h"

#define MAX_NETWORK_CONNECTIONS 32
#define CONNECTION_TIMEOUT_SEC 3

struct network_conn {
    int fd;
    enum network_type type;
    bool connected;
    bool dynamic_client;
    struct sockaddr_in sin_me;
    struct sockaddr_in sin_oth;
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
create_socket(struct network_conn *conn)
{
    struct timeval timeout;
    int one = 1;

    if (conn->type == NETWORK_TYPE_UDP) {
        conn->fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    } else {
        conn->fd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    }

    if (conn->fd == -1) {
        perror("socket");
        return -1;
    }

    timeout.tv_sec = CONNECTION_TIMEOUT_SEC;
    timeout.tv_usec = 0;

    if (setsockopt(conn->fd, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout)) != 0) {
        perror("setsockopt recv");
    }

    if (setsockopt(conn->fd, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout, sizeof(timeout)) != 0) {
        perror("setsockopt send");
    }

    if (setsockopt(conn->fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(int)) != 0) {
        perror("setsockopt");
    }

    return 0;
}

static int
init_conn(struct network_conn *conn, in_port_t local_port, in_addr_t dest_addr, in_port_t dest_port)
{
    if (create_socket(conn) == -1) {
        return -1;
    }

    conn->sin_me.sin_family = AF_INET;
    conn->sin_me.sin_addr.s_addr = htonl(INADDR_ANY);
    conn->sin_me.sin_port = local_port;

    if (local_port > 0 && bind(conn->fd, (struct sockaddr *) &conn->sin_me, sizeof(conn->sin_me)) != 0) {
        perror("bind");
        close(conn->fd);
        return -1;
    }

    if (dest_addr == 0) {
        conn->dynamic_client = true;
    }

    conn->sin_oth.sin_addr.s_addr = dest_addr;
    conn->sin_oth.sin_family = AF_INET;
    conn->sin_oth.sin_port = dest_port;

    if (conn->type == NETWORK_TYPE_UDP) {
        return 0;
    }

    if (connect(conn->fd, (struct sockaddr *) &conn->sin_oth , sizeof(conn->sin_oth)) != 0) {
        perror("connect");
        close(conn->fd);
        return -1;
    }

    return 0;
}

int
network_init_conn(enum network_type type, in_port_t local_port, in_addr_t dest_addr, in_port_t dest_port)
{
    int conn_id;
    struct network_conn *conn;

    conn_id = atomic_fetch_add(&g_conn_count, 1);
    conn = &g_conns[conn_id];
    assert(!conn->connected);

    conn->type = type;
    if (init_conn(conn, local_port, dest_addr, dest_port) != 0) {
        return -1;
    }

    conn->connected = true;
    return conn_id;
}

int
network_reconnect(int conn_id)
{
    struct network_conn *conn;

    conn = get_network_conn(conn_id);

    if (conn->connected) {
        network_disconnect(conn_id);
    }

    if (init_conn(conn, conn->sin_me.sin_port, conn->sin_oth.sin_addr.s_addr, conn->sin_oth.sin_port) != 0) {
        return -1;
    }

    conn->connected = true;
    return conn_id;
}

int
network_send(int conn_id, const void *buf, size_t len)
{
    struct network_conn *conn;
    ssize_t sent_bytes;
    char hexdump_line[64];

    conn = get_network_conn(conn_id);
    assert(conn->connected);

    if (conn->type == NETWORK_TYPE_UDP) {
        sent_bytes = sendto(conn->fd, buf, len, 0, (struct sockaddr *) &conn->sin_oth, sizeof(conn->sin_oth));
    } else {
        sent_bytes = send(conn->fd, buf, len, 0);
    }

    if (sent_bytes < 0) {
        perror("sendto");
    }

    snprintf(hexdump_line, sizeof(hexdump_line), "sent %zd/%zu bytes to %d", sent_bytes, len, ntohs(conn->sin_oth.sin_port));
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
    assert(conn->connected);

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

    if (conn->type == NETWORK_TYPE_UDP && conn->dynamic_client) {
        memcpy(&conn->sin_oth, &sin_oth_tmp, sizeof(conn->sin_oth));
    }

    snprintf(hexdump_line, sizeof(hexdump_line), "received %zd bytes from %d", recv_bytes, ntohs(conn->sin_oth.sin_port));
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
network_disconnect(int conn_id)
{
    struct network_conn *conn;

    conn = get_network_conn(conn_id);
    assert(conn->connected);

    close(conn->fd);
    conn->connected = false;
    return 0;
}

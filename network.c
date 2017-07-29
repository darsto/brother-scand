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
#include "network.h"
#include "log.h"

#define MAX_NETWORK_CONNECTIONS 32
#define CONNECTION_TIMEOUT_SEC 3

struct network_conn {
    int fd;
    bool connected;
    struct sockaddr_in sin_me;
    struct sockaddr_in sin_oth;
    socklen_t slen;
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
create_socket(void)
{
    struct timeval timeout;
    int fd, one = 1;

    fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd == -1) {
        perror("socket");
        return -1;
    }

    timeout.tv_sec = CONNECTION_TIMEOUT_SEC;
    timeout.tv_usec = 0;

    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout)) != 0) {
        perror("setsockopt recv");
    }

    if (setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout, sizeof(timeout)) != 0) {
        perror("setsockopt send");
    }

    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(int)) != 0) {
        perror("setsockopt");
    }

    return fd;
}

static int
init_conn(struct network_conn *conn, in_port_t local_port, in_addr_t dest_addr, in_port_t dest_port)
{
    conn->fd = create_socket();
    if (conn->fd == -1) {
        return -1;
    }

    conn->sin_me.sin_family = AF_INET;
    conn->sin_me.sin_addr.s_addr = htonl(INADDR_ANY);
    conn->sin_me.sin_port = local_port;

    if (bind(conn->fd, (struct sockaddr *) &conn->sin_me, sizeof(conn->sin_me)) != 0) {
        perror("bind");
        close(conn->fd);
        return -1;
    }

    /* destination init */
    conn->sin_oth.sin_addr.s_addr = dest_addr;
    conn->sin_oth.sin_family = AF_INET;
    conn->sin_oth.sin_port = dest_port;

    conn->slen = sizeof(conn->sin_oth);
    return 0;
}

int
network_udp_init_conn(in_port_t local_port, in_addr_t dest_addr, in_port_t dest_port)
{
    int conn_id;
    struct network_conn *conn;

    conn_id = atomic_fetch_add(&g_conn_count, 1);
    conn = &g_conns[conn_id];
    assert(!conn->connected);
    
    conn->fd = create_socket();

    conn->sin_me.sin_family = AF_INET;
    conn->sin_me.sin_addr.s_addr = htonl(INADDR_ANY);
    conn->sin_me.sin_port = local_port;

    if (local_port > 0 && bind(conn->fd, (struct sockaddr *) &conn->sin_me, sizeof(conn->sin_me)) == -1) {
        perror("bind");
        close(conn->fd);
        return -1;
    }

    conn->sin_oth.sin_addr.s_addr = dest_addr;
    conn->sin_oth.sin_family = AF_INET;
    conn->sin_oth.sin_port = dest_port;

    conn->slen = sizeof(conn->sin_oth);

    conn->connected = true;
    return conn_id;
}

int
network_udp_send(int conn_id, const void *buf, size_t len)
{
    struct network_conn *conn;
    ssize_t sent_bytes;
    char hexdump_line[64];

    conn = get_network_conn(conn_id);
    assert(conn->connected);
    
    sent_bytes = sendto(conn->fd, buf, len, 0, (struct sockaddr *) &conn->sin_oth, conn->slen);
    if (sent_bytes < 0) {
        perror("sendto");
    }

    snprintf(hexdump_line, sizeof(hexdump_line), "sent %zd/%zu bytes to %d", sent_bytes, len, ntohs(conn->sin_oth.sin_port));
    hexdump(hexdump_line, buf, len);
    
    return (int) sent_bytes;
}

int
network_udp_receive(int conn_id, void *buf, size_t len)
{
    struct network_conn *conn;
    ssize_t recv_bytes;
    struct sockaddr_in sin_oth_tmp;
    socklen_t slen;
    char hexdump_line[64];
    int rc;
    
    conn = get_network_conn(conn_id);
    assert(conn->connected);

    slen = sizeof(sin_oth_tmp);
    recv_bytes = recvfrom(conn->fd, buf, len, 0, (struct sockaddr *) &sin_oth_tmp, &slen);
    if (recv_bytes < 0) {
        rc = errno;
        if (rc != EAGAIN && rc != EWOULDBLOCK) {
            perror("recvfrom");
        }
        return -1;
    }

    snprintf(hexdump_line, sizeof(hexdump_line), "received %zd bytes from %d", recv_bytes, ntohs(sin_oth_tmp.sin_port));
    hexdump(hexdump_line, buf, recv_bytes);
    
    if (conn->sin_oth.sin_addr.s_addr == 0) {
        memcpy(&conn->sin_oth, &sin_oth_tmp, sizeof(conn->sin_oth));
    }
    
    return (int) recv_bytes;
}

int
network_udp_disconnect(int conn_id)
{
    struct network_conn *conn;

    conn = get_network_conn(conn_id);
    assert(conn->connected);

    close(conn->fd);
    conn->connected = false;
    return 0;
}

int
network_tcp_init_conn(in_port_t local_port, in_addr_t dest_addr, in_port_t dest_port)
{
    int conn_id;
    struct network_conn *conn;

    conn_id = atomic_fetch_add(&g_conn_count, 1);
    conn = &g_conns[conn_id];
    assert(!conn->connected);

    if (init_conn(conn, local_port, dest_addr, dest_port) != 0) {
        return -1;
    }

    if (connect(conn->fd , (struct sockaddr *)&conn->sin_oth , conn->slen) != 0) {
        perror("connect");
        close(conn->fd);
        return -1;
    }

    conn->connected = true;
    return conn_id;
}

int
network_tcp_send(int conn_id, const void *buf, size_t len)
{
    struct network_conn *conn;
    ssize_t sent_bytes;
    char hexdump_line[64];
    
    conn = get_network_conn(conn_id);
    assert(conn->connected);

    sent_bytes = send(conn->fd, buf, len, 0);
    if (sent_bytes < 0) {
        perror("sendto");
    }
    
    snprintf(hexdump_line, sizeof(hexdump_line), "sent %zd/%zu bytes to %d", sent_bytes, len, ntohs(conn->sin_oth.sin_port));
    hexdump(hexdump_line, buf, len);

    return (int) sent_bytes;
}

int
network_tcp_receive(int conn_id, void *buf, size_t len)
{
    struct network_conn *conn;
    ssize_t recv_bytes;
    char hexdump_line[64];
    int rc;

    conn = get_network_conn(conn_id);
    assert(conn->connected);

    recv_bytes = recv(conn->fd, buf, len, 0);
    rc = errno;
    if (recv_bytes < 0) {
        if (rc != EAGAIN && rc != EWOULDBLOCK) {
            perror("recvfrom");
        }
        return -1;
    }

    snprintf(hexdump_line, sizeof(hexdump_line), "received %zd bytes from %d", recv_bytes, ntohs(conn->sin_oth.sin_port));
    hexdump(hexdump_line, buf, recv_bytes);

    return (int) recv_bytes;
}

int
network_tcp_disconnect(int conn_id)
{
    struct network_conn *conn;

    conn = get_network_conn(conn_id);
    assert(conn->connected);

    close(conn->fd);
    conn->connected = false;
    return 0;
}

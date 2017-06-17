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
#include "network.h"

#define MAX_NETWORK_CONNECTIONS 32

enum network_conn_state {
    NETWORK_CONN_STATE_UNITIALIZED = 0,
    NETWORK_CONN_STATE_DISCONNECTED,
    NETWORK_CONN_STATE_CONNECTED,
};

struct network_conn {
    int fd;
    enum network_conn_state state;
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

int
network_udp_init_conn(in_port_t port)
{
    int conn_id;
    struct network_conn *conn;

    conn_id = atomic_fetch_add(&g_conn_count, 1);
    conn = &g_conns[conn_id];
    assert(conn->state == NETWORK_CONN_STATE_UNITIALIZED);
    
    conn->fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (conn->fd == -1) {
        perror("socket");
        return -1;
    }

    conn->sin_me.sin_family = AF_INET;
    conn->sin_me.sin_addr.s_addr = htonl(INADDR_ANY);
    conn->sin_me.sin_port = port;

    if (bind(conn->fd, (struct sockaddr *) &conn->sin_me, sizeof(conn->sin_me)) == -1) {
        perror("bind");
        close(conn->fd);
        return -1;
    }
    
    conn->state = NETWORK_CONN_STATE_DISCONNECTED;
    return conn_id;
}

int
network_udp_connect(int conn_id, in_addr_t addr, in_port_t port)
{
    struct network_conn *conn;

    conn = get_network_conn(conn_id);
    assert(conn->state == NETWORK_CONN_STATE_DISCONNECTED);

    conn->sin_oth.sin_addr.s_addr = addr;
    conn->sin_oth.sin_family = AF_INET;
    conn->sin_oth.sin_port = port;
    
    conn->slen = sizeof(conn->sin_oth);

    conn->state = NETWORK_CONN_STATE_CONNECTED;
    return 0;
}

int
network_udp_send(int conn_id, const void *buf, size_t len)
{
    struct network_conn *conn;
    ssize_t sent_bytes;
    
    conn = get_network_conn(conn_id);
    assert(conn->state == NETWORK_CONN_STATE_CONNECTED);
    
    sent_bytes = sendto(conn->fd, buf, len, 0, (struct sockaddr *) &conn->sin_oth, conn->slen);
    if (sent_bytes < 0) {
        perror("sendto");
    }
    
    return (int) sent_bytes;
}

int
network_udp_receive(int conn_id, void *buf, size_t len)
{
    struct network_conn *conn;
    ssize_t recv_bytes;

    conn = get_network_conn(conn_id);
    assert(conn->state == NETWORK_CONN_STATE_CONNECTED);

    recv_bytes = recvfrom(conn->fd, buf, len, 0, (struct sockaddr *) &conn->sin_oth, &conn->slen);
    if (recv_bytes < 0) {
        perror("recvfrom");
    }
    
    return (int) recv_bytes;
}

int
network_udp_disconnect(int conn_id)
{
    struct network_conn *conn;

    conn = get_network_conn(conn_id);
    assert(conn->state == NETWORK_CONN_STATE_CONNECTED);
    
    conn->state = NETWORK_CONN_STATE_DISCONNECTED;
    close(conn->fd);
    return 0;
}

int
network_udp_free(int conn_id)
{
    struct network_conn *conn;

    conn = get_network_conn(conn_id);
    assert(conn->state == NETWORK_CONN_STATE_DISCONNECTED);

    conn->state = NETWORK_CONN_STATE_UNITIALIZED;
    return 0;
}
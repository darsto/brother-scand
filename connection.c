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

#include "connection.h"
#include "log.h"

struct brother_conn {
    enum brother_connection_type type;
    int fd;
    bool connected;
    bool is_stream;
    struct sockaddr_in sin_me;
    struct sockaddr_in sin_oth;
    struct timeval timeout;
};

static int
create_socket(struct brother_conn *conn, unsigned timeout_sec)
{
    int one = 1;

    if (conn->type == BROTHER_CONNECTION_TYPE_UDP) {
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

struct brother_conn *
brother_conn_open(enum brother_connection_type type, unsigned timeout_sec)
{
    struct brother_conn *conn;

    conn = calloc(1, sizeof(*conn));
    if (conn == NULL) {
        return NULL;
    }

    conn->type = type;
    if (create_socket(conn, timeout_sec) != 0) {
        return NULL;
    }

    return conn;
}

int
brother_conn_bind(struct brother_conn *conn, in_port_t local_port)
{
    conn->sin_me.sin_family = AF_INET;
    conn->sin_me.sin_addr.s_addr = htonl(INADDR_ANY);
    conn->sin_me.sin_port = local_port;

    if (bind(conn->fd, (struct sockaddr *)&conn->sin_me,
             sizeof(conn->sin_me)) != 0) {
        perror("bind");
        return -1;
    }

    return 0;
}

int
brother_conn_reconnect(struct brother_conn *conn, in_addr_t dest_addr,
                  in_port_t dest_port)
{
    int retries;

    if (conn->connected) {
        close(conn->fd);
        conn->connected = false;

        if (create_socket(conn, (unsigned)conn->timeout.tv_sec) != 0) {
            return -1;
        }

        if (conn->sin_me.sin_port &&
            brother_conn_bind(conn, conn->sin_me.sin_port) != 0) {
            return -1;
        }
    }

    conn->is_stream = true;
    conn->sin_oth.sin_addr.s_addr = dest_addr;
    conn->sin_oth.sin_family = AF_INET;
    conn->sin_oth.sin_port = dest_port;

    for (retries = 0; retries < 3; ++retries) {
        usleep(1000 * 25);
        if (connect(conn->fd, (struct sockaddr *)&conn->sin_oth,
                    sizeof(conn->sin_oth)) == 0) {
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
brother_conn_sendto(struct brother_conn *conn, const void *buf, size_t len,
               in_addr_t dest_addr, in_port_t dest_port)
{
    struct sockaddr_in sin_oth;
    ssize_t sent_bytes;

    if (conn->type == BROTHER_CONNECTION_TYPE_TCP) {
        LOG_ERR("sendto can't be used with TCP sockets\n");
        return -1;
    }

    sin_oth.sin_addr.s_addr = dest_addr;
    sin_oth.sin_family = AF_INET;
    sin_oth.sin_port = dest_port;

    do {
        sent_bytes = sendto(conn->fd, buf, len, 0,
                            (struct sockaddr *) &sin_oth,
                            sizeof(sin_oth));
    } while (errno == EINTR);

    if (sent_bytes < 0) {
        perror("sendto");
    }

    LOG_DEBUG("sent %zd/%zu bytes to %d", sent_bytes, len,
              ntohs(sin_oth.sin_port));
    DUMP_DEBUG(buf, len);

    return (int) sent_bytes;
}

int
brother_conn_send(struct brother_conn *conn, const void *buf, size_t len)
{
    ssize_t sent_bytes;

    do {
        if (conn->type == BROTHER_CONNECTION_TYPE_UDP) {
            sent_bytes = sendto(conn->fd, buf, len, 0,
                                (struct sockaddr *) &conn->sin_oth,
                                sizeof(conn->sin_oth));
        } else {
            sent_bytes = send(conn->fd, buf, len, 0);
        }
    } while (errno == EINTR);

    if (sent_bytes < 0) {
        perror("sendto");
    }

    LOG_DEBUG("sent %zd/%zu bytes to %d", sent_bytes, len,
              ntohs(conn->sin_oth.sin_port));
    DUMP_DEBUG(buf, len);

    return (int) sent_bytes;
}

int
brother_conn_receive(struct brother_conn *conn, void *buf, size_t len)
{
    ssize_t recv_bytes;
    struct sockaddr_in sin_oth_tmp;
    socklen_t slen;

    slen = sizeof(sin_oth_tmp);

    do {
        if (conn->type == BROTHER_CONNECTION_TYPE_UDP) {
            recv_bytes = recvfrom(conn->fd, buf, len, 0,
                                  (struct sockaddr *) &sin_oth_tmp, &slen);
        } else {
            recv_bytes = recv(conn->fd, buf, len, 0);
        }
    } while (errno == EINTR);

    if (recv_bytes < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            perror("recvfrom");
        }
        return -1;
    }

    if (conn->type == BROTHER_CONNECTION_TYPE_UDP && !conn->is_stream) {
        memcpy(&conn->sin_oth, &sin_oth_tmp, sizeof(conn->sin_oth));
    }

    LOG_DEBUG("received %zd bytes from %d", recv_bytes,
              ntohs(conn->sin_oth.sin_port));
    DUMP_DEBUG(buf, recv_bytes);

    return (int) recv_bytes;
}

int
brother_conn_get_client_ip(struct brother_conn *conn, char ip[16])
{
    const char *ret;

    ret = inet_ntop(AF_INET, &conn->sin_oth.sin_addr, ip, 16);
    return ret != NULL ? 0 : -1;
}

int
brother_conn_get_local_ip(struct brother_conn *conn, char ip[16])
{
    const char *ret;
    struct sockaddr_in name;
    socklen_t namelen = sizeof(name);

    if (getsockname(conn->fd, (struct sockaddr *) &name, &namelen) != 0) {
        perror("getsockname");
        return -1;
    }

    ret = inet_ntop(AF_INET, &name.sin_addr, ip, 16);
    return ret != NULL ? 0 : -1;
}

void
brother_conn_close(struct brother_conn *conn)
{
    close(conn->fd);
    free(conn);
}

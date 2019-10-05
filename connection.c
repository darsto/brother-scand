/*
 * Copyright (c) 2017 Dariusz Stojaczyk. All Rights Reserved.
 * The following source code is released under an MIT-style license,
 * that can be found in the LICENSE file.
 */

#include <arpa/inet.h>
#include <errno.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

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
    char *buf;  // buffered output
    size_t buf_position;
    size_t buf_filled;
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
        if (conn->buf) free(conn->buf);
        conn->buf = calloc(1, BROTHER_CONN_READ_BUFSIZE);
        conn->buf_position = 0;
        conn->buf_filled = 0;
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

void brother_conn_disconnect(struct brother_conn *conn) {
  if (conn->connected) {
    close(conn->fd);
    conn->connected = false;
    conn->fd = 0;
  }
}

int
brother_conn_reconnect(struct brother_conn *conn, in_addr_t dest_addr,
                  in_port_t dest_port)
{
    int retries;
    brother_conn_disconnect(conn);
    if (!conn->fd) {
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

    const int max_retries = 3;
    for (retries = 0; retries < max_retries; ++retries) {
      usleep(1000 * 25);
      if (connect(conn->fd, (struct sockaddr *)&conn->sin_oth,
                  sizeof(conn->sin_oth)) == 0) {
        break;
      }
      perror("connect");
    }

    if (retries == max_retries) {
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

    LOG_DEBUG("sent %zd/%zu bytes to :%d\n", sent_bytes, len,
              ntohs(sin_oth.sin_port));
    DUMP_DEBUG(buf, len);

    return (int) sent_bytes;
}

int
brother_conn_poll(struct brother_conn *conn, unsigned timeout_sec)
{
    struct pollfd pfd;
    int rc;

    pfd.fd = conn->fd;
    pfd.events = POLLIN | POLLERR | POLLHUP | POLLNVAL;

    do {
        rc = poll(&pfd, 1, timeout_sec * 1000);
    } while (rc == -EINTR);
    if (rc < 0) {
        return rc;
    }

    return pfd.revents & (POLLIN | POLLERR | POLLHUP | POLLNVAL);
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

    LOG_DEBUG("received %zd bytes from :%d\n", recv_bytes,
              ntohs(conn->sin_oth.sin_port));
    DUMP_DEBUG(buf, recv_bytes);

    return (int) recv_bytes;
}

size_t brother_conn_data_available(struct brother_conn *conn) {
  return conn->buf_filled - conn->buf_position;
}

int brother_conn_fill_buffer(struct brother_conn *conn, size_t size,
                             int timeout_seconds) {
  if (brother_conn_data_available(conn) >= size) {
    return 0;
  }
  int rc = brother_conn_poll(conn, timeout_seconds);
  if (rc < 0) {
    return -1;
  }
  if (brother_conn_peek(conn, size - brother_conn_data_available(conn)) ==
      NULL) {
    return -1;
  }
  return 0;
}

void *brother_conn_peek(struct brother_conn *conn, size_t len) {
  int rc;
  if (conn->type == BROTHER_CONNECTION_TYPE_UDP) {
    fprintf(stderr, "Buffered reading is only available to TCP sockets");
    return NULL;
  }
  if (BROTHER_CONN_READ_BUFSIZE < len) {
    fprintf(stderr, "Can't read more than %zd bytes at once.", len);
    return NULL;
  }
  if (BROTHER_CONN_READ_BUFSIZE - conn->buf_position < len) {
    memcpy(conn->buf, conn->buf + conn->buf_position,
           brother_conn_data_available(conn));
    conn->buf_filled -= conn->buf_position;
    conn->buf_position = 0;
  }
  size_t available_chars = conn->buf_filled - conn->buf_position;
  if (available_chars < len) {
    rc = brother_conn_receive(conn, conn->buf + conn->buf_filled,
                              BROTHER_CONN_READ_BUFSIZE - conn->buf_filled);
    if (rc < 0) {
      return NULL;
    }
    conn->buf_filled += rc;
  }
  return conn->buf + conn->buf_position;
}

void *brother_conn_read(struct brother_conn *conn, size_t len) {
  void *result = brother_conn_peek(conn, len);
  if (result != NULL) {
    conn->buf_position += len;
  }
  return result;
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
    if (conn->buf) free(conn->buf);
    free(conn);
}

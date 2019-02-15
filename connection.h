/*
 * Copyright (c) 2017 Dariusz Stojaczyk. All Rights Reserved.
 * The following source code is released under an MIT-style license,
 * that can be found in the LICENSE file.
 */

#include <netinet/in.h>

#ifndef BROTHER_CONNECTION_H
#define BROTHER_CONNECTION_H

#define BROTHER_CONN_READ_BUFSIZE 2048

enum brother_connection_type {
    BROTHER_CONNECTION_TYPE_UDP,
    BROTHER_CONNECTION_TYPE_TCP,
};

struct brother_conn;

struct brother_conn *brother_conn_open(enum brother_connection_type type, unsigned timeout_sec);
int brother_conn_bind(struct brother_conn *conn, in_port_t local_port);
int brother_conn_reconnect(struct brother_conn *conn, in_addr_t dest_addr,
                      in_port_t dest_port);
int brother_conn_poll(struct brother_conn *conn, unsigned timeout_sec);
int brother_conn_send(struct brother_conn *conn, const void *buf, size_t len);
int brother_conn_sendto(struct brother_conn *conn, const void *buf, size_t len,
                   in_addr_t dest_addr, in_port_t dest_port);
size_t brother_conn_data_available(struct brother_conn *conn);
int brother_conn_receive(struct brother_conn *conn, void *buf, size_t len);
int brother_conn_fill_buffer(struct brother_conn *conn, size_t size,
                             int timeout_seconds);
int brother_conn_get_client_ip(struct brother_conn *conn, char ip[16]);
int brother_conn_get_local_ip(struct brother_conn *conn, char ip[16]);
void brother_conn_close(struct brother_conn *conn);

void *brother_conn_peek(struct brother_conn *conn, size_t len);
void *brother_conn_read(struct brother_conn *conn, size_t len);

#endif //BROTHER_CONNECTION_H

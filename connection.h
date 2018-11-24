/*
 * Copyright (c) 2017 Dariusz Stojaczyk. All Rights Reserved.
 * The following source code is released under an MIT-style license,
 * that can be found in the LICENSE file.
 */

#include <netinet/in.h>

#ifndef BROTHER_CONNECTION_H
#define BROTHER_CONNECTION_H

enum brother_connection_type {
    BROTHER_CONNECTION_TYPE_UDP,
    BROTHER_CONNECTION_TYPE_TCP,
};

struct brother_conn;

struct brother_conn *brother_conn_open(enum brother_connection_type type, unsigned timeout_sec);
int brother_conn_bind(struct brother_conn *conn, in_port_t local_port);
int brother_conn_reconnect(struct brother_conn *conn, in_addr_t dest_addr,
                      in_port_t dest_port);
int brother_conn_send(struct brother_conn *conn, const void *buf, size_t len);
int brother_conn_sendto(struct brother_conn *conn, const void *buf, size_t len,
                   in_addr_t dest_addr, in_port_t dest_port);
int brother_conn_receive(struct brother_conn *conn, void *buf, size_t len);
int brother_conn_get_client_ip(struct brother_conn *conn, char ip[16]);
int brother_conn_get_local_ip(struct brother_conn *conn, char ip[16]);
void brother_conn_close(struct brother_conn *conn);

#endif //BROTHER_CONNECTION_H

/*
 * Copyright (c) 2017 Dariusz Stojaczyk. All Rights Reserved.
 * The following source code is released under an MIT-style license,
 * that can be found in the LICENSE file.
 */

#include <netinet/in.h>

#ifndef BROTHER_NETWORK_H
#define BROTHER_NETWORK_H

int network_udp_init_conn(in_port_t local_port, in_addr_t dest_addr, in_port_t dest_port);
int network_udp_send(int conn_id, const void *buf, size_t len);
int network_udp_receive(int conn_id, void *buf, size_t len);
int network_udp_disconnect(int conn_id);

int network_tcp_init_conn(in_port_t local_port, in_addr_t dest_addr, in_port_t dest_port);
int network_tcp_send(int conn_id, const void *buf, size_t len);
int network_tcp_receive(int conn_id, void *buf, size_t len);
int network_tcp_disconnect(int conn_id);

#endif //BROTHER_NETWORK_H

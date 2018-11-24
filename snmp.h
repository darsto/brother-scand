/*
 * Copyright (c) 2017 Dariusz Stojaczyk. All Rights Reserved.
 * The following source code is released under an MIT-style license,
 * that can be found in the LICENSE file.
 */

#ifndef BROTHER_SNMP_H
#define BROTHER_SNMP_H

#include <stdint.h>
#include <stdlib.h>

int snmp_get_printer_status(struct network_conn *conn,
                            uint8_t *buf, size_t buf_len, in_addr_t dest_addr);
int snmp_register_scanner_driver(struct network_conn *conn, bool enabled,
                                 uint8_t *buf, size_t buf_len,
                                 const char *functions[4], in_addr_t dest_addr);

#endif //BROTHER_SNMP_H

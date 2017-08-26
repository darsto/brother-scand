/*
 * Copyright (c) 2017 Dariusz Stojaczyk. All Rights Reserved.
 * The following source code is released under an MIT-style license,
 * that can be found in the LICENSE file.
 */

#ifndef BROTHER_SNMP_H
#define BROTHER_SNMP_H

#include <stdint.h>
#include <stdlib.h>

int snmp_get_printer_status(int conn, uint8_t *buf, size_t buf_len);
int snmp_register_scanner_driver(int conn, bool enabled, uint8_t *buf, size_t buf_len, const char *functions[4]);

#endif //BROTHER_SNMP_H

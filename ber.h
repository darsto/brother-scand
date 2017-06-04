/*
 * Copyright (c) 2017 Dariusz Stojaczyk. All Rights Reserved.
 * The following source code is released under an MIT-style license,
 * that can be found in the LICENSE file.
 */

#ifndef BROTHER_BER_H
#define BROTHER_BER_H

enum ber_data_type {
    /* ASN.1 primitives */
    SNMP_DATA_T_INTEGER = 0x02,
    SNMP_DATA_T_OCTET_STRING = 0x04,
    SNMP_DATA_T_NULL = 0x05,
};

/** Encode variable-length integer */
int ber_encode_vlint(uint8_t *buf, uint32_t num);

int ber_encode_int(uint8_t *buf, int num);
int ber_encode_string(uint8_t *buf, const char *str);
int ber_encode_null(uint8_t *buf);

#endif //BROTHER_BER_H

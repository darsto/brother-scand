/*
 * Copyright (c) 2017 Dariusz Stojaczyk. All Rights Reserved.
 * The following source code is released under an MIT-style license,
 * that can be found in the LICENSE file.
 */

#ifndef DCP_J105_DEVICE_HANDLER_H
#define DCP_J105_DEVICE_HANDLER_H

void device_handler_init(const char *config_path);
struct device *device_handler_add_device(const char *ip);

#endif //DCP_J105_DEVICE_HANDLER_H

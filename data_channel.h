/*
 * Copyright (c) 2017 Dariusz Stojaczyk. All Rights Reserved.
 * The following source code is released under an MIT-style license,
 * that can be found in the LICENSE file.
 */

#ifndef BROTHER_DATA_CHANNEL_H
#define BROTHER_DATA_CHANNEL_H

#include "config.h"

struct data_channel *data_channel_create(struct device_config *config);
void data_channel_kick(struct data_channel *data_channel);

#endif //BROTHER_DATA_CHANNEL_H

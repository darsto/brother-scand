/*
 * Copyright (c) 2017 Dariusz Stojaczyk. All Rights Reserved.
 * The following source code is released under an MIT license.
 * Feel free to reuse, modify and distribute it.
 */

#ifndef DARSTO_UTILS_CONCURRENTQUEUE_H
#define DARSTO_UTILS_CONCURRENTQUEUE_H

#include <stdatomic.h>
#include <stddef.h>
#include <stdbool.h>

struct concurrent_queue {
    atomic_size_t head, tail;
    size_t size;
    void *data[0];
};

int con_queue_push(struct concurrent_queue *queue, void *element);
int con_queue_pop(struct concurrent_queue *queue, void **element);

#endif //DARSTO_UTILS_CONCURRENTQUEUE_H
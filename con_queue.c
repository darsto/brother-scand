/*
 * Copyright (c) 2017 Dariusz Stojaczyk. All Rights Reserved.
 * The following source code is released under an MIT-style license,
 * that can be found in the LICENSE file.
 */

#include "con_queue.h"

static size_t
con_queue_next(struct con_queue *queue, size_t num)
{
    return ++num % queue->size;
}

int
con_queue_push(struct con_queue *queue, void *element)
{
    size_t currentTail = atomic_load(&queue->tail);
    size_t newTail = con_queue_next(queue, currentTail);

    if (newTail == atomic_load(&queue->head)) {
        return -1;
    }

    queue->data[currentTail] = element;
    atomic_store(&queue->tail, newTail);

    return 0;
}

int
con_queue_pop(struct con_queue *queue, void **element)
{
    size_t currentHead = atomic_load(&queue->head);

    if (currentHead == atomic_load(&queue->tail)) {
        return -1;
    }

    *element = queue->data[currentHead];
    atomic_store(&queue->head, con_queue_next(queue, currentHead));

    return 0;
}

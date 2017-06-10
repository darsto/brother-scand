/*
 * Copyright (c) 2017 Dariusz Stojaczyk. All Rights Reserved.
 * The following source code is released under an MIT-style license,
 * that can be found in the LICENSE file.
 */

#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include <signal.h>
#include "event_thread.h"
#include "concurrent_queue.h"
#include "log.h"

#define MAX_EVENT_THREADS 32

enum event_thread_state {
    EVENT_THREAD_S_RUNNING,
    EVENT_THREAD_S_STOPPED,
};

struct event_thread {
    enum event_thread_state state;
    char *name;
    struct concurrent_queue *events;
    pthread_t pthread_id;
};

struct event {
    void (*callback)(void *, void *);
    void *arg1;
    void *arg2;
};

static atomic_size_t g_thread_cnt;
static struct event_thread g_threads[MAX_EVENT_THREADS];

static struct event_thread *
_get_event_thread(size_t thread_id)
{
    struct event_thread *thread;

    if (thread_id > MAX_EVENT_THREADS) {
        return NULL;
    }

    thread = &g_threads[thread_id - 1];    
    return thread;
}

int
enqueue_event(size_t thread_id, void (*callback)(void *, void *), void *arg1, void *arg2)
{
    struct event_thread *thread = _get_event_thread(thread_id);
    struct event *event;
    
    if (!thread) {
        fprintf(stderr, "Trying to enqueue event to inexistent thread.\n");
        return -1;
    }
    
    event = calloc(1, sizeof(*event));
    if (!event) {
        fprintf(stderr, "Failed to allocate memory for event.\n");
        return -1;
    }
    
    event->callback = callback;
    event->arg1 = arg1;
    event->arg2 = arg2;
    
    con_queue_push(thread->events, event);
    return 0;
}

static void
_event_thread_destroy(struct event_thread *thread) {
    struct event *event;

    while (con_queue_pop(thread->events, (void **) &event) == 0) {
        free(event);
    }
    
    free(thread->events);
    free(thread->name);
}

static void
_sig_handler(int signo __attribute__((unused)))
{
    /** Interrupt any blocking IO / poll */
}

static void *
_event_thread_loop(void *arg)
{
    struct event_thread *thread = arg;
    struct event *update_ev = NULL;
    struct event *event;

    if (signal(SIGUSR1, _sig_handler) == SIG_ERR) {
        fprintf(stderr, "%s: Failed to bind SIGUSR1 handler.\n", thread->name);
        goto out;
    }

    if (con_queue_pop(thread->events, (void **) &update_ev) != 0) {
        fprintf(stderr, "%s: Failed to fetch event thread update callback.\n", thread->name);
        goto out;
    }
    
    while (thread->state != EVENT_THREAD_S_STOPPED) {
        if (update_ev->callback) {
            update_ev->callback(update_ev->arg1, update_ev->arg2);
        }
        
        if (con_queue_pop(thread->events, (void **) &event) == 0) {
            event->callback(event->arg1, event->arg2);
            free(event);
        }
    }

out:
    free(update_ev);
    _event_thread_destroy(thread);    
    pthread_exit(NULL);
    return NULL;
}

size_t
event_thread_create(const char *name, void (*update_cb)(void *, void *), void *arg1, void *arg2)
{
    struct event_thread *thread;
    size_t thread_id;

    thread_id = atomic_fetch_add(&g_thread_cnt, 1);
    thread = &g_threads[thread_id - 1];
    
    thread->state = EVENT_THREAD_S_RUNNING;
    thread->name = strdup(name);
    if (!thread->name) {
        fprintf(stderr, "Fatal: strdup() failed, cannot start event thread.\n");
        goto err;
    }
    
    thread->events = calloc(1, sizeof(*thread->events) + 32 * sizeof(void *));
    if (!thread->events) {
        fprintf(stderr, "Fatal: calloc() failed, cannot start event thread.\n");
        goto name_err;
    }
    
    thread->events->size = 32;

    event_thread_enqueue_event(thread_id, update_cb, arg1, arg2);
    if (pthread_create(&thread->pthread_id, NULL, _event_thread_loop, thread) != 0) {
        fprintf(stderr, "Fatal: pthread_create() failed, cannot start event thread.\n");
        goto events_err;
    }
    
    return thread_id;
    
events_err:
    free(thread->events);  
name_err:
    free(thread->name);
err:
    return 0;
}

static void
_event_thread_stop_cb(void *arg1, void *arg2 __attribute__((unused)))
{
    struct event_thread *thread = arg1;
    
    thread->state = EVENT_THREAD_S_STOPPED;
}

int
event_thread_stop(size_t thread_id)
{
    struct event_thread *thread;

    thread = _get_event_thread(thread_id);
    if (!thread) {
        fprintf(stderr, "Trying to stop inexistent event thread %zu.\n", thread_id);
        return -1;
    }
    
    
    if (event_thread_enqueue_event(thread_id, _event_thread_stop_cb, thread, NULL) != 0) {
        fprintf(stderr, "Failed to stop thread %zu.\n", thread_id);
        return -1;
    }

    pthread_kill(thread->pthread_id, SIGUSR1);
    return 0;
}
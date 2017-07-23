/*
 * Copyright (c) 2017 Dariusz Stojaczyk. All Rights Reserved.
 * The following source code is released under an MIT-style license,
 * that can be found in the LICENSE file.
 */

#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <pthread.h>
#include <signal.h>
#include "event_thread.h"
#include "con_queue.h"

#define MAX_EVENT_THREADS 32

struct event {
    void (*callback)(void *, void *);
    void *arg1;
    void *arg2;
};

struct event_thread {
    bool running;
    char *name;
    void (*update_cb)(void *);
    void (*stop_cb)(void *);
    void *arg;
    struct con_queue *events;
    pthread_t tid;
};

static atomic_int g_thread_cnt;
static struct event_thread g_threads[MAX_EVENT_THREADS];

static struct event *
allocate_event(void (*callback)(void *, void *), void *arg1, void *arg2)
{
    struct event *event;

    event = calloc(1, sizeof(*event));
    if (!event) {
        fprintf(stderr, "Failed to allocate memory for event.\n");
        return NULL;
    }

    event->callback = callback;
    event->arg1 = arg1;
    event->arg2 = arg2;
    
    return event;
}

int
event_thread_enqueue_event(struct event_thread *thread, void (*callback)(void *, void *), void *arg1, void *arg2)
{
    struct event *event;
    
    if (!thread) {
        fprintf(stderr, "Trying to enqueue event to inexistent thread.\n");
        return -1;
    }
    
    event = allocate_event(callback, arg1, arg2);
    if (event == NULL) {
        fprintf(stderr, "Failed to allocate event for enqueuing.\n");
        return -1;
    }
    
    con_queue_push(thread->events, event);
    return 0;
}

static void
event_thread_destroy(struct event_thread *thread)
{
    struct event *event;
    
    while (con_queue_pop(thread->events, (void **) &event) == 0) {
        free(event);
    }
    
    free(thread->events);
    free(thread->name);
}

static void
sig_handler(int signo)
{
    /** Interrupt any blocking IO / poll */
}

static void *
event_thread_loop(void *arg)
{
    struct event_thread *thread = arg;
    struct event *event;

    if (signal(SIGUSR1, sig_handler) == SIG_ERR) {
        fprintf(stderr, "%s: Failed to bind SIGUSR1 handler.\n", thread->name);
        goto out;
    }
    
    while (thread->running) {
        if (thread->update_cb) {
            thread->update_cb(thread->arg);
        }
        
        if (con_queue_pop(thread->events, (void **) &event) == 0) {
            event->callback(event->arg1, event->arg2);
            free(event);
        }
    }

    if (thread->stop_cb) {
        thread->stop_cb(thread->arg);
    }
    
out:
    event_thread_destroy(thread);    
    pthread_exit(NULL);
    return NULL;
}

struct event_thread *
event_thread_create(const char *name, void (*update_cb)(void *), void (*stop_cb)(void *), void *arg)
{
    struct event_thread *thread;
    int thread_id;

    thread_id = atomic_fetch_add(&g_thread_cnt, 1);
    if (thread_id >= MAX_EVENT_THREADS) {
        fprintf(stderr, "Fatal: reached thread limit of %d. Can't create another thread.\n", MAX_EVENT_THREADS);
        goto name_err;
    }

    thread = &g_threads[thread_id];
    
    thread->running = true;
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
    thread->update_cb = update_cb;
    thread->stop_cb = stop_cb;
    thread->arg = arg;

    if (pthread_create(&thread->tid, NULL, event_thread_loop, thread) != 0) {
        fprintf(stderr, "Fatal: pthread_create() failed, cannot start event thread.\n");
        goto events_err;
    }
    
    return thread;
    
events_err:
    free(thread->events);  
name_err:
    free(thread->name);
err:
    return NULL;
}

static void
event_thread_stop_cb(void *arg1, void *arg2)
{
    struct event_thread *thread = arg1;
    
    thread->running = false;
}

int
event_thread_stop(struct event_thread *thread)
{
    if (!thread->running) {
        fprintf(stderr, "Thread %p is not running.\n", (void *)thread);
        return -1;
    }

    if (event_thread_enqueue_event(thread, event_thread_stop_cb, thread, NULL) != 0) {
        fprintf(stderr, "Failed to stop thread %p.\n", (void *)thread);
        return -1;
    }

    pthread_kill(thread->tid, SIGUSR1);
    return 0;
}

struct event_thread *
event_thread_self(void)
{
    struct event_thread *thread_tmp;
    int i;

    for (i = 0; i < MAX_EVENT_THREADS; ++i) {
        thread_tmp = &g_threads[i];
        if (thread_tmp->tid == pthread_self()) {
            return thread_tmp;
        }
    }

    return NULL;
}

void
event_thread_lib_init(void)
{
    atomic_init(&g_thread_cnt, 0);
}

void
event_thread_lib_wait(void)
{
    struct event_thread *thread;
    int i;

    for (i = 0; i < MAX_EVENT_THREADS; ++i) {
        thread = &g_threads[i];
        if (thread->running) {
            pthread_join(thread->tid, NULL);
        }
    }
}

static void *
event_thread_lib_shutdown_cb(void *arg)
{
    struct event_thread *thread;
    int i;

    for (i = 0; i < MAX_EVENT_THREADS; ++i) {
        thread = &g_threads[i];
        if (thread->running) {
            event_thread_stop(thread);
        }
    }
    
    return NULL;
}

void
event_thread_lib_shutdown(void)
{
    pthread_t tid;
    if (pthread_create(&tid, NULL, event_thread_lib_shutdown_cb, NULL) != 0) {
        fprintf(stderr, "Fatal: pthread_create() failed, cannot start shutdown thread.\n");
        abort();
    }
    pthread_detach(tid);
}

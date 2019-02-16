/*
 * Copyright (c) 2017 Dariusz Stojaczyk. All Rights Reserved.
 * The following source code is released under an MIT-style license,
 * that can be found in the LICENSE file.
 */

#ifndef BROTHER_LOG_H
#define BROTHER_LOG_H

#include <stdio.h>
#include <stdarg.h>

enum { LEVEL_DEBUG, LEVEL_INFO, LEVEL_WARN, LEVEL_ERR, LEVEL_FATAL };

#define LOG_DEBUG(...)  log_printf(LEVEL_DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_INFO(...)   log_printf(LEVEL_INFO,  __FILE__, __LINE__, __VA_ARGS__)
#define LOG_WARN(...)   log_printf(LEVEL_WARN,  __FILE__, __LINE__, __VA_ARGS__)
#define LOG_ERR(...)    log_printf(LEVEL_ERR,   __FILE__, __LINE__, __VA_ARGS__)
#define LOG_FATAL(...)  log_printf(LEVEL_FATAL, __FILE__, __LINE__, __VA_ARGS__)

#define DUMP_DEBUG(...) hexdump(LEVEL_DEBUG, __VA_ARGS__)
#define DUMP_ERR(...)   hexdump(LEVEL_ERR,   __VA_ARGS__)

void log_set_level(int level);
void log_printf(int level, const char *file, int line, const char *fmt, ...);
void hexdump(int level, const void *data, size_t len);

#endif //DCP_J105_LOG_H

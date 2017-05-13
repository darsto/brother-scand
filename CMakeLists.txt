cmake_minimum_required(VERSION 3.7)
project(brother)

set(CMAKE_C_STANDARD 11)

set(SOURCE_FILES main.c concurrent_queue.h concurrent_queue.c)
add_executable(brother ${SOURCE_FILES})
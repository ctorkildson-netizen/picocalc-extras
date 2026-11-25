#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
typedef uint64_t absolute_time_t;
static inline absolute_time_t get_absolute_time(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000+t.tv_nsec/1000;}
static inline absolute_time_t delayed_by_ms(absolute_time_t a,unsigned ms){return a+ms*1000ull;}
static inline absolute_time_t make_timeout_time_ms(unsigned ms){return get_absolute_time()+ms*1000ull;}
static inline bool time_reached(absolute_time_t t){return get_absolute_time()>=t;}
static inline int64_t absolute_time_diff_us(absolute_time_t a,absolute_time_t b){return (int64_t)(b-a);}
void sleep_ms(unsigned ms);
void sleep_us(unsigned us);

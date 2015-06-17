#ifndef PERF_TEST_H
#define PERF_TEST_H

// #define WALL_TIME

#ifdef WALL_TIME
#include <sys/time.h>
#endif
#include <stdint.h>

#include "qemu/atomic.h"

struct perf_timer {
    const char *name;
    uint64_t time_spent;
};

struct perf_counter {
    const char *name;
    uint64_t counter;
};

#ifndef WALL_TIME
static inline uint64_t rdtsc(void)
{
    uint32_t ret_lo, ret_hi;
    __asm__ __volatile__ ("rdtsc" : "=a"(ret_lo), "=d"(ret_hi));
    return ((uint64_t)ret_hi << 32) | ret_lo;
}
#endif

#define PERF_TIMER(tname) \
    struct perf_timer _pt ## tname __attribute__((section("perf_timers"))) = { \
        .name = #tname \
    }

#define EXTERN_PERF_TIMER(tname) \
    extern struct perf_timer _pt ## tname;

#define PERF_COUNTER(cname) \
    struct perf_counter _pc ## cname __attribute__((section("perf_counters"))) = { \
        .name = #cname \
    }

#define PERF_COUNTER_INC(cname) \
    (_pc ## cname).counter++


#ifdef WALL_TIME

#define PERF_TIMER_START(name, varname) \
    struct timeval _pt ## name ## varname; gettimeofday(&(_pt ## name ## varname), NULL)

#define PERF_TIMER_COUNTER(name, varname) \
    struct timeval _pt ## name ## varname

#define PERF_TIMER_COUNTER_START(name, varname) \
    gettimeofday(&(_pt ## name ## varname), NULL)

static inline void do_perf_timer_stop(struct perf_timer *timer,
                                      const struct timeval *start)
{
    struct timeval end;
    gettimeofday(&end, NULL);
    timer->time_spent += ((int64_t)end.tv_sec - (int64_t)start->tv_sec)
                         * 1000000
                         + (int)end.tv_usec - (int)start->tv_usec;
}

#define PERF_TIMER_STOP(name, varname) \
    do_perf_timer_stop(&(_pt ## name), &(_pt ## name ## varname))

#else

#define PERF_TIMER_START(name, varname) \
    uint64_t _pt ## name ## varname = rdtsc()

#define PERF_TIMER_DECL(name, varname) \
    uint64_t _pt ## name ## varname

#define PERF_TIMER_START_NODECL(name, varname) \
    _pt ## name ## varname = rdtsc()

#define PERF_TIMER_COUNTER(name, varname) \
    uint64_t _pt ## name ## varname

#define PERF_TIMER_COUNTER_START(name, varname) \
    _pt ## name ## varname = rdtsc()

#define PERF_TIMER_STOP(name, varname) \
    atomic_add(&(_pt ## name).time_spent, rdtsc() - (_pt ## name ## varname))

#endif

extern const struct perf_timer __start_perf_timers, __stop_perf_timers;

static inline const struct perf_timer *next_perf_timer(const struct perf_timer *prev)
{
    if (!prev) {
        return &__start_perf_timers;
    } else if (prev + 1 < &__stop_perf_timers) {
        return prev + 1;
    } else {
        return NULL;
    }
}

extern const struct perf_counter __start_perf_counters, __stop_perf_counters;

static inline const struct perf_counter *next_perf_counter(const struct perf_counter *prev)
{
    if (!prev) {
        return &__start_perf_counters;
    } else if (prev + 1 < &__stop_perf_counters) {
        return prev + 1;
    } else {
        return NULL;
    }
}

#endif

#ifndef __LRCU_USER_SPACE_DEFINES_H__
#define __LRCU_USER_SPACE_DEFINES_H__

/* print defines */

#include <stdio.h>
#include <assert.h>

#define LRCU_LOG(...) fprintf(stdout, __VA_ARGS__)
#define LRCU_ASSERT(cond) assert(cond)
#define LRCU_WARN(...) fprintf(stderr, __VA_ARGS__)

/* thread defines */

#include <pthread.h>

/* shall return NULL on success */
#define LRCU_THREAD_CREATE(tid, func, data) pthread_create(tid, NULL, func, data)
#define LRCU_THREAD_JOIN(ptid) pthread_join(*(ptid), NULL)
typedef pthread_t LRCU_THREAD_T;

#define LRCU_TLS_DEFINE(t, v) static LRCU_ALIGNED __thread t v
/* in case of pthread-only tls */
#define LRCU_TLS_INIT(x)
#define LRCU_TLS_DEINIT(x)

#define LRCU_TLS_SET(a, b) ((a) = (b))
#define LRCU_TLS_GET(a) (a)

/* misc */
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <inttypes.h>
#include <stdint.h>
#include <stdbool.h>

#define LRCU_CALLOC(a, b) calloc((a), (b))
#define LRCU_MALLOC(a) malloc(a)
#define LRCU_FREE(a) free(a)

#define LRCU_QSORT(base, num, size, cmp_func) \
		qsort((base), (num), (size), (cmp_func))

#define LRCU_USLEEP(x) usleep(x)

#define LRCU_EXPORT_SYMBOL(x)

#define LRCU_CACHE_LINE_SIZE 64
#define LRCU_ALIGNED __attribute__((__aligned__(LRCU_CACHE_LINE_SIZE)))

/* custom define in case we need some code in object  */
#define LRCU_OS_API_DEFINE

/* time */
#include <sys/time.h>

typedef struct timeval LRCU_TIMER_TYPE;
#define LRCU_TIMER_INIT(sec, usec) {.tv_sec = (sec), .tv_usec = (usec)}

#define LRCU_TIMER_ADD(a, b, c) timeradd((a), (b), (c))
#define LRCU_TIMER_GET(x) gettimeofday((x), NULL)
#define LRCU_TIMER_ISSET(x) timerisset(x)
#define LRCU_TIMER_CLEAR(x) timerclear(x)
#define LRCU_TIMER_CMP(a, b, c) timercmp((a), (b), c)

/* for testing purposes */
#define LRCU_EXIT(x) exit(x)

#endif /* __LRCU_USER_SPACE_DEFINES_H__ */
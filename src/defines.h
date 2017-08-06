#ifndef __LRCU_USER_DEFINES_H__
#define __LRCU_USER_DEFINES_H__

#define LRCU_KERNEL __KERNEL__

enum{
    LRCU_NS_DEFAULT = 0,
    /* add your own namespaces here */
    LRCU_NS_MAX, /* but no more than 255 */
};

#define LRCU_THREADS_MAX 64

#define LRCU_WORKER_SLEEP_US    50000
#define LRCU_NS_SYNC_SLEEP_US   10000
#define LRCU_HANG_TIMEOUT_S     1

#define LRCU_CACHE_LINE_SIZE 64
#define LRCU_ALIGNED __attribute__((__aligned__(LRCU_CACHE_LINE_SIZE)))

/* print defines */
#ifndef LRCU_KERNEL
#include <stdlib.h>
#include <stdio.h>
#include <libgen.h>

#define LRCU_LOG(...) printf(__VA_ARGS__)
#define LRCU_HALT() exit(1);

#define LRCU_BUG()  do{ \
        LRCU_LOG("LRCU BUG %s(%d):%s\n", basename(__FILE__), __LINE__, __FUNCTION__); \
        LRCU_HALT(); \
    }while(0)
#define LRCU_WARN(...)  do{ \
        LRCU_LOG("LRCU WARN %s(%d):%s| %s\n", \
            basename(__FILE__), __LINE__, __FUNCTION__, \
            __VA_ARGS__); \
    }while(0)
#else
#define LRCU_LOG(...) printk(KERN_NOTICE __VA_ARGS__)
#define LRCU_HALT() BUG();

#define LRCU_BUG()  BUG()
#define LRCU_WARN(...)  WARN_ON(1, __VA_ARGS__)
#endif

/* thread defines. in case we want to use it in kernel-space */
#ifndef LRCU_KERNEL
#include <pthread.h>
#define LRCU_THREAD_CREATE(...) pthread_create(__VA_ARGS__)
#define LCRU_THREAD_JOIN(...) pthread_join(__VA_ARGS__)
#define LRCU_THREAD_T pthread_t

#define LRCU_TLS_DEFINE(t, v) static LRCU_ALIGNED __thread t v
#define LRCU_TLS_INIT(x)
#define LRCU_TLS_DEINIT(x)

#define LRCU_TLS_SET(a, b) ((a) = (b))
#define LRCU_TLS_GET(a) (a)

#else
#error TODO implement
#define LRCU_TLS_DEFINE(t, v) DECLARE_PER_CPU_ALIGNED(t, v)
#endif

/* misc */
#ifndef LRCU_KERNEL
#include <sys/time.h>

#define LRCU_CALLOC(a, b) calloc((a), (b))
#define LRCU_MALLOC(a) malloc(a)
#define LRCU_FREE(a) free(a)

#define LRCU_EXPORT_SYMBOL(x)
#else
#define LRCU_EXPORT_SYMBOL(x) EXPORT_SYMBOL(x)
#endif

#endif /* __LRCU_USER_DEFINES_H__ */
/* 
	https://github.com/grumm/light-rcu
	Andrei Dubasov - andrew.dubasov@gmail.com
*/

#ifndef _LRCU_API_H
#define _LRCU_API_H
/*
    Lazy RCU: extra light read section and hard write section
    locks taken only on lrcu_ptr  
*/

/*
    lrcu_thread_info->lrcu_ptr_ns->lrcu_ptr
                                 ->lrcu_ptr
                    ->lrcu_ptr_ns->lrcu_ptr
                                 ->lrcu_ptr
    each thread has to have local version - which section thread in
    (global)lrcu_ptr's have their version. thread accesses
*/
#include "defines.h"
#include "types.h"


/***********************************************************/

typedef void (lrcu_destructor_t)(void *);
struct lrcu_namespace;
struct lrcu_handler;

struct lrcu_ptr {
    void *ptr; /* actual data behind pointer */
    lrcu_destructor_t *deinit;
    /* what about when version wraps -1? 
        at least after 143 years on 4Ghz CPU in ticks :) */
    u64 version;
    u8 ns_id;
};

struct lrcu_local_namespace {
    u64 version;
    i32 counter; /* max nesting depth 2^32 */
};

/* XXX make number of namespaces dynamic??? */
struct lrcu_thread_info{
    struct lrcu_handler *h;
    struct timeval timeval[LRCU_NS_MAX];
    struct lrcu_local_namespace lns[LRCU_NS_MAX];
    struct lrcu_local_namespace hung_lns[LRCU_NS_MAX];
};

/***********************************************************/

#define lrcu_write_barrier() lrcu_write_barrier_ns(LRCU_NS_DEFAULT)

void lrcu_write_barrier_ns(u8 ns_id);
LRCU_EXPORT_SYMBOL(lrcu_write_barrier);

/***********************************************************/

#define lrcu_write_lock() lrcu_write_lock_ns(LRCU_NS_DEFAULT)

void lrcu_write_lock_ns(u8 ns_id);
LRCU_EXPORT_SYMBOL(lrcu_write_lock_ns);

/***********************************************************/

#define lrcu_assign_pointer(p, v) __lrcu_assign_pointer(&(p), (v))

void __lrcu_assign_pointer(void **pp, void *newptr);
LRCU_EXPORT_SYMBOL(__lrcu_assign_pointer);

void lrcu_assign_ptr(struct lrcu_ptr *ptr, void *newptr,
                        u8 ns_id, lrcu_destructor_t *callback);
LRCU_EXPORT_SYMBOL(lrcu_assign_ptr);

/***********************************************************/

#define lrcu_write_unlock() lrcu_write_unlock_ns(LRCU_NS_DEFAULT)

void lrcu_write_unlock_ns(u8 ns_id);
LRCU_EXPORT_SYMBOL(lrcu_write_unlock_ns);

/***********************************************************/

#define lrcu_read_lock() lrcu_read_lock_ns(LRCU_NS_DEFAULT)

void lrcu_read_lock_ns(u8 ns_id);
LRCU_EXPORT_SYMBOL(lrcu_read_lock_ns);

/***********************************************************/

#define lrcu_dereference_pointer(p) __lrcu_dereference_pointer(&(p))

void *__lrcu_dereference_pointer(void **p);
LRCU_EXPORT_SYMBOL(__lrcu_dereference_pointer);

void *lrcu_dereference_ptr(struct lrcu_ptr *ptr);
LRCU_EXPORT_SYMBOL(lrcu_dereference_ptr);

/***********************************************************/

#define lrcu_read_unlock() lrcu_read_unlock_ns(LRCU_NS_DEFAULT)

void lrcu_read_unlock_ns(u8 ns_id);
LRCU_EXPORT_SYMBOL(lrcu_read_unlock_ns);

/***********************************************************/

#define lrcu_call(x, y) lrcu_call_ns(LRCU_NS_DEFAULT, (x), (y))

/* x - lrcu_ptr */
#define lrcu_call_ptr(x) lrcu_call_ns((x)->ns_id, (x)->ptr, (x)->deinit)

void lrcu_call_ns(u8 ns_id, void *p, lrcu_destructor_t *destr);
LRCU_EXPORT_SYMBOL(lrcu_call_ns);

/***********************************************************/

#define lrcu_synchronize() lrcu_synchronize_ns(LRCU_NS_DEFAULT)

void lrcu_synchronize_ns(u8 ns_id);
LRCU_EXPORT_SYMBOL(lrcu_synchronize_ns);

/***********************************************************/

#define lrcu_barrier() lrcu_synchronize_ns(LRCU_NS_DEFAULT)

void lrcu_barrier_ns(u8 ns_id);
LRCU_EXPORT_SYMBOL(ns_id);

/***********************************************************/

struct lrcu_handler *lrcu_init(void);

struct lrcu_handler *__lrcu_init(void);

void lrcu_deinit(void);
LRCU_EXPORT_SYMBOL(lrcu_deinit);

/***********************************************************/

struct lrcu_namespace *lrcu_ns_init(u8 id);

void lrcu_ns_deinit(u8 id);
LRCU_EXPORT_SYMBOL(lrcu_ns_deinit);

void lrcu_ns_deinit_safe(u8 id);
LRCU_EXPORT_SYMBOL(lrcu_ns_deinit_safe);

/***********************************************************/

/* same as __X, but also set thread to deafult ns */
struct lrcu_thread_info *lrcu_thread_init(void);
LRCU_EXPORT_SYMBOL(lrcu_thread_init);

struct lrcu_thread_info *__lrcu_thread_init(void);
LRCU_EXPORT_SYMBOL(__lrcu_thread_init);

/***********************************************************/

bool lrcu_thread_set_ns(u8 ns_id);
LRCU_EXPORT_SYMBOL(lrcu_thread_set_ns);

bool lrcu_thread_del_ns(u8 ns_id);
LRCU_EXPORT_SYMBOL(lrcu_thread_del_ns);

/***********************************************************/

void lrcu_thread_deinit(void);
LRCU_EXPORT_SYMBOL(lrcu_thread_deinit);

/***********************************************************/

/* already allocated ptr */
#define lrcu_ptr_init(x) __lrcu_ptr_init((x), LRCU_NS_DEFAULT, free)

void __lrcu_ptr_init(struct lrcu_ptr *ptr, u8 ns_id, 
                            lrcu_destructor_t *deinit);
LRCU_EXPORT_SYMBOL(__lrcu_ptr_init);

/***********************************************************/

//LRCU_EXPORT_SYMBOL(__lrcu_handler);
//LRCU_EXPORT_SYMBOL(__lrcu_thread_info);
//extern struct lrcu_handler *__lrcu_handler;
//extern __thread struct lrcu_thread_info *__lrcu_thread_info;

#endif
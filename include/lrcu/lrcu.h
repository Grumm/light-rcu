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

/***********************************************************/

typedef void (lrcu_destructor_t)(void *); /* no typedef forward declarations */

#include "defines.h"

struct lrcu_ptr {
    void *ptr; /* actual data behind pointer */
    lrcu_destructor_t *deinit;
    /* what about when version wraps -1? 
        u64 would last at least after 143 years on 4Ghz CPU in ticks :) */
    u64 version;
    u8 ns_id;
};

typedef struct lrcu_ptr_head {
    lrcu_list_t list;
    lrcu_destructor_t *func;
    u64 version;
    u8 ns_id; //?
} lrcu_ptr_head_t;


/***********************************************************/

#define lrcu_write_barrier() lrcu_write_barrier_ns(LRCU_NS_DEFAULT)

void lrcu_write_barrier_ns(u8 ns_id);

/***********************************************************/

#define lrcu_write_lock() lrcu_write_lock_ns(LRCU_NS_DEFAULT)

void lrcu_write_lock_ns(u8 ns_id);

/***********************************************************/

#define lrcu_write_unlock() lrcu_write_unlock_ns(LRCU_NS_DEFAULT)

void lrcu_write_unlock_ns(u8 ns_id);

/***********************************************************/

#define lrcu_read_lock() lrcu_read_lock_ns(LRCU_NS_DEFAULT)

void lrcu_read_lock_ns(u8 ns_id);

/***********************************************************/

#define lrcu_dereference(p) __lrcu_dereference((void **)&(p))

void *__lrcu_dereference(void **p);

void *lrcu_dereference_ptr(struct lrcu_ptr *ptr);

/***********************************************************/

#define lrcu_assign_pointer(p, v) __lrcu_assign_pointer_ns(LRCU_NS_DEFAULT, (void **)&(p), (v))

#define lrcu_assign_pointer_ns(ns, p, v) __lrcu_assign_pointer_ns((ns), (void **)&(p), (v))

void __lrcu_assign_pointer_ns(u8 ns_id, void **pp, void *newptr);

void lrcu_assign_ptr(struct lrcu_ptr *ptr, void *newptr);

void __lrcu_assign_ptr(struct lrcu_ptr *ptr, void *newptr);

/***********************************************************/

/* already allocated ptr */
void lrcu_ptr_init(struct lrcu_ptr *ptr, u8 ns_id, 
                            lrcu_destructor_t *deinit);

/***********************************************************/

#define lrcu_read_unlock() lrcu_read_unlock_ns(LRCU_NS_DEFAULT)

void lrcu_read_unlock_ns(u8 ns_id);

/***********************************************************/

#define lrcu_call(x, y) lrcu_call_ns(LRCU_NS_DEFAULT, (x), (y))

/* x - lrcu_ptr */
#define lrcu_call_ptr(x) lrcu_call_ns((x)->ns_id, (x)->ptr, (x)->deinit)

void lrcu_call_ns(u8 ns_id, void *p, lrcu_destructor_t *destr);

/***********************************************************/

#define lrcu_call_head(ptr, func) \
        lrcu_call_head_ns(LRCU_NS_DEFAULT, (ptr), (func))

void lrcu_call_head_ns(u8 ns_id, struct lrcu_ptr_head *head,
                                    lrcu_destructor_t *destr);

/***********************************************************/

#define lrcu_synchronize() lrcu_synchronize_ns(LRCU_NS_DEFAULT)

void lrcu_synchronize_ns(u8 ns_id);

/***********************************************************/

#define lrcu_barrier() lrcu_barrier_ns(LRCU_NS_DEFAULT)

void lrcu_barrier_ns(u8 ns_id);

/***********************************************************/

struct lrcu_handler;

struct lrcu_handler *lrcu_init(void);

struct lrcu_handler *__lrcu_init(void);

void lrcu_deinit(void);

/***********************************************************/

struct lrcu_namespace;

struct lrcu_namespace *lrcu_ns_init(u8 id);

void lrcu_ns_deinit(u8 id);

void lrcu_ns_deinit_safe(u8 id);

/***********************************************************/

/* same as __X, but also set thread to deafult ns */
struct lrcu_thread_info;

struct lrcu_thread_info *lrcu_thread_init(void);

struct lrcu_thread_info *__lrcu_thread_init(void);

/***********************************************************/

bool lrcu_thread_set_ns(u8 ns_id);

bool lrcu_thread_del_ns(u8 ns_id);

/***********************************************************/

void lrcu_thread_deinit(void);

/***********************************************************/

//extern struct lrcu_handler *__lrcu_handler;
//extern __thread struct lrcu_thread_info *__lrcu_thread_info;

#endif
#ifndef _LRCU_SIMPLE_LIST_H
#define _LRCU_SIMPLE_LIST_H

/*
    lrcu_list API:

    lrcu_list_init
    lrcu_list_add --inplace copy data with sizeof macro
    lrcu_list_del
    lrcu_list_empty
    lrcu_list_splice
    lrcu_list_for_each --first argument is a pointer to actual data in lrcu_list element, 
                    iterating by sizeof(*(p))

*/
#include "atomics.h"
#include "types.h"

struct lrcu_list;
typedef struct lrcu_list lrcu_list_t;
struct lrcu_list{
    lrcu_list_t *next;
    char data[0];
};

typedef struct lrcu_lrcu_list_head_s {
    lrcu_list_t *head;
} lrcu_list_head_t;

static inline void lrcu_list_init(lrcu_list_head_t *lh){
    lh->head = NULL;
}

static inline bool lrcu_list_empty(lrcu_list_head_t *lh){
    return lh->head == NULL;
}

static inline void lrcu_list_check_loop(lrcu_list_head_t *lh){
#ifdef LRCU_LIST_DEBUG
    lrcu_list_t *e = lh->head;
    u64 cnt = 0;
    lrcu_list_t *t = NULL;


#define __lrcu_list_check_loop_depth 100000
    while(e){
        LRCU_ASSERT(e != e->next);
        LRCU_ASSERT(e != t);
        cnt++;
        if(cnt > __lrcu_list_check_loop_depth && !t){
            t = e;
        }
        e = e->next;
    }
    LRCU_ASSERT(cnt < __lrcu_list_check_loop_depth);
#else
    (void)lh;
#endif
}

static inline lrcu_list_t *lrcu_list_get_tail(lrcu_list_head_t *lh){
    lrcu_list_t *e, *e_prev = NULL;
    e = lh->head;
    lrcu_list_check_loop(lh);
    while(e){
        e_prev = e;
        e = e->next;
    }
    return e_prev;
}

/*
    In general, these functions should be safe for traversal, atomic insert, remove
    and splice.
    But in this case, we want only make traversal, insert and splice for
    general-purpose list
*/
#ifdef LRCU_LIST_ATOMIC

static inline lrcu_list_t *lrcu_list_reset_atomic(lrcu_list_head_t *lt){
    lrcu_list_t *t, *newt;
    t = lt->head;
    for(;;){
        newt = lrcu_cmpxchg(&lt->head, t, NULL);
        if(newt == t)
            break;
        t = newt;
    }
    lrcu_list_check_loop(lt);
    return t;
}

/* lh = lt + lh, lt = 0 */
static inline void lrcu_list_splice_atomic(lrcu_list_head_t *lh, lrcu_list_head_t *lt){
    lrcu_list_t *t;

    lrcu_list_check_loop(lh);
    lrcu_list_check_loop(lt);
    if(lrcu_list_empty(lt))
        return;

    t = lrcu_list_reset_atomic(lt);
    if(lrcu_list_empty(lh)){
        lh->head = t;
    }else{
        /* merge two lists */
        /* find last element in list. XXX not sure which list is bigger */
        lrcu_list_t *e_prev = lrcu_list_get_tail(lh);
        /* e_prev is the tail of lh */
        LRCU_ASSERT(e_prev);
        if(e_prev)
            e_prev->next = t;
    }
    lrcu_list_check_loop(lh);
    lrcu_list_check_loop(lt);
}

/* p - data to store in lrcu_list */
#define lrcu_list_add_atomic(lh, p) __lrcu_list_add(lh, &(p), sizeof(p), true)

static inline void lrcu_list_insert_atomic(lrcu_list_head_t *lh, lrcu_list_t *e){
    lrcu_list_t *t, *newt;
    t = lh->head;
    for(;;){
        e->next = t;
        /* implies mb() */
        newt = lrcu_cmpxchg(&lh->head, t, e);
        if(t == newt)
            break;
        t = newt;
    }
    lrcu_list_check_loop(lh);
}

#if 0
/* I think, in general, we cannot unlink _any_ element
                    and insert to front/back atomically */
static inline void lrcu_list_unlink_next_atomic(lrcu_list_head_t *lh, lrcu_list_t *e){
    lrcu_list_t *t, *tn;
    if(e == NULL){
        for(;;){
            t = lh->head; /* t is element to be removed */
            if(t){
                tn = t->next;
            }else{
                /* someone removed us already */
                return;
            }
            /* implies mb() */
            if(lrcu_cmpxchg(&lh->head, t, tn))
                break;
        }
        t->next = NULL;
    }else{
        for(;;){
            t = e->next; /* t is element to be removed */
            if(t){
                tn = t->next;
            }else{
                /* someone removed us already */
                return;
            }
            /* implies mb() */
            if(lrcu_cmpxchg(&e->next, t, tn))
                break;
        }
        t->next = NULL;
    }
}
#endif
#endif

static inline void lrcu_list_insert(lrcu_list_head_t *lh, lrcu_list_t *e){
    if(lrcu_list_empty(lh)){ /* no elements */
        e->next = NULL;
        wmb();
        lh->head = e;
    } else { /* >=1 elem in lrcu_list */
        e->next = lh->head;
        wmb();
        lh->head = e;
    }
    lrcu_list_check_loop(lh);
}

/* p - data to store in lrcu_list */
#define lrcu_list_add(lh, p) __lrcu_list_add(lh, &(p), sizeof(p), false)
static inline lrcu_list_t *__lrcu_list_add(lrcu_list_head_t *lh,
                                void *data, size_t size, bool atomic){
    lrcu_list_t *e = LRCU_MALLOC(sizeof(lrcu_list_t) + size);
    if(!e)
        return NULL;

    memcpy(&e->data[0], data, size);
    e->next = NULL;
    if(!atomic)
        lrcu_list_insert(lh, e);
#ifdef LRCU_LIST_ATOMIC
    else
        lrcu_list_insert_atomic(lh, e);
#endif
    lrcu_list_check_loop(lh);
    return e;
}

/* if you want to use this with lrcu, you should walk it only forward */
//#define lrcu_list_unlink_data(lh, p) lrcu_list_unlink_next(lh, container_of((p), lrcu_list_t, data))
static inline void lrcu_list_unlink_next(lrcu_list_head_t *lh, lrcu_list_t *e){
    lrcu_list_t *t, *tn;
    if(e == NULL){
        /* head */
        t = lh->head;
        LRCU_ASSERT(t);
        if(t){
            tn = t->next;
            t->next = NULL;
            wmb();
            lh->head = tn;
        }
    }else{
        /* anything else */
        t = e->next;
        LRCU_ASSERT(t);
        if(t){
            lrcu_list_t *t2 = t->next;
            t->next = NULL;
            wmb();
            e->next = t2;
        }
    }
    lrcu_list_check_loop(lh);
}

/* lh = lt + lh, lt = 0 */
static inline void lrcu_list_splice(lrcu_list_head_t *lh, lrcu_list_head_t *lt){
    if(lrcu_list_empty(lt))
        return;
    if(lrcu_list_empty(lh)){
        lh->head = lt->head;
    }else{
        /* merge two lists */
        lrcu_list_t *t = lt->head;
        /* find last element in list. XXX not sure which list is bigger */
        lrcu_list_t *e_prev = lrcu_list_get_tail(lh);

        /* e_prev is the tail of lh */
        LRCU_ASSERT(e_prev);
        lrcu_list_check_loop(lh);
        if(e_prev)
            e_prev->next = t;
        else
            lh->head = lt->head;
    }
    lrcu_list_init(lt);
    lrcu_list_check_loop(lh);
    lrcu_list_check_loop(lt);
}

/* 
    if prev == null && n == null => n = head.
                    && n != null => if n != head => ....other thread added while we we in {} section
                                                    n = head
                                    if n == head => n = n->next, prev = head
    if prev != null => if prev->next == n => n = prev = n, n->next
                          prev->next != n => n = prev->next

*/
#define __lrcu_list_for_each_get___n(n, n_prev, lh) ({ \
            rmb(); \
            if((n_prev) == NULL){ \
                if((n) == NULL || ((n) != NULL && (n) != (lh)->head)){ \
                    (n) = (lh)->head; \
                }else{ \
                    (n_prev) = (lh)->head; \
                    (n) = (n)->next; \
                } \
            }else{ \
                if((n_prev)->next == (n)){ \
                    (n_prev) = (n); \
                    (n) = (n)->next; \
                } else { \
                    (n) = (n_prev)->next; \
                } \
            } \
           (n); \
        })

#define __lrcu_list_for_each(n, n_prev, lh, f, ...) \
for((n_prev) = NULL, (n) = NULL, lrcu_list_check_loop(lh); ({ \
        lrcu_list_t *__n = __lrcu_list_for_each_get___n((n), (n_prev), (lh)); \
        f(__n, ##__VA_ARGS__); \
        __n; \
    }); \
)

#define __lrcu_list_empty_macro(__n)

#define lrcu_list_for_each(n, n_prev, lh) \
            __lrcu_list_for_each(n, n_prev, lh, __lrcu_list_empty_macro)

#define __lrcu_list_get_val(__n, val) ({ \
            void *__c; \
            if(__n){ \
                __c = (__n)->data; \
                (val) = (typeof(val))(*(void **)__c); \
            }else \
                (val) = NULL; \
        })

#define lrcu_list_for_each_ptr(val, n, n_prev, lh) \
            __lrcu_list_for_each(n, n_prev, lh, __lrcu_list_get_val, val)

static inline lrcu_list_t *__lrcu_list_find_ptr_unlink(
                                                lrcu_list_head_t *lh,
                                                void *p, bool unlink){
    lrcu_list_t *n, *n_prev;
    void *val = NULL;

    lrcu_list_for_each_ptr(val, n, n_prev, lh){
        if(val == p){
            if(unlink)
                lrcu_list_unlink_next(lh, n_prev);
            return n;
        }
    }
    return NULL;
}

static inline lrcu_list_t *lrcu_list_find_ptr(lrcu_list_head_t *lh, void *p){
    return __lrcu_list_find_ptr_unlink(lh, p, false);
}

static inline lrcu_list_t *lrcu_list_find_ptr_unlink(lrcu_list_head_t *lh, void *p){
    return __lrcu_list_find_ptr_unlink(lh, p, true);
}

#endif
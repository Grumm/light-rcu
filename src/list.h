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

struct lrcu_list;
typedef struct lrcu_list lrcu_list_t;
struct lrcu_list{
    lrcu_list_t *next, *prev;
    char data[0];
};

typedef struct lrcu_lrcu_list_head_s {
    lrcu_list_t *head, *tail;
} lrcu_list_head_t;

static inline void lrcu_list_init(lrcu_list_head_t *lh){
    lh->head = lh->tail = NULL;
}

static inline bool lrcu_list_empty(lrcu_list_head_t *lh){
    return lh->head == NULL;
}

static inline void lrcu_list_insert(lrcu_list_head_t *lh, lrcu_list_t *e){
    if(lrcu_list_empty(lh)){ /* no elements */
        lh->head = e;
        lh->tail = e;
    } else { /* >=1 elem in lrcu_list */
        lrcu_list_t *t;

        t = lh->tail;
        lh->tail = e;
        e->prev = t;
        wmb();
        t->next = e;
    }
}

/* p - data to store in lrcu_list */
#define lrcu_list_add(lh, p) __lrcu_list_add(lh, &(p), sizeof(p))
static inline lrcu_list_t *__lrcu_list_add(lrcu_list_head_t *lh, void *data, size_t size){
    lrcu_list_t *e = LRCU_MALLOC(sizeof(lrcu_list_t) + size);
    if(!e)
        return NULL;

    memcpy(&e->data[0], data, size);
    e->next = NULL;
    e->prev = NULL;
    lrcu_list_insert(lh, e);
    return e;
}

/* if you want to use this with lrcu, you should walk it only forward */
//#define lrcu_list_unlink_data(lh, p) lrcu_list_unlink(lh, container_of((p), lrcu_list_t, data))
static inline void lrcu_list_unlink(lrcu_list_head_t *lh, lrcu_list_t *e){
    if(e->next)
        e->next->prev = e->prev;
    else /* we are last element */
        lh->tail = e->prev;

    wmb();
    if(e->prev)
        e->prev->next = e->next;
    else /* we are first element */
        lh->head = e->next;

    e->next = e->prev = NULL;

    return;
}

static inline void lrcu_list_splice(lrcu_list_head_t *lh, lrcu_list_head_t *lt){
    if(lrcu_list_empty(lt))
        return;
    if(lrcu_list_empty(lh)){
        *lh = *lt;
    }else{
        lh->tail->next = lt->head;
        lt->head->prev = lh->tail;

        lh->tail = lt->tail;
    }
    lrcu_list_init(lt);
}

/* e - temporary storage; n - working element, could be free'd */
#define lrcu_list_for_each_ptr(val, n, prev, lh) \
            for ((n) = (prev) = (lh)->head; \
                ((n) = (prev)) /* hack with strict aliasing */\
                && ({char *_c = (n)->data; (val) = \
                    (typeof(val))(*(void **)_c); (val);}) \
                && ({rmb(); (prev) = (prev)->next, true;}); \
                )

#define lrcu_list_for_each(n, prev, lh) \
            for ((n) = (prev) = (lh)->head; \
                ((n) = (prev)) && \
                ({rmb(); (prev) = (prev)->next, true;}); \
                )


static inline lrcu_list_t *lrcu_list_find_ptr(lrcu_list_head_t *lh, void *p){
    lrcu_list_t *n, *prev;
    void *val = NULL;

    lrcu_list_for_each_ptr(val, n, prev, lh){
        if(val == p){
            return n;
        }
    }
    return NULL;
}

#endif
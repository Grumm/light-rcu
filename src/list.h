#ifndef _LRCU_SIMPLE_LIST_H
#define _LRCU_SIMPLE_LIST_H

/*
    list API:

    list_init
    list_add --inplace copy data with sizeof macro
    list_del
    list_empty
    list_splice
    list_for_each --first argument is a pointer to actual data in list element, 
                    iterating by sizeof(*(p))

*/
#include "types.h"
#include "atomics.h"
#include <string.h>

struct list;
typedef struct list list_t;
struct list{
    list_t *next, *prev;
    char data[0];
};

typedef struct list_head {
    list_t *head, *tail;
} list_head_t;

static inline void list_init(list_head_t *lh){
    lh->head = lh->tail = NULL;
}

static inline bool list_empty(list_head_t *lh){
    return lh->head == NULL;
}

static inline void list_insert(list_head_t *lh, list_t *e){
    if(list_empty(lh)){ /* no elements */
        lh->head = e;
        lh->tail = e;
    } else { /* >=1 elem in list */
        list_t *t;

        t = lh->tail;
        lh->tail = e;
        e->prev = t;
        wmb();
        t->next = e;
    }
}

/* p - data to store in list */
#define list_add(lh, p) __list_add(lh, &(p), sizeof(p))
static inline list_t *__list_add(list_head_t *lh, void *data, size_t size){
    list_t *e = LRCU_MALLOC(sizeof(list_t) + size);
    if(!e)
        return NULL;

    memcpy(&e->data[0], data, size);
    e->next = NULL;
    e->prev = NULL;
    list_insert(lh, e);
    return e;
}

/* if you want to use this with lrcu, you should walk it only forward */
//#define list_unlink_data(lh, p) list_unlink(lh, container_of((p), list_t, data))
static inline void list_unlink(list_head_t *lh, list_t *e){
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

static inline void list_splice(list_head_t *lh, list_head_t *lt){
    if(list_empty(lt))
        return;
    if(list_empty(lh)){
        *lh = *lt;
    }else{
        lh->tail->next = lt->head;
        lt->head->prev = lh->tail;

        lh->tail = lt->tail;
    }
    list_init(lt);
}

/* e - temporary storage; n - working element, could be free'd */
#define list_for_each_ptr(val, n, prev, lh) \
            for ((n) = (prev) = (lh)->head; \
                ((n) = (prev)) /* hack with strict aliasing */\
                && ({char *_c = (n)->data; (val) = \
                    (typeof(val))(*(void **)_c); (val);}) \
                && ({rmb(); (prev) = (prev)->next, true;}); \
                )

#define list_for_each(n, prev, lh) \
            for ((n) = (prev) = (lh)->head; \
                ((n) = (prev)) && \
                ({rmb(); (prev) = (prev)->next, true;}); \
                )


static inline list_t *list_find_ptr(list_head_t *lh, void *p){
    list_t *n, *prev;
    void *val = NULL;

    list_for_each_ptr(val, n, prev, lh){
        if(val == p){
            return n;
        }
    }
    return NULL;
}

#endif
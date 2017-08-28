#ifndef _LRCU_RANGETREE_CONTAINER_H
#define _LRCU_RANGETREE_CONTAINER_H

#include <lrcu/lrcu.h>
#include "compiler.h"
#include "atomics.h"

#define BINTREE_SEARCH_SELF_IMPLEMENTED

typedef struct lrcu_range_s {
    u64 minv, maxv;
} lrcu_range_t;

typedef struct lrcu_rangetree_s{
    size_t len, capacity;
    bool sorted;
    lrcu_range_t *r;
} lrcu_rangetree_t;

enum {
    RANGE_BINTREE_OPTLEVEL_MERGE = 0,
    RANGE_BINTREE_OPTLEVEL_SQUEEZE,
    RANGE_BINTREE_OPTLEVEL_MAX = RANGE_BINTREE_OPTLEVEL_SQUEEZE,
};

#define RANGE_BINTREE_EMPTY_VALUE   ((u64)0xFFFFFFFFFFFFFFFF)

#define RANGE_BINTREE_INIT(ranges_ptr, cap) \
                    {.capacity = (cap), .r = (ranges_ptr)}

lrcu_rangetree_t *lrcu_rangetree_init(size_t capacity);
void lrcu_rangetree_deinit(lrcu_rangetree_t *rbt);
void lrcu_rangetree_add(lrcu_rangetree_t *rbt, u64 minv, u64 maxv);
void lrcu_rangetree_print(lrcu_rangetree_t *rbt);
bool lrcu_rangetree_find(lrcu_rangetree_t *rbt, u64 value);
/* true if rbt->len is changed */
bool lrcu_rangetree_optimize(lrcu_rangetree_t *rbt, int opt_level);
u64 lrcu_rangetree_getmin(lrcu_rangetree_t *rbt);

/* 
    What we want to have:
    struct bintree_range_data v[100];
    struct bintree_range b = BINTREE_STATIC_INIT(v, 100);

    f(){
        for(;;)
            add_range(&b, minv, maxv);

        find_range(&b, ptr->version);

    }
    -----------------------------------
    add_range(b, minv, maxv){
        -- add [minv, maxv] to ranges, merge with existing if overlaps
        -- if ranges more than max, merge existing ranges, minimizing gaps between ranges
    }
    find_range(b, v){
        -- binary search for v in ranges. result would be either ai, bi, or (xi, yj)
        -- b = {[a1, b1], [a2, b2], [a3, b3], ..., [an, bn]}
    }

 */


#endif /* _LRCU_RANGETREE_CONTAINER_H */
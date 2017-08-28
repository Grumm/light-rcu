#include "range.h"

lrcu_rangetree_t *lrcu_rangetree_init(size_t capacity){
    lrcu_rangetree_t *rbt;
    size_t mem_size;

    mem_size = capacity * sizeof(lrcu_range_t) + sizeof(lrcu_rangetree_t);
    rbt = LRCU_CALLOC(1, mem_size);
    if(rbt == NULL)
        return NULL;

    rbt->capacity = capacity;
    rbt->r = (lrcu_range_t *)((char *)rbt + sizeof(lrcu_rangetree_t));
    return rbt;
}

void lrcu_rangetree_deinit(lrcu_rangetree_t *rbt){
    LRCU_FREE(rbt);
}

static inline int cmp_ranges(const void *p1, const void *p2){
    const lrcu_range_t *r1 = (const lrcu_range_t *)p1;
    const lrcu_range_t *r2 = (const lrcu_range_t *)p2;

    if(r1->minv > r2->minv)
        return +1;
    if(r1->minv < r2->minv)
        return -1;
    if(r1->minv == r2->minv){
        if(r1->maxv == r2->maxv)
            return 0;
        if(r1->maxv > r2->maxv)
            return +1;
        if(r1->maxv < r2->maxv)
            return -1;
    }
    return 0;
}

/* remove overlapping segments. rbt has to be sorted with cmp_ranges() */
static inline void lrcu_rangetree_remove_overlapping(lrcu_rangetree_t *rbt){
    size_t i, j;
    size_t real_index, real_offset;

    LRCU_ASSERT(rbt->sorted);
    for(i = 0, real_index = 0, real_offset = 0;
                            i < rbt->len; i++, real_index++){
        lrcu_range_t *r_outer = &rbt->r[i];
        lrcu_range_t *r_real = &rbt->r[real_index];
        lrcu_range_t *r_inner = NULL;
        u64 end_of_segment = r_outer->maxv;
        size_t delta_offset;

        LRCU_ASSERT(r_outer->minv != RANGE_BINTREE_EMPTY_VALUE);
        for(j = i + 1; j < rbt->len; j++){
            r_inner = &rbt->r[j];
            if(end_of_segment < r_inner->minv)
                break;
            /* merge */
            if(r_inner->maxv > end_of_segment)
                end_of_segment = r_inner->maxv;
            /* remove r_innter */
            //r_inner->minv = RANGE_BINTREE_EMPTY_VALUE;
            //r_inner->maxv = RANGE_BINTREE_EMPTY_VALUE;
        }
        r_outer->maxv = end_of_segment;
        if(real_offset)
            memcpy(r_real, r_outer, sizeof(lrcu_range_t)); 
        /* only moving single element, so no overlapping,
        so using memcpy instead of memmove */

        delta_offset = j - 1 - i;
        real_offset += delta_offset;
        i = j - 1;
    }
    rbt->len = real_index;
}

/* squeeze close segments. rbt has to be sorted with cmp_ranges() */
static inline void lrcu_rangetree_squeeze(lrcu_rangetree_t *rbt){
    ssize_t i;
    u64 v_delta;
    u64 delta_min = (u64)-1;
    ssize_t index_squeeze = rbt->len - 2;
    lrcu_range_t *r;

    LRCU_ASSERT(rbt->sorted);
    LRCU_ASSERT(rbt->len > 1);
    for(i = rbt->len - 2; i >= 0; i--){
        lrcu_range_t *r_next;
        if(i == 0)
            break;

        r = &rbt->r[i];
        r_next = &rbt->r[i + 1];
        v_delta = r_next->minv - r->maxv;
        if(v_delta == 1){
            /* do stuff */
            index_squeeze = i;
            goto out_squeeze;
        }
        if(v_delta < delta_min){
            delta_min = v_delta;
            index_squeeze = i;
        }
    }
out_squeeze:
    r = &rbt->r[index_squeeze];
    r[0].maxv = r[1].maxv;
    memmove(r + 1, r + 2, sizeof(lrcu_range_t) * (rbt->len - index_squeeze - 1));
    rbt->len--;
}

/* true if rbt->len is changed */
bool lrcu_rangetree_optimize(lrcu_rangetree_t *rbt, int opt_level){
    
    LRCU_ASSERT(opt_level <= RANGE_BINTREE_OPTLEVEL_MAX);

    if(opt_level == RANGE_BINTREE_OPTLEVEL_MERGE){
        if(!rbt->sorted){
            LRCU_QSORT(rbt->r, rbt->len, sizeof(lrcu_range_t), cmp_ranges);
            rbt->sorted = true;
        }

        lrcu_rangetree_remove_overlapping(rbt);
    }
    if(opt_level == RANGE_BINTREE_OPTLEVEL_SQUEEZE){
        if(!rbt->sorted){
            LRCU_QSORT(rbt->r, rbt->len, sizeof(lrcu_range_t), cmp_ranges);
            rbt->sorted = true;
        }

        lrcu_rangetree_squeeze(rbt);
    }

    return rbt->len != rbt->capacity;
}

u64 lrcu_rangetree_getmin(lrcu_rangetree_t *rbt){
    LRCU_ASSERT(rbt->sorted);
    if(!rbt->sorted)
        return 0;

    if(rbt->len == 0)
        return 0;

    return rbt->r[0].minv;
}

void lrcu_rangetree_add(lrcu_rangetree_t *rbt, u64 minv, u64 maxv){
    int opt_level = RANGE_BINTREE_OPTLEVEL_MERGE;

retry:
    if(likely(rbt->len < rbt->capacity)){
        rbt->r[rbt->len].minv = minv;
        rbt->r[rbt->len].maxv = maxv;
        rbt->len++;
        rbt->sorted = false;
        return;
    }
    LRCU_ASSERT(opt_level <= RANGE_BINTREE_OPTLEVEL_MAX);
    /* find closest range */
    lrcu_rangetree_optimize(rbt, opt_level++);
    goto retry;
}

void lrcu_rangetree_print(lrcu_rangetree_t *rbt){
    size_t i;

    LRCU_LOG("lrcu_rangetree: len=%zu capacity=%zu\n", rbt->len, rbt->capacity);
    for(i = 0; i < rbt->len; i++){
        LRCU_LOG("[%"PRIu64",%"PRIu64"]", rbt->r[i].minv, rbt->r[i].maxv);
    }
    LRCU_LOG("\n");
    return;
}

#ifdef BINTREE_SEARCH_SELF_IMPLEMENTED
static inline int bintree_range_value_cmp(lrcu_range_t *r, u64 value){
    if(r->minv > value)
        return -1;
    if(r->maxv < value)
        return +1;
    return 0;
}

/* returns index in which value contains, -1 otherwise */
static inline ssize_t bintree_range_search(lrcu_range_t *r, size_t len, u64 value){
    ssize_t low, high, mid;

    low = 0;
    high = (ssize_t)len - 1;
    do{
        int range_cmp;

        mid = (low + high) / 2;
        range_cmp = bintree_range_value_cmp(&r[mid], value);
        //LRCU_LOG("%lu %ld %ld %ld, %d\n", len, low, high, mid, range_cmp);
        if(range_cmp == 0)
            return mid;
       if(range_cmp < 0)
            high = mid - 1;
       if(range_cmp > 0)
            low = mid + 1;
    }while(low <= high);
    return -1;
}
#else
static inline int bintree_range_value_cmp2(lrcu_range_t *v, lrcu_range_t *r){
    if(r->minv > v->minv)
        return -1;
    if(r->maxv < v->minv)
        return +1;
    return 0;
}
#endif

bool lrcu_rangetree_find(lrcu_rangetree_t *rbt, u64 value){
#ifdef BINTREE_SEARCH_SELF_IMPLEMENTED
    return (bintree_range_search(rbt->r, rbt->len, value) != -1);
#else
    lrcu_range_t value_range = {.minv = value};
    return (bsearch(&value_range, rbt->r, rbt->len, 
            sizeof(lrcu_range_t), bintree_range_value_cmp2) != NULL);
#endif
}

/*
LRCU_EXPORT_SYMBOL(lrcu_rangetree_init);
LRCU_EXPORT_SYMBOL(lrcu_rangetree_deinit);
LRCU_EXPORT_SYMBOL(lrcu_rangetree_add);
LRCU_EXPORT_SYMBOL(lrcu_rangetree_print);
LRCU_EXPORT_SYMBOL(lrcu_rangetree_find);
LRCU_EXPORT_SYMBOL(lrcu_rangetree_optimize);
LRCU_EXPORT_SYMBOL(lrcu_rangetree_getmin);
*/
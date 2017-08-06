#ifndef _LRCU_BINTREE_CONTAINER_H
#define _LRCU_BINTREE_CONTAINER_H

struct range{
    u64 minv, maxv;
};

struct range_bintree{
    size_t len, capacity;
    bool sorted;
    struct range *r;
};

enum {
    RANGE_BINTREE_OPTLEVEL_MERGE = 0,
    RANGE_BINTREE_OPTLEVEL_SQUEEZE,
    RANGE_BINTREE_OPTLEVEL_MAX = RANGE_BINTREE_OPTLEVEL_SQUEEZE,
};

#define RANGE_BINTREE_EMPTY_VALUE   ((u64)0xFFFFFFFFFFFFFFFF)

#define RANGE_BINTREE_INIT(ranges_ptr, cap) \
                    {.capacity = (cap), .r = (ranges_ptr)}

static inline void range_bintree_print(struct range_bintree *rbt);

static inline struct range_bintree *range_bintree_init(size_t capacity){
    struct range_bintree *rbt;
    size_t mem_size;

    mem_size = capacity * sizeof(struct range) + sizeof(struct range_bintree);
    rbt = calloc(1, mem_size);
    if(rbt == NULL)
        return NULL;

    rbt->capacity = capacity;
    rbt->r = (struct range *)((char *)rbt + sizeof(struct range_bintree));
    return rbt;
}

static inline void range_bintree_deinit(struct range_bintree *rbt){
    LRCU_FREE(rbt);
}

static inline int cmp_ranges(const void *p1, const void *p2){
    const struct range *r1 = (const struct range *)p1;
    const struct range *r2 = (const struct range *)p2;

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
static inline void range_bintree_remove_overlapping(struct range_bintree *rbt){
    size_t i, j;
    size_t real_index, real_offset;

    assert(rbt->sorted);
    for(i = 0, real_index = 0, real_offset = 0;
                            i < rbt->len; i++, real_index++){
        struct range *r_outer = &rbt->r[i];
        struct range *r_real = &rbt->r[real_index];
        struct range *r_inner = NULL;
        u64 end_of_segment = r_outer->maxv;
        size_t delta_offset;

        assert(r_outer->minv != RANGE_BINTREE_EMPTY_VALUE);
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
            memcpy(r_real, r_outer, sizeof(struct range)); 
        /* only moving single element, so no overlapping,
        so using memcpy instead of memmove */

        delta_offset = j - 1 - i;
        real_offset += delta_offset;
        i = j - 1;
    }
    rbt->len = real_index;
}

/* squeeze close segments. rbt has to be sorted with cmp_ranges() */
static inline void range_bintree_squeeze(struct range_bintree *rbt){
    ssize_t i;
    u64 v_delta;
    u64 delta_min = (u64)-1;
    ssize_t index_squeeze = rbt->len - 2;
    struct range *r;

    assert(rbt->sorted);
    assert(rbt->len > 1);
    for(i = rbt->len - 2; i >= 0; i--){
        struct range *r_next;
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
    memmove(r + 1, r + 2, sizeof(struct range) * (rbt->len - index_squeeze - 1));
    rbt->len--;
}

/* true if rbt->len is changed */
static inline bool range_bintree_optimize(struct range_bintree *rbt, int opt_level){
    
    assert(opt_level <= RANGE_BINTREE_OPTLEVEL_MAX);

    if(opt_level == RANGE_BINTREE_OPTLEVEL_MERGE){
        if(!rbt->sorted){
            qsort(rbt->r, rbt->len, sizeof(struct range), cmp_ranges);
            rbt->sorted = true;
        }

        range_bintree_remove_overlapping(rbt);
    }
    if(opt_level == RANGE_BINTREE_OPTLEVEL_SQUEEZE){
        if(!rbt->sorted){
            qsort(rbt->r, rbt->len, sizeof(struct range), cmp_ranges);
            rbt->sorted = true;
        }

        range_bintree_squeeze(rbt);
    }

    return rbt->len != rbt->capacity;
}

static inline u64 range_bintree_getmin(struct range_bintree *rbt){
    assert(rbt->sorted);
    if(!rbt->sorted)
        return 0;

    if(rbt->len == 0)
        return 0;

    return rbt->r[0].minv;
}

static inline void range_bintree_add(struct range_bintree *rbt, u64 minv, u64 maxv){
    int opt_level = RANGE_BINTREE_OPTLEVEL_MERGE;

retry:
    if(likely(rbt->len < rbt->capacity)){
        rbt->r[rbt->len].minv = minv;
        rbt->r[rbt->len].maxv = maxv;
        rbt->len++;
        rbt->sorted = false;
        return;
    }
    assert(opt_level <= RANGE_BINTREE_OPTLEVEL_MAX);
    /* find closest range */
    range_bintree_optimize(rbt, opt_level++);
    goto retry;
}

static inline void range_bintree_print(struct range_bintree *rbt){
    size_t i;

    LRCU_LOG("range_bintree: len=%u capacity=%u\n", rbt->len, rbt->capacity);
    for(i = 0; i < rbt->len; i++){
        LRCU_LOG("[%"PRIu64",%"PRIu64"]", rbt->r[i].minv, rbt->r[i].maxv);
    }
    LRCU_LOG("\n");
    return;
}

#ifdef BINTREE_SEARCH_SELF_IMPLEMENTED
static inline int bintree_range_value_cmp(struct range *r, u64 value){
    if(r->minv > value)
        return -1;
    if(r->maxv < value)
        return +1;
    return 0;
}

/* returns index in which value contains, -1 otherwise */
static inline ssize_t bintree_range_search(struct range *r, size_t len, u64 value){
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
static inline int bintree_range_value_cmp2(struct range *v, struct range *r){
    if(r->minv > v->minv)
        return -1;
    if(r->maxv < v->minv)
        return +1;
    return 0;
}
#endif

static inline bool range_bintree_find(struct range_bintree *rbt, u64 value){
#ifdef BINTREE_SEARCH_SELF_IMPLEMENTED
    return (bintree_range_search(rbt->r, rbt->len, value) != -1);
#else
    struct range value_range = {.minv = value};
    return (bsearch(&value_range, rbt->r, rbt->len, 
            sizeof(struct range), bintree_range_value_cmp2) != NULL);
#endif
}

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


#endif /* _LRCU_BINTREE_CONTAINER_H */
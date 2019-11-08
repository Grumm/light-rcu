#include <lrcu/lrcu.h>

#if 0
lrcu_list_head_t
lrcu_list_t

static inline lrcu_list_t *lrcu_list_get_tail(lrcu_list_head_t *lh)

/*
    In general, these functions should be safe for traversal, atomic insert, remove
    and splice.
    But in this case, we want only make traversal, insert and splice for
    general-purpose list
*/

static inline lrcu_list_t *lrcu_list_reset_atomic(lrcu_list_head_t *lt)

/* lh = lt + lh, lt = 0 */
static inline void lrcu_list_splice_atomic(lrcu_list_head_t *lh, lrcu_list_head_t *lt)

/* p - data to store in lrcu_list */
#define lrcu_list_add_atomic(lh, p) __lrcu_list_add(lh, &(p), sizeof(p), true)

static inline void lrcu_list_insert_atomic(lrcu_list_head_t *lh, lrcu_list_t *e)

static inline void lrcu_list_insert(lrcu_list_head_t *lh, lrcu_list_t *e)

/* p - data to store in lrcu_list */
#define lrcu_list_add(lh, p) __lrcu_list_add(lh, &(p), sizeof(p), false)
static inline lrcu_list_t *__lrcu_list_add(lrcu_list_head_t *lh,
                                void *data, size_t size, bool atomic)

/* if you want to use this with lrcu, you should walk it only forward */
//#define lrcu_list_unlink_data(lh, p) lrcu_list_unlink_next(lh, container_of((p), lrcu_list_t, data))
static inline void lrcu_list_unlink_next(lrcu_list_head_t *lh, lrcu_list_t *e)

/* lh = lt + lh, lt = 0 */
static inline void lrcu_list_splice(lrcu_list_head_t *lh, lrcu_list_head_t *lt)


#define lrcu_list_for_each(n, n_prev, lh) 
#define lrcu_list_for_each_ptr(val, n, n_prev, lh) 

static inline lrcu_list_t *lrcu_list_find_ptr(lrcu_list_head_t *lh, void *p)

static inline lrcu_list_t *lrcu_list_find_ptr_unlink(lrcu_list_head_t *lh, void *p)
#endif

#define DO_MATCH_FIND_PTR(lh, var, val) do{ \
        (var) = (val); \
        LRCU_ASSERT(*(uintptr_t *)lrcu_list_find_ptr(&(lh), (void *)(var))->data == (var)); \
    }while(0)
#define DO_MISMATCH_FIND_PTR(lh, var, val) do{ \
        (var) = (val); \
        LRCU_ASSERT(lrcu_list_find_ptr(&(lh), (void *)(var)) == NULL); \
    }while(0)

void unlink_some(uintptr_t u1, uintptr_t u2){
    lrcu_list_head_t lh = {0};
    lrcu_list_t *n, *n_prev;
    uintptr_t d;
    void *dptr;
    size_t cnt;

    d = 5;
    lrcu_list_add(&lh, d);
    d = 4;
    lrcu_list_add(&lh, d);
    d = 3;
    lrcu_list_add(&lh, d);
    d = 2;
    lrcu_list_add(&lh, d);
    d = 1;
    lrcu_list_add(&lh, d);

    cnt = 0;
    lrcu_list_for_each(n, n_prev, &lh){
        uintptr_t *nd = (uintptr_t *)n->data;
        cnt++;
        if(*nd == u1){
            lrcu_list_unlink_next(&lh, n_prev);
            LRCU_FREE(n);
        }else if(*nd == u2){
            lrcu_list_unlink_next(&lh, n_prev);
            LRCU_FREE(n);
        }
    }
    LRCU_ASSERT(cnt == 5);

    bool has1 = false;
    bool has2 = false;
    bool has3 = false;
    bool has4 = false;
    bool has5 = false;

#define CHECK_X(v, x) (((x) == u1 || (x) == u2) ? false : (x) == (v))

    cnt = 0;
    lrcu_list_for_each_ptr(dptr, n, n_prev, &lh){
        cnt++;
        LRCU_ASSERT(cnt <= 3);
        if(CHECK_X((uintptr_t)dptr, 1)){
            LRCU_ASSERT(!has1);
            has1 = true;
        }else if(CHECK_X((uintptr_t)dptr, 2)){
            LRCU_ASSERT(!has2);
            has2 = true;
        }else if(CHECK_X((uintptr_t)dptr, 3)){
            LRCU_ASSERT(!has3);
            has3 = true;
        }else if(CHECK_X((uintptr_t)dptr, 4)){
            LRCU_ASSERT(!has4);
            has4 = true;
        }else if(CHECK_X((uintptr_t)dptr, 5)){
            LRCU_ASSERT(!has5);
            has5 = true;
        }else 
            LRCU_ASSERT(false);
    }
    LRCU_ASSERT(cnt == 3);
}

int main(void){
    lrcu_list_head_t lh = {0};
    lrcu_list_head_t lh2 = {0};
    lrcu_list_t *n, *n_prev;
    uintptr_t d;
    void *dptr;
    size_t cnt;

    /* add 1 element and splice */
    d = 1;
    lrcu_list_add(&lh, d);
    cnt = 0;
    lrcu_list_for_each(n, n_prev, &lh){
        uintptr_t *nd = (uintptr_t *)n->data;
        cnt++;
        LRCU_ASSERT(cnt == 1);
        LRCU_ASSERT(*nd == 1);
    }
    cnt = 0;
    lrcu_list_for_each_ptr(dptr, n, n_prev, &lh){
        cnt++;
        LRCU_ASSERT(cnt == 1);
        LRCU_ASSERT((uintptr_t)dptr == 1);
    }

    DO_MATCH_FIND_PTR(lh, d, 1);
    DO_MISMATCH_FIND_PTR(lh, d, 2);

    lrcu_list_splice(&lh2, &lh);
    
    lrcu_list_for_each(n, n_prev, &lh){
        LRCU_ASSERT(false);
    }
    lrcu_list_for_each_ptr(dptr, n, n_prev, &lh){
        LRCU_ASSERT(false);
    }

    cnt = 0;
    lrcu_list_for_each(n, n_prev, &lh2){
        uintptr_t *nd = (uintptr_t *)n->data;
        cnt++;
        LRCU_ASSERT(cnt == 1);
        LRCU_ASSERT(*nd == 1);
    }
    cnt = 0;
    lrcu_list_for_each_ptr(dptr, n, n_prev, &lh2){
        cnt++;
        LRCU_ASSERT(cnt == 1);
        LRCU_ASSERT((uintptr_t)dptr == 1);
    }
    d = 1;
    LRCU_ASSERT(*(uintptr_t *)lrcu_list_find_ptr(&lh2, (void *)d)->data == d);
    /***************************/
    /* add another element an splice with other */
    d = 2;
    lrcu_list_add(&lh, d);
    d = 3;
    lrcu_list_add(&lh, d);
    d = 4;
    lrcu_list_add(&lh2, d);

    cnt = 0;
    lrcu_list_for_each(n, n_prev, &lh){
        uintptr_t *nd = (uintptr_t *)n->data;
        cnt++;
        LRCU_ASSERT(cnt <= 2);
        if(*nd == 2)
            *nd = 5;
        else if(*nd == 3)
            *nd = 6;
        else
            LRCU_ASSERT(false);
    }
    LRCU_ASSERT(cnt == 2);
    DO_MATCH_FIND_PTR(lh, d, 5);
    DO_MATCH_FIND_PTR(lh, d, 6);
    DO_MISMATCH_FIND_PTR(lh, d, 1);
    DO_MISMATCH_FIND_PTR(lh, d, 2);
    DO_MISMATCH_FIND_PTR(lh, d, 3);
    DO_MISMATCH_FIND_PTR(lh, d, 4);

    bool has5 = false, has6 = false;
    cnt = 0;
    lrcu_list_for_each_ptr(dptr, n, n_prev, &lh){
        cnt++;
        LRCU_ASSERT(cnt <= 2);
        if((uintptr_t)dptr == 5)
            has5 = true;
        else if((uintptr_t)dptr == 6)
            has6 = true;
        else
            LRCU_ASSERT(false);
    }
    LRCU_ASSERT(cnt == 2);
    LRCU_ASSERT(has5);
    LRCU_ASSERT(has6);

    DO_MATCH_FIND_PTR(lh2, d, 4);
    DO_MATCH_FIND_PTR(lh2, d, 1);
    DO_MISMATCH_FIND_PTR(lh, d, 3);

    lrcu_list_splice(&lh2, &lh);
    lrcu_list_for_each(n, n_prev, &lh){
        LRCU_ASSERT(false);
    }
    lrcu_list_for_each_ptr(dptr, n, n_prev, &lh){
        LRCU_ASSERT(false);
    }

    cnt = 0;
    lrcu_list_for_each(n, n_prev, &lh2){
        uintptr_t *nd = (uintptr_t *)n->data;
        cnt++;
        LRCU_ASSERT(cnt <= 4);
        if(*nd == 1)
            *nd = 7;
        else if(*nd == 4)
            *nd = 8;
        else if(*nd == 5)
            *nd = 9;
        else if(*nd == 6)
            *nd = 10;
        else
            LRCU_ASSERT(false);
    }
    LRCU_ASSERT(cnt == 4);
    bool has7 = false, has8 = false, has9 = false, has10 = false;
    cnt = 0;
    lrcu_list_for_each_ptr(dptr, n, n_prev, &lh2){
        cnt++;
        LRCU_ASSERT(cnt <= 4);
        if((uintptr_t)dptr == 7){
            LRCU_ASSERT(!has7);
            has7 = true;
        }else if((uintptr_t)dptr == 8){
            LRCU_ASSERT(!has8);
            has8 = true;
        }else if((uintptr_t)dptr == 9){
            LRCU_ASSERT(!has9);
            has9 = true;
        }else if((uintptr_t)dptr == 10){
            LRCU_ASSERT(!has10);
            has10 = true;
        }else
            LRCU_ASSERT(false);
    }
    LRCU_ASSERT(cnt == 4);
    LRCU_ASSERT(has7);
    LRCU_ASSERT(has8);
    LRCU_ASSERT(has9);
    LRCU_ASSERT(has10);

    DO_MATCH_FIND_PTR(lh2, d, 7);
    DO_MATCH_FIND_PTR(lh2, d, 8);
    DO_MATCH_FIND_PTR(lh2, d, 9);
    DO_MATCH_FIND_PTR(lh2, d, 10);
    DO_MISMATCH_FIND_PTR(lh2, d, 1);
    DO_MISMATCH_FIND_PTR(lh2, d, 2);
    DO_MISMATCH_FIND_PTR(lh2, d, 3);
    DO_MISMATCH_FIND_PTR(lh2, d, 4);
    DO_MISMATCH_FIND_PTR(lh2, d, 5);
    DO_MISMATCH_FIND_PTR(lh2, d, 6);
    DO_MISMATCH_FIND_PTR(lh2, d, 11);

    /* unlink multiple combinations */
    unlink_some(1, 2);
    unlink_some(1, 3);
    unlink_some(1, 4);
    unlink_some(1, 5);
    unlink_some(2, 3);
    unlink_some(2, 4);
    unlink_some(2, 5);
    unlink_some(3, 4);
    unlink_some(4, 5);

    /* TODO free data. deallocate data. atomic list. insert */

    return 0;
}
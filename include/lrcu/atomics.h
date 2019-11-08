#ifndef _LRCU_ATOMICS_H
#define _LRCU_ATOMICS_H

#define lrcu_atomic_xadd(P, V) __sync_fetch_and_add((P), (V))
#define lrcu_cmpxchg(P, O, N) __sync_val_compare_and_swap((P), (O), (N))
#define lrcu_atomic_inc(P) __sync_add_and_fetch((P), 1)
#define lrcu_atomic_dec(P) __sync_add_and_fetch((P), -1) 
#define lrcu_atomic_add(P, V) __sync_add_and_fetch((P), (V))
#define lrcu_atomic_set_bit(P, V) __sync_or_and_fetch((P), 1<<(V))
#define lrcu_atomic_clear_bit(P, V) __sync_and_and_fetch((P), ~(1<<(V)))

#endif
#ifndef _LRCU_SPINLOCK_H
#define _LRCU_SPINLOCK_H

#include <lrcu/lrcu.h>
#include <lrcu/atomics.h>
#include <lrcu/compiler.h>

#define LRCU_SPINLOCK_INITIALIZER {0, 0};

typedef union lrcu_spinlock_s {
    u32 u;
    struct {
        u16 ticket;
        u16 users;
    } s;
} lrcu_spinlock_t;

void lrcu_spin_lock(lrcu_spinlock_t *t);
void lrcu_spin_unlock(lrcu_spinlock_t *t);
int lrcu_spin_trylock(lrcu_spinlock_t *t);
int lrcu_spin_lockable(lrcu_spinlock_t *t);

#endif /* _LRCU_SPINLOCK_H */
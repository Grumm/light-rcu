#include "spinlock.h"

/* https://github.com/cyfdecyf/spinlock/blob/master/spinlock-ticket.h */
/* Code copied from http://locklessinc.com/articles/locks/ */

void lrcu_spin_lock(lrcu_spinlock_t *t){
    unsigned short me;

    LRCU_PREEMPT_DISABLE();
    me = lrcu_atomic_xadd(&t->s.users, 1);    
    for(;;){
        if(t->s.ticket == me)
            break;

        LRCU_PREEMPT_ENABLE();
        cpu_relax();
        LRCU_PREEMPT_DISABLE();
    }
}

void lrcu_spin_unlock(lrcu_spinlock_t *t){
    barrier();
    t->s.ticket++;
    LRCU_PREEMPT_ENABLE();
}

int lrcu_spin_trylock(lrcu_spinlock_t *t){
    unsigned short me = t->s.users;
    unsigned short menew = me + 1;
    unsigned cmp = ((unsigned) me << 16) + me;
    unsigned cmpnew = ((unsigned) menew << 16) + me;

    LRCU_PREEMPT_DISABLE();
    if (lrcu_cmpxchg(&t->u, cmp, cmpnew) == cmp)
        return 0;
    
    LRCU_PREEMPT_ENABLE();
    return 1; // Busy
}

int lrcu_spin_lockable(lrcu_spinlock_t *t){
    lrcu_spinlock_t u = *t;
    barrier();
    return (u.s.ticket == u.s.users);
}

/*
LRCU_EXPORT_SYMBOL(lrcu_spin_lock);
LRCU_EXPORT_SYMBOL(lrcu_spin_unlock);
LRCU_EXPORT_SYMBOL(lrcu_spin_trylock);
LRCU_EXPORT_SYMBOL(lrcu_spin_lockable);
*/
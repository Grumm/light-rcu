#ifndef _LRCU_ATOMICS_H
#define _LRCU_ATOMICS_H

/* https://github.com/cyfdecyf/spinlock/blob/master/spinlock-ticket.h */
/* Code copied from http://locklessinc.com/articles/locks/ */

#define atomic_xadd(P, V) __sync_fetch_and_add((P), (V))
#define cmpxchg(P, O, N) __sync_val_compare_and_swap((P), (O), (N))
#define atomic_inc(P) __sync_add_and_fetch((P), 1)
#define atomic_dec(P) __sync_add_and_fetch((P), -1) 
#define atomic_add(P, V) __sync_add_and_fetch((P), (V))
#define atomic_set_bit(P, V) __sync_or_and_fetch((P), 1<<(V))
#define atomic_clear_bit(P, V) __sync_and_and_fetch((P), ~(1<<(V)))


/* XXX ifdef x86_64 */
#define cpu_relax() asm volatile("pause\n": : :"memory")

#define barrier() asm volatile("": : :"memory")
#define likely(x)      __builtin_expect(!!(x), 1)
#define unlikely(x)    __builtin_expect(!!(x), 0)
#define smp_mb() __sync_synchronize()



#define spin_lock ticket_lock
#define spin_unlock ticket_unlock
#define spin_trylock ticket_trylock
#define spin_lockable ticket_lockable
#define spinlock_t ticketlock

#define SPINLOCK_INITIALIZER { 0, 0 };

typedef union ticketlock ticketlock;

union ticketlock
{
	u32 u;
	struct
	{
		u16 ticket;
		u16 users;
	} s;
};

static inline void ticket_lock(ticketlock *t)
{
	unsigned short me = atomic_xadd(&t->s.users, 1);
	
	while (t->s.ticket != me)
		cpu_relax();
}

static inline void ticket_unlock(ticketlock *t)
{
	barrier();
	t->s.ticket++;
}

static inline int ticket_trylock(ticketlock *t)
{
	unsigned short me = t->s.users;
	unsigned short menew = me + 1;
	unsigned cmp = ((unsigned) me << 16) + me;
	unsigned cmpnew = ((unsigned) menew << 16) + me;

	if (cmpxchg(&t->u, cmp, cmpnew) == cmp)
		return 0;
	
	return 1; // Busy
}

static inline int ticket_lockable(ticketlock *t)
{
	ticketlock u = *t;
	barrier();
	return (u.s.ticket == u.s.users);
}

#endif
#ifndef _LRCU_COMPILER_H
#define _LRCU_COMPILER_H

#include <lrcu/defines.h>

#ifdef LRCU_USER

#include <stddef.h>

#define barrier() asm volatile("": : :"memory")

/* http://nadeausoftware.com/articles/2012/02/c_c_tip_how_detect_processor_type_using_compiler_predefined_macros */
#if defined(__i386__) || defined(_M_IX86) || defined(__x86_64__) || defined(_M_AMD64)
#define cpu_relax() asm volatile("pause\n": : :"memory")
#define mb()    asm volatile("mfence":::"memory")
#define rmb()   asm volatile("lfence":::"memory")
#define wmb()   asm volatile("sfence" ::: "memory")
#define read_barrier_depends()
#else
#define cpu_relax() barrier()
#define mb()    __sync_synchronize()
#define rmb()   __sync_synchronize()
#define wmb()   __sync_synchronize()
#define read_barrier_depends() __sync_synchronize()
#endif

/*
 * container_of - cast a member of a structure out to the containing structure
 * @ptr:        the pointer to the member.
 * @type:       the type of the container struct this is embedded in.
 * @member:     the name of the member within the struct.
 *
 */
#define container_of(ptr, type, member) ({                      \
        const typeof( ((type *)0)->member ) *__mptr = (ptr);    \
        (type *)( (char *)__mptr - offsetof(type,member) );})

#define likely(x)      __builtin_expect(!!(x), 1)
#define unlikely(x)    __builtin_expect(!!(x), 0)
#define ACCESS_ONCE(x) (*(volatile typeof(x) *)&(x))

#endif

#endif
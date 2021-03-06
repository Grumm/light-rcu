#ifndef __LRCU_LINUX_KERNEL_DEFINES_H__
#define __LRCU_LINUX_KERNEL_DEFINES_H__

/* print defines */
#include <linux/printk.h>

#define LRCU_LOG(...) printk(KERN_NOTICE __VA_ARGS__)
#define PRIu64 "llu"
#define LRCU_ASSERT(cond) WARN(!(cond), #cond)
#define LRCU_WARN(...) WARN(true, __VA_ARGS__)

/* thread defines */
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/completion.h>

//static LRCU_ALIGNED __thread t v; this shall be in (struct task_struct) 
//http://elixir.free-electrons.com/linux/v4.12/source/include/linux/sched.h#L483
#define LRCU_TLS_DEFINE(t, v) 
//DECLARE_PER_CPU_ALIGNED(t, v) ???

struct lrcu_ts_compl_data{
	struct completion compl;
	struct task_struct *th;
	void *(*func)(void *data);
	void *data;
};
extern int __lrcu_kthread_wrapper_func(void *data);

#define LRCU_OS_API_DEFINE \
int __lrcu_kthread_wrapper_func(void *data){ \
	struct lrcu_ts_compl_data *d = (struct lrcu_ts_compl_data *)data; \
	d->func(d->data); \
	complete(&d->compl); \
	return 0; \
} \
EXPORT_SYMBOL(__lrcu_kthread_wrapper_func);

#define LRCU_THREAD_CREATE(tid, f, d) ({ \
				struct lrcu_ts_compl_data **__ptcd = (tid); \
				struct lrcu_ts_compl_data *__tcd; \
				int ret = true; \
				__tcd = *__ptcd = LRCU_CALLOC(1, sizeof(struct lrcu_ts_compl_data)); \
				if(__tcd){ \
					init_completion(&__tcd->compl); \
					__tcd->data = (d); \
					__tcd->func = (f); \
					__tcd->th = kthread_create(__lrcu_kthread_wrapper_func, __tcd, "lrcu_worker"); \
					if(!IS_ERR(__tcd->th)){ \
						const struct sched_param param = { .sched_priority = 20 }; \
						sched_setscheduler_nocheck(__tcd->th, SCHED_NORMAL, &param); \
						wake_up_process(__tcd->th); \
						ret = false; \
					}else{ \
						LRCU_FREE(__tcd); \
						*__ptcd = NULL; \
					} \
				} \
				ret; \
			})

#define LRCU_THREAD_JOIN(tid) ({ \
		if(wait_for_completion_interruptible(&(*(tid))->compl) != -ERESTARTSYS) \
			LRCU_FREE(*(tid)); \
	})
typedef struct lrcu_ts_compl_data *LRCU_THREAD_T;
#define LRCU_THREAD_SHOULD_STOP() kthread_should_stop()

#define LRCU_TLS_INIT(x)
#define LRCU_TLS_DEINIT(x)

#define LRCU_TLS_SET(a, b) (current->a = (b))
#define LRCU_TLS_GET(a) (current->a)

/* preemption things */
#include <linux/preempt.h>

#define LRCU_PREEMPT_ENABLE() preempt_enable()
#define LRCU_PREEMPT_DISABLE() preempt_disable()

/* misc */
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/vmalloc.h>

#if 0
#define LRCU_CALLOC(a, b) kzalloc((a) * (b), GFP_ATOMIC)
#define LRCU_MALLOC(a) kmalloc(a, GFP_ATOMIC)
#define LRCU_FREE(a) kfree(a)
#else
#define LRCU_CALLOC(a, b) kzalloc((a) * (b), GFP_KERNEL)
#define LRCU_MALLOC(a) kmalloc(a, GFP_KERNEL)
#define LRCU_FREE(a) kfree(a)
#endif
//#define LRCU_CALLOC(a, b) vzalloc((a) * (b))
//#define LRCU_MALLOC(a) vmalloc(a)
//#define LRCU_FREE(a) vfree(a)

#include <linux/sort.h>
#define LRCU_QSORT(base, num, size, cmp_func) \
		sort((base), (num), (size), (cmp_func), NULL)

#define LRCU_USLEEP(x) usleep_range((x), (x))
#define LRCU_YIELD() schedule()

#include <linux/module.h>

#define LRCU_EXPORT_SYMBOL(x) EXPORT_SYMBOL(x)

#define LRCU_ALIGNED ____cacheline_aligned

/* time */
#include <linux/timekeeping.h>
#include <linux/jiffies.h>

typedef u64 LRCU_TIMER_TYPE;
/* sec is MAX_INT/1000, usec is MAX_INT */
#define LRCU_TIMER_INIT(sec, usec) (msecs_to_jiffies(1000*(sec)) + usecs_to_jiffies(usec))

#define LRCU_TIMER_ADD(a, b, c) (*(c) = *(b) + *(a))
/* in case of 32bit */
#define LRCU_TIMER_GET(x) get_jiffies_64()
#define LRCU_TIMER_ISSET(x) ((x) == 0)
#define LRCU_TIMER_CLEAR(x) ((x) = 0)
/* a, b, < => a < b */
#define LRCU_TIMER_CMP(a, b, c) ((a) c (b))

/* for testing purposes */
#define LRCU_EXIT(x) BUG()
#define EXIT_SUCCESS 0
#define EXIT_FAILURE -1

#endif /* __LRCU_LINUX_KERNEL_DEFINES_H__ */
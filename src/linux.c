#include <lrcu/lrcu.h>
#ifdef LRCU_LINUX
#include <linux/module.h>

static int __init lrcu_start(void)
{
	if(lrcu_init())
    	return 0;
    return -1;
}
static void __exit lrcu_cleanup_module(void)
{
	lrcu_deinit();
}

#ifdef CONFIG_LRCU_MODULE

module_init(lrcu_start);
module_exit(lrcu_cleanup_module);

#define DRV_VERSION "1.0"
MODULE_VERSION(DRV_VERSION);
MODULE_LICENSE("GPL");

MODULE_DESCRIPTION("LRCU" ", v" DRV_VERSION);
MODULE_AUTHOR("Andrei Dubasov, andrew.dubasov@gmail.com");
#else /* CONFIG_LRCU_MODULE */
subsys_initcall(lrcu_start);
__exitcall(lrcu_cleanup_module);
#endif /* CONFIG_LRCU_MODULE */
#endif /* LRCU_LINUX */
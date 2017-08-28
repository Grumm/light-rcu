#include <linux/module.h>

static int __init lrcu_start(void)
{
    return 0;
}
static void __exit lrcu_cleanup_module(void)
{
}

module_init(lrcu_start);
module_exit(lrcu_cleanup_module);

#define DRV_VERSION "1.0"
MODULE_VERSION(DRV_VERSION);
MODULE_LICENSE("GPL");

MODULE_DESCRIPTION("LRCU" ", v" DRV_VERSION);
MODULE_AUTHOR("Andrei Dubasov, andrew.dubasov@gmail.com");

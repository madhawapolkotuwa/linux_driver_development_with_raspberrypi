#include <linux/module.h>
#include <linux/init.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("MPCoding - LDD");
MODULE_DESCRIPTION("Our first dynamically loadable kernel module");

static int __init my_init(void) {
    printk("Hello: Hello, Kernel\n");
    return 0;
}

static void __exit my_exit(void){
    printk("Hello: Goodbye, Kernel\n");
}

module_init(my_init);
module_exit(my_exit);
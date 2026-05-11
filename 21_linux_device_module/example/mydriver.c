#include "mybus.h"

/***********************************
 * probe()
 ***********************************/
static int my_probe(struct device *dev)
{
    pr_info("mydriver: probe called for %s\n",
            dev_name(dev));

    return 0;
}

/***********************************
 * remove()
 ***********************************/
static int my_remove(struct device *dev)
{
    pr_info("mydriver: remove called for %s\n",
            dev_name(dev));

    return 0;
}

/***********************************
 * Driver object
 ***********************************/
static struct my_driver mydrv = {
    .driver = {
        .name   = "mydev0",
        .owner  = THIS_MODULE,
        .probe  = my_probe,
        .remove = my_remove,
    },
};

static int __init mydriver_init(void)
{
    pr_info("mydriver: init\n");

    return my_register_driver(&mydrv);
}

static void __exit mydriver_exit(void)
{
    pr_info("mydriver: exit\n");

    my_unregister_driver(&mydrv);
}

module_init(mydriver_init);
module_exit(mydriver_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("MPCoding - LDD");
MODULE_DESCRIPTION("Fake Driver");
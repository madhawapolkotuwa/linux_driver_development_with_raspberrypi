#include "mybus.h"

/*******************************************
 * Create one fake device
 *******************************************/
static struct my_device mydev = {
    .name = "mydev0",
};

static int __init mydevice_init(void)
{
    int ret;

    pr_info("mydevice: init\n");

    ret = my_register_device(&mydev);
    if (ret)
        return ret;

    pr_info("mydevice: device registered\n");

    return 0;
}

static void __exit mydevice_exit(void)
{
    pr_info("mydevice: exit\n");

    my_unregister_device(&mydev);
}

module_init(mydevice_init);
module_exit(mydevice_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("MPCoding -LDD");
MODULE_DESCRIPTION("Fake Device");
#include <linux/module.h>
#include <linux/init.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("MPCoding - LDD");
MODULE_DESCRIPTION("Our log levels check kernel module");

static const char *module_name = "log_levels";

/* Single value param */
static int my_int = 30;
static bool my_bool = true;
static char *my_str = "hello";

/* Array param */
#define ARR_SIZE  10
static int my_arr[ARR_SIZE] = { 1, 2, 3 };
static int arr_count = ARR_SIZE; // kernel will set this to the number of elements passed 

/* Declare module parameters:
   name, type, permissions (0 = sysfs entry; use S-IRUGO-like values)
*/

module_param(my_int, int, 0444);
MODULE_PARM_DESC(my_int, "An interger parameter");

module_param(my_bool, bool, 0444);
MODULE_PARM_DESC(my_bool, "A boolean parameter");

module_param(my_str, charp, 0444);
MODULE_PARM_DESC(my_str, "A string paramerter");

/* For arrays, use module_param_array */
module_param_array(my_arr, int, &arr_count, 0644);
MODULE_PARM_DESC(my_arr, "An array of integers");

static int __init my_init(void) {
    printk(KERN_INFO "%s: Hello, Kernel\n",module_name);
    // printk(KERN_ALERT "%s: This is an alert\n",module_name);
    // printk(KERN_WARNING "%s: This is a warning\n",module_name);
    // printk(KERN_NOTICE "%s: This is a notice\n",module_name);
    // printk(KERN_ERR "%s: This is an error\n",module_name);
    // printk(KERN_EMERG "%s: This is an emergency\n",module_name);
    // printk(KERN_DEBUG "%s: This is a debug\n",module_name);

    int i;
    pr_info("%s: my_int = %d\n",module_name, my_int);
    pr_info("%s: my_bool = %d\n",module_name, my_bool);
    pr_info("%s: my_str = %s\n",module_name, my_str);
    pr_info("%s: my_arr (count=%d)=\n",module_name, arr_count);
    for(i=0; i < arr_count; i++)
            pr_info(" %d", my_arr[i]);
    pr_info("\n");

    return 0;
}

static void __exit my_exit(void){
    pr_info("%s: Goodbye, Kernel\n", module_name);
    // pr_err("%s: This is an error on exit\n", module_name);
    // pr_warn("%s: This is a warning on exit\n", module_name);
    // pr_emerg("%s: This is an emergency on exit\n", module_name);
    // pr_debug("%s: This is a debug on exit\n", module_name);
}

module_init(my_init);
module_exit(my_exit);
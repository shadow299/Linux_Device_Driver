
#include <linux/module.h>
#include <linux/init.h>
#include <linux/moduleparam.h> //for module parameter

static int count;
static char *data;

module_param(count, int, 0644);
module_param(data, charp, 0644);

static int __init hello_init(void)
{
    printk("Module loaded with integer value %d and string value %s\n", count, data);
    // log levels
    pr_info("this is info\n");
    pr_warn("this is warning\n");
    pr_err("this is error\n");
    return 0;
}

static void __exit hello_exit(void)
{
    printk("Bye Kernel\n");
}

module_init(hello_init);
module_exit(hello_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Manish Chandra <chandramanish900@gmail.com>");
MODULE_DESCRIPTION("A simple hello world module");
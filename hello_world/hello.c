#include <linux/module.h>
#include <linux/init.h>

static int sayhello(void)
{
    printk("hello world\n");
    return 0;
}

static int __init hello_init(void)
{
    for (int i = 0; i < 10; i++)
    {
        printk("hello %d\n", i);
    }
    sayhello();
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
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>                    //alloc_chrdev_region
#include <uapi/asm-generic/errno-base.h> //errors
#include <linux/device.h>                //class_create
#include <linux/err.h>
#include <linux/cdev.h>

static dev_t devt;
static char *driver_name = "c_driver";
static struct class *c_class;
static struct cdev cdv;
static struct device *dev;

static ssize_t char_read(struct file *fp, char __user *buffer, size_t len, loff_t *offset)
{
    return 0;
}

static ssize_t char_write(struct file *fp, const char __user *buffer, size_t len, loff_t *offset)
{
    return 0;
}

static int char_open(struct inode *inode, struct file *fp)
{
    return 0;
}

static int char_release(struct inode *inode, struct file *fp)
{
    return 0;
}

static const struct file_operations fops =
    {
        .read = char_read,
        .write = char_write,
        .open = char_open,
        .release = char_release,
};

static char *c_devnode(const struct device *dev, umode_t *mode)
{
    if (mode)
        *mode = 0666; // or 0660, etc.
    return NULL;      // NULL means use default name
}

static int __init
char_init(void)
{
    // allocate major and minor number
    if (alloc_chrdev_region(&devt, 0, 1, driver_name) < 0)
    {
        pr_err("Failed to allocate major and minor number\n");
        return -ENODEV;
    }
    // create a device class
    c_class = class_create(driver_name);
    if (IS_ERR(c_class))
    {
        unregister_chrdev_region(devt, 1);
        return -ENODEV;
    }
    c_class->devnode = c_devnode;

    // init char device
    cdev_init(&cdv, &fops);
    cdev_add(&cdv, devt, 1);

    // create device for /dev
    dev = device_create(c_class, NULL, devt, NULL, driver_name);
    if (IS_ERR(dev))
    {
        unregister_chrdev_region(devt, 1);
        device_destroy(c_class, devt);
        cdev_del(&cdv);
        class_destroy(c_class);
    }
    pr_info("%s Module Initilized\n", driver_name);
    return 0;
}

static void __exit char_exit(void)
{
    unregister_chrdev_region(devt, 1);
    device_destroy(c_class, devt);
    cdev_del(&cdv);
    class_destroy(c_class);
    printk("Bye Kernel\n");
}

module_init(char_init);
module_exit(char_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Manish Chandra <chandramanish900@gmail.com>");
MODULE_DESCRIPTION("A simple char device driver");
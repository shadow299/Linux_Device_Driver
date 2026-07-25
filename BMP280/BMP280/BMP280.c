#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/errno.h>

#define BMP280_REG_DIG_T1 0x88
#define BMP280_REG_DIG_T2 0x8A
#define BMP280_REG_DIG_T3 0x8C
#define BMP280_REG_CTRL_MEAS 0xF4
#define BMP280_REG_TEMP_MSB 0xFA
#define BMP280_REG_TEMP_LSB 0xFB
#define BMP280_TEMP_CONVERT_CMD 0x2E

struct bmp280_device
{
    struct cdev cdev;
    struct class *class;
    struct device *device;
    dev_t devt;
    struct i2c_client *client;
    s16 dig_t1;
    s16 dig_t2;
    s16 dig_t3;
    s32 temperature_centi;
    bool ready;
};

static struct bmp280_device bmp280_dev;
static const char *driver_name = "bmp280";

/*
 * Read the BMP280 calibration coefficients from the sensor.
 * The parameter dev points to the driver-private state that keeps the
 * sensor context and the calibration values.
 */
static int bmp280_read_calibration(struct bmp280_device *dev)
{
    struct i2c_client *client = dev->client;
    int ret;

    if (!client)
        return -ENODEV;

    ret = i2c_smbus_read_word_data(client, BMP280_REG_DIG_T1);
    if (ret < 0)
        return ret;
    dev->dig_t1 = (s16)ret;

    ret = i2c_smbus_read_word_data(client, BMP280_REG_DIG_T2);
    if (ret < 0)
        return ret;
    dev->dig_t2 = (s16)ret;

    ret = i2c_smbus_read_word_data(client, BMP280_REG_DIG_T3);
    if (ret < 0)
        return ret;
    dev->dig_t3 = (s16)ret;

    return 0;
}

/*
 * Trigger one temperature conversion and compute the final temperature.
 * The returned value is in centi-degrees Celsius, so 2500 means 25.00 C.
 */
static int bmp280_read_temperature(struct bmp280_device *dev)
{
    struct i2c_client *client = dev->client;
    s32 adc_temp;
    s32 var1;
    s32 var2;
    s32 t_fine;
    int ret;

    if (!dev->ready || !client)
        return -ENODEV;

    ret = i2c_smbus_write_byte_data(client, BMP280_REG_CTRL_MEAS,
                                    BMP280_TEMP_CONVERT_CMD);
    if (ret < 0)
        return ret;

    msleep(5);

    ret = i2c_smbus_read_byte_data(client, BMP280_REG_TEMP_MSB);
    if (ret < 0)
        return ret;
    adc_temp = ret << 8;

    ret = i2c_smbus_read_byte_data(client, BMP280_REG_TEMP_LSB);
    if (ret < 0)
        return ret;
    adc_temp |= ret;

    var1 = ((((adc_temp >> 3) - ((s32)dev->dig_t1 << 1))) * dev->dig_t2) >> 11;
    var2 = (((((adc_temp >> 4) - dev->dig_t1) * ((adc_temp >> 4) - dev->dig_t1)) >> 12) *
            dev->dig_t3) >>
           14;
    t_fine = var1 + var2;
    dev->temperature_centi = ((t_fine * 5 + 128) >> 8);

    return dev->temperature_centi;
}

/*
 * This is the file operation that runs when user-space code calls read().
 * Parameters:
 *  - file: the open file object
 *  - buffer: destination buffer in user space
 *  - count: requested number of bytes
 *  - ppos: current read position inside the file
 */
static ssize_t bmp280_char_read(struct file *file, char __user *buffer,
                                size_t count, loff_t *ppos)
{
    struct bmp280_device *dev = file->private_data;
    char temp_buf[32];
    int len;
    int ret;

    if (!dev)
        return -EINVAL;

    ret = bmp280_read_temperature(dev);
    if (ret < 0)
        return ret;

    len = scnprintf(temp_buf, sizeof(temp_buf), "%d\n", ret);
    if (*ppos >= len)
        return 0;

    if (count > len - *ppos)
        count = len - *ppos;

    if (copy_to_user(buffer, temp_buf + *ppos, count))
        return -EFAULT;

    *ppos += count;
    return count;
}

/*
 * This is the file operation that runs when user-space code calls write().
 * A non-zero write triggers a fresh temperature measurement.
 */
static ssize_t bmp280_char_write(struct file *file, const char __user *buffer,
                                 size_t count, loff_t *ppos)
{
    struct bmp280_device *dev = file->private_data;
    int ret;

    if (!dev)
        return -EINVAL;

    if (count == 0)
        return 0;

    ret = bmp280_read_temperature(dev);
    if (ret < 0)
        return ret;

    return count;
}

/*
 * Called when the character device is opened.
 * The file->private_data field stores the driver context for later reads/writes.
 */
static int bmp280_char_open(struct inode *inode, struct file *file)
{
    file->private_data = &bmp280_dev;
    return 0;
}

static int bmp280_char_release(struct inode *inode, struct file *file)
{
    return 0;
}

static const struct file_operations bmp280_fops = {
    .owner = THIS_MODULE,
    .read = bmp280_char_read,
    .write = bmp280_char_write,
    .open = bmp280_char_open,
    .release = bmp280_char_release,
};

static char *bmp280_devnode(const struct device *dev, umode_t *mode)
{
    if (mode)
        *mode = 0666;
    return NULL;
}

static void bmp280_cleanup_char_device(struct bmp280_device *dev)
{
    if (dev->device)
    {
        device_destroy(dev->class, dev->devt);
        dev->device = NULL;
    }

    if (!IS_ERR_OR_NULL(dev->class))
    {
        class_destroy(dev->class);
        dev->class = NULL;
    }

    cdev_del(&dev->cdev);
    unregister_chrdev_region(dev->devt, 1);
}

static int bmp280_create_char_device(struct bmp280_device *dev)
{
    int ret;

    ret = alloc_chrdev_region(&dev->devt, 0, 1, driver_name);
    if (ret)
    {
        pr_err("failed to allocate chrdev region\n");
        return ret;
    }

    dev->class = class_create(THIS_MODULE, driver_name);
    if (IS_ERR(dev->class))
    {
        ret = PTR_ERR(dev->class);
        pr_err("failed to create class\n");
        unregister_chrdev_region(dev->devt, 1);
        return ret;
    }
    dev->class->devnode = bmp280_devnode;

    cdev_init(&dev->cdev, &bmp280_fops);
    ret = cdev_add(&dev->cdev, dev->devt, 1);
    if (ret)
    {
        pr_err("failed to add cdev\n");
        class_destroy(dev->class);
        unregister_chrdev_region(dev->devt, 1);
        return ret;
    }

    dev->device = device_create(dev->class, NULL, dev->devt, NULL,
                                driver_name);
    if (IS_ERR(dev->device))
    {
        ret = PTR_ERR(dev->device);
        pr_err("failed to create device\n");
        cdev_del(&dev->cdev);
        class_destroy(dev->class);
        unregister_chrdev_region(dev->devt, 1);
        return ret;
    }

    return 0;
}

/*
 * I2C probe callback. This is called when the kernel finds a matching BMP280.
 * The parameter client represents the I2C device that was detected.
 */
static int bmp280_probe(struct i2c_client *client,
                        const struct i2c_device_id *id)
{
    int ret;

    bmp280_dev.client = client;
    ret = bmp280_read_calibration(&bmp280_dev);
    if (ret < 0)
    {
        pr_err("failed to read BMP280 calibration data: %d\n", ret);
        return ret;
    }

    bmp280_dev.ready = true;
    pr_info("BMP280 detected at address 0x%02x\n", client->addr);
    return 0;
}

static int bmp280_remove(struct i2c_client *client)
{
    bmp280_dev.client = NULL;
    bmp280_dev.ready = false;
    return 0;
}

static const struct i2c_device_id bmp280_id_table[] = {
    {"bmp280", 0},
    {}};
MODULE_DEVICE_TABLE(i2c, bmp280_id_table);

static struct i2c_driver bmp280_i2c_driver = {
    .driver = {
        .name = "bmp280",
        .owner = THIS_MODULE,
    },
    .probe = bmp280_probe,
    .remove = bmp280_remove,
    .id_table = bmp280_id_table,
};

static int __init bmp280_init(void)
{
    int ret;

    memset(&bmp280_dev, 0, sizeof(bmp280_dev));

    ret = bmp280_create_char_device(&bmp280_dev);
    if (ret)
        return ret;

    ret = i2c_add_driver(&bmp280_i2c_driver);
    if (ret)
    {
        pr_err("failed to register I2C driver: %d\n", ret);
        bmp280_cleanup_char_device(&bmp280_dev);
        return ret;
    }

    pr_info("BMP280 module initialized\n");
    return 0;
}

static void __exit bmp280_exit(void)
{
    i2c_del_driver(&bmp280_i2c_driver);
    bmp280_cleanup_char_device(&bmp280_dev);
    pr_info("BMP280 module unloaded\n");
}

module_init(bmp280_init);
module_exit(bmp280_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Manish Chandra <chandramanish900@gmail.com>");
MODULE_DESCRIPTION("BMP280 temperature sensor driver");
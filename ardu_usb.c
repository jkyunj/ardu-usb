#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/err.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#define MEM_SIZ	1024
#define USB_VENDOR_ID	(0x2341)
#define USB_PRODUCT_ID	(0x0043)

#define PRINT_USB_INTERFACE_DESCRIPTOR(i)                                    \
        {                                                                    \
                pr_info("USB_INTERFACE_DESCRIPTOR:\n");                      \
                pr_info("-----------------------------\n");                  \
                pr_info("bLength: 0x%x\n", i.bLength);                       \
                pr_info("bDescriptorType: 0x%x\n", i.bDescriptorType);       \
                pr_info("bInterfaceNumber: 0x%x\n", i.bInterfaceNumber);     \
               pr_info("bAlternateSetting: 0x%x\n", i.bAlternateSetting);   \
                pr_info("bNumEndpoints: 0x%x\n", i.bNumEndpoints);           \
                pr_info("bInterfaceClass: 0x%x\n", i.bInterfaceClass);       \
                pr_info("bInterfaceSubClass: 0x%x\n", i.bInterfaceSubClass); \
                pr_info("bInterfaceProtocol: 0x%x\n", i.bInterfaceProtocol); \
                pr_info("iInterface: 0x%x\n", i.iInterface);                 \
                pr_info("\n");                                               \
        }
#define PRINT_USB_ENDPOINT_DESCRIPTOR(e)                                 \
        {                                                                \
                pr_info("USB_ENDPOINT_DESCRIPTOR:\n");                   \
                pr_info("------------------------\n");                   \
                pr_info("bLength: 0x%x\n", e.bLength);                   \
                pr_info("bDescriptorType: 0x%x\n", e.bDescriptorType);   \
                pr_info("bEndPointAddress: 0x%x\n", e.bEndpointAddress); \
                pr_info("bmAttributes: 0x%x\n", e.bmAttributes);         \
                pr_info("wMaxPacketSize: 0x%x\n", e.wMaxPacketSize);     \
                pr_info("bInterval: 0x%x\n", e.bInterval);               \
                pr_info("\n");                                           \
        }

dev_t dev = MKDEV(234, 0);
static struct class *dev_class;
static struct cdev ardu_cdev;
int *kbuf;

static int	ardu_open(struct inode *inode, struct file *file);
static int	ardu_release(struct inode *inode, struct file *file);
static ssize_t	ardu_read(struct file *file, char __user *buf, size_t len, loff_t *off);
static ssize_t	ardu_write(struct file *file, const char *buf, size_t len, loff_t *off);

static struct file_operations fops = 
{
	.owner		= THIS_MODULE,
	.read		= ardu_read,
	.write		= ardu_write,
	.open		= ardu_open,
	.release	= ardu_release,
};

static int ardu_open(struct inode *inode, struct file *file)
{
	pr_info("ardu_open is called\n");
	return 0;
}

static int ardu_release(struct inode *inode, struct file *file)
{
	pr_info("ardu_release is called\n");
	return 0;
}

static ssize_t ardu_read(struct file *file, char __user *buf, size_t len, loff_t *off)
{
	if(copy_to_user(buf, kbuf, MEM_SIZ)) {
		pr_err("cannot copy data to user\n");
	}
	pr_info("copy data to user\n");
	return MEM_SIZ;
}

static ssize_t ardu_write(struct file *file, const char *buf, size_t len, loff_t *off)
{
	if(copy_from_user(kbuf, buf, len)) {
		pr_err("cannot copy data from user\n");
	}
	pr_info("copy data from user\n");
	return len;
}

static int ardu_usb_probe(struct usb_interface *interface,
                          const struct usb_device_id *id)
{
        unsigned int i;
        unsigned int endpoints_count;
        struct usb_host_interface *iface_desc = interface->cur_altsetting;
        dev_info(&interface->dev, "USB Driver Probed: Vendor ID : 0x%02x,\t"
                                  "Product ID : 0x%02x\n",
                 id->idVendor, id->idProduct);

        endpoints_count = iface_desc->desc.bNumEndpoints;

        PRINT_USB_INTERFACE_DESCRIPTOR(iface_desc->desc);

        for (i = 0; i < endpoints_count; i++)
        {
                PRINT_USB_ENDPOINT_DESCRIPTOR(iface_desc->endpoint[i].desc);
        }
        return 0;
}

static void ardu_usb_disconnect(struct usb_interface *interface)
{
        dev_info(&interface->dev, "USB Driver Disconnected\n");
}

const struct usb_device_id ardu_usb_table[] = {
    {USB_DEVICE(USB_VENDOR_ID, USB_PRODUCT_ID)}, // Put your USB device's Vendor and Product ID
    {}                                           /* Terminating entry */
};

MODULE_DEVICE_TABLE(usb, ardu_usb_table);

static struct usb_driver ardu_usb_driver = {
    .name = "Arduino USB Driver",
    .probe = ardu_usb_probe,
    .disconnect = ardu_usb_disconnect,
    .id_table = ardu_usb_table,
};

static int __init ardu_usb_init(void)
{
	if((register_chrdev_region(dev, 1, "ardu_usb")) < 0) {
		pr_info("fail to alloc_chrdev_region\n");
		return -1;
	}
	pr_info("ardu_usb: Major = %d Minor = %d\n", MAJOR(dev), MINOR(dev));

	cdev_init(&ardu_cdev, &fops);

	if(cdev_add(&ardu_cdev, dev, 1) < 0) {
		pr_info("Cannot add the ardu_cdev\n");
		goto err_class;
	}

	dev_class = class_create(THIS_MODULE, "ardu_dev");
	if(IS_ERR(dev_class)) {
		pr_err("cannot create the struct class for device\n");
		goto err_class;
	}

	if(IS_ERR(device_create(dev_class, NULL, dev, NULL, "ardu_device"))) {
		pr_err("Cannot create the ardu_dev\n");
		goto err_device;
	}

	if((kbuf = (int*)kmalloc(MEM_SIZ, GFP_KERNEL)) == 0) {
		pr_info("Cannot allocate kernel memory\n");
		goto err_alloc;
	}
	memset(kbuf, 0, MEM_SIZ);

	pr_info("ardu_usb is loaded successfully\n");
	return 0;

err_alloc:
	device_destroy(dev_class, dev);
err_device: 
	class_destroy(dev_class);
err_class:
	unregister_chrdev_region(dev, 1);
	return -1;
}

static void __exit ardu_usb_exit(void)
{
	kfree(kbuf);
	device_destroy(dev_class, dev);
	class_destroy(dev_class);
	cdev_del(&ardu_cdev);
	unregister_chrdev_region(dev, 1);
	pr_info("ardu_usb is unloaded\n");
}

module_init(ardu_usb_init);
module_exit(ardu_usb_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jaekyun Jang <jaegun0103@ajou.ac.kr>");
MODULE_DESCRIPTION("arduino device driver to connect with usb");
MODULE_VERSION("1.0.3");

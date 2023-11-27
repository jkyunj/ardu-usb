#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/errno.h>
#include <linux/kref.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#define ARDU_VENDOR_ID	(0x2341)
#define ARDU_PRODUCT_ID	(0x0043)

const struct usb_device_id ardu_usb_table[] = {
    {USB_DEVICE(ARDU_VENDOR_ID, ARDU_PRODUCT_ID)}, 
    {}
};
MODULE_DEVICE_TABLE(usb, ardu_usb_table);

#define ARDU_MINOR_BASE	0

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
struct ardu_usb {
	struct usb_device		*udev;
	struct usb_interface	*interface;
	struct urb				*bulk_in_urb;
	unsigned char			*bulk_in_buffer, *ctrl_buffer;
	size_t					bulk_in_size;
	size_t					bulk_in_filled;
	size_t					bulk_in_copied;
	__u8					bulk_in_endpointAddr;
	wait_queue_head_t		wq;
	int						errors;
	bool					ongoing;
	spinlock_t				spin_lock;
	struct mutex			mutex;
	struct kref				kref;
	unsigned long			disconnected:1;
};

#define to_ardu_dev(d) container_of(d, struct ardu_usb, kref)

static void ardu_delete(struct kref *kref)
{
	struct ardu_usb *ardu = to_ardu_dev(kref);

	usb_free_urb(ardu->bulk_in_urb);
	usb_put_intf(ardu->interface);
	usb_put_dev(ardu->udev);
	kfree(ardu->bulk_in_buffer);
	kfree(ardu);
}

static struct usb_driver ardu_usb_driver;

static int ardu_open(struct inode *inode, struct file *file)
{
	struct ardu_usb *ardu;
	struct usb_interface *interface;
	int rv = 0;

	interface = usb_find_interface(&ardu_usb_driver, iminor(inode));
	if(!interface) {
		pr_err("%s Error: cannot find interface #%d\n",
			__func__, iminor(inode));
		rv = -ENODEV;
		goto exit;
	}

	ardu = usb_get_intfdata(interface);
	if(!ardu) {
		rv = -ENODEV;
		goto exit;
	}

	rv = usb_autopm_get_interface(interface);
	if(rv)
		goto exit;

	kref_get(&ardu->kref);

	file->private_data = ardu;

	printk("ardu_open is called\n");
exit:
	return rv;
}

static int ardu_release(struct inode *inode, struct file *file)
{
	struct ardu_usb *ardu = file->private_data;
	if(!ardu)
		return -ENODEV;

	usb_autopm_put_interface(ardu->interface);

	kref_put(&ardu->kref, ardu_delete);

	printk("ardu_release is called\n");
	return 0;
}

static void ardu_read_bulk_callback(struct urb *urb)
{
	struct ardu_usb *ardu;
	unsigned long flags;

	ardu = urb->context;

	spin_lock_irqsave(&ardu->spin_lock, flags);
	if (urb->status) {
		if (!(urb->status == -ENOENT ||
		    urb->status == -ECONNRESET ||
		    urb->status == -ESHUTDOWN))
			dev_err(&ardu->interface->dev,
				"%s Error: error status received: %d\n",
				__func__, urb->status);

		ardu->errors = urb->status;
		printk("urb status: %d\n", urb->status);
	} else {
		ardu->bulk_in_filled = urb->actual_length;
		printk("urb actual_length: %d\n", urb->actual_length);
	}
	ardu->ongoing = 0;
	spin_unlock_irqrestore(&ardu->spin_lock, flags);

	wake_up_interruptible(&ardu->wq);

}


static int ardu_do_read(struct ardu_usb *ardu, size_t len)
{
	int rv;

	usb_fill_bulk_urb(ardu->bulk_in_urb,
		ardu->udev,
		usb_rcvbulkpipe(ardu->udev, ardu->bulk_in_endpointAddr),
		ardu->bulk_in_buffer,
		min(ardu->bulk_in_size, len),
		ardu_read_bulk_callback,
		ardu);

	spin_lock_irq(&ardu->spin_lock);
	ardu->ongoing = 1;
	spin_unlock_irq(&ardu->spin_lock);

	ardu->bulk_in_filled = 0;
	ardu->bulk_in_copied = 0;

	rv = usb_submit_urb(ardu->bulk_in_urb, GFP_KERNEL);
	if (rv < 0) {
		dev_err(&ardu->interface->dev,
			"%s Error: fail to submit read urb, errorno %d\n",
			__func__, rv);
		spin_lock_irq(&ardu->spin_lock);
		ardu->ongoing = 0;
		spin_unlock_irq(&ardu->spin_lock);
	}

	return rv;
}

static ssize_t ardu_read(struct file *file, char __user *buf, size_t len, loff_t *off)
{
	struct ardu_usb *ardu;
	int rv;
	bool ongoing;

	ardu = file->private_data;

	if(!ardu->bulk_in_urb || !len)
		return 0;

	rv = mutex_lock_interruptible(&ardu->mutex);
	if(rv < 0)
		return rv;
	
	if(ardu->disconnected) {
		rv = -ENODEV;
		goto exit;
	}

retry:
	spin_lock_irq(&ardu->spin_lock);
	ongoing = ardu->ongoing;
	spin_unlock_irq(&ardu->spin_lock);

	if(ongoing) {
		if(file->f_flags & O_NONBLOCK) {
			rv = -EAGAIN;
			goto exit;
		}
	
		rv = wait_event_interruptible(ardu->wq, (!ardu->ongoing));
		if(rv < 0)
			goto exit;
	}

	rv = ardu->errors;
	if(rv < 0) {
		ardu->errors = 0;
		goto exit;
	}

	if(ardu->bulk_in_filled) {
		size_t avail = ardu->bulk_in_filled - ardu->bulk_in_copied;
		size_t chunk = min(avail, len);

		if(!avail) {
			rv = ardu_do_read(ardu, len);
			if(rv < 0)
				goto exit;
			else
				goto retry;
		}

		if(copy_to_user(buf,
				ardu->bulk_in_buffer + ardu->bulk_in_copied,
				chunk))
			rv = -EFAULT;
		else
			rv = chunk;

		ardu->bulk_in_copied += chunk;

		if(avail < len)
			ardu_do_read(ardu, len - chunk);
	} else {
		/* no filled data in buffer */
		rv = ardu_do_read(ardu, len);

		if(rv < 0)
			goto exit;
		else
			goto retry;
	}

	printk("ardu_read is called\n");
exit:
	mutex_unlock(&ardu->mutex);
	return rv;
}

static ssize_t ardu_write(struct file *file, const char *user_buffer, size_t len, loff_t *off)
{
	return 0;
}

static struct file_operations ardu_fops = 
{
	.owner		= THIS_MODULE,
	.read		= ardu_read,
	.write		= ardu_write,
	.open		= ardu_open,
	.release	= ardu_release,
};

#ifdef DEBUG
static int ardu_print_desc(struct usb_interface *interface, 
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
#endif

static struct usb_class_driver ardu_class = {
	.name = 		"ardu%d",
	.fops =			&ardu_fops,
	.minor_base = 	ARDU_MINOR_BASE,
};

static int ardu_probe(struct usb_interface *interface, 
				const struct usb_device_id *id)
{
		struct ardu_usb *ardu;
		struct usb_endpoint_descriptor *bulk_in;
		int rv;

		ardu = kzalloc(sizeof(*ardu), GFP_KERNEL);
		if(!ardu)
			return -ENOMEM;

		mutex_init(&ardu->mutex);
		spin_lock_init(&ardu->spin_lock);
		kref_init(&ardu->kref);
		init_waitqueue_head(&ardu->wq);

		ardu->udev = usb_get_dev(interface_to_usbdev(interface));
		ardu->interface = usb_get_intf(interface);

		rv = usb_find_common_endpoints(interface->cur_altsetting,
					&bulk_in, NULL, NULL, NULL);
		if(rv) {
			dev_err(&interface->dev,
				"could not find bulk-in\n");
				goto err;
		}

		ardu->bulk_in_size = usb_endpoint_maxp(bulk_in);
		ardu->bulk_in_endpointAddr = bulk_in->bEndpointAddress;
		ardu->bulk_in_buffer = kmalloc(ardu->bulk_in_size, GFP_KERNEL);
		if(!ardu->bulk_in_buffer) {
			rv = -ENOMEM;
			goto err;
		}
		ardu->bulk_in_urb = usb_alloc_urb(0, GFP_KERNEL);
		if(!ardu->bulk_in_urb) {
			rv = -ENOMEM;
			goto err;
		}

		/* send control message to communicate */
		ardu->ctrl_buffer = kzalloc(8, GFP_KERNEL);
		if(!ardu->ctrl_buffer) {
			rv = -ENOMEM;
			goto err;
		}
		rv = usb_control_msg(ardu->udev, 
				usb_sndctrlpipe(ardu->udev, 0), 
				0x22, 
				0x21, 
				cpu_to_le16(0x00), 
				cpu_to_le16(0x00), 
				ardu->ctrl_buffer,
				cpu_to_le16(0x00), 
				0);
		if(rv < 0) {
			dev_err(&interface->dev,
				"could not send control command(1) message\n");
				goto err;	
		}

		ardu->ctrl_buffer[0] = 0x80;
		ardu->ctrl_buffer[1] = 0x25;
		ardu->ctrl_buffer[6] = 0x08;

		rv = usb_control_msg(ardu->udev, 
				usb_sndctrlpipe(ardu->udev, 0), 
				0x20, 
				0x21, 
				cpu_to_le16(0x00), 
				cpu_to_le16(0x00), 
				ardu->ctrl_buffer,
				cpu_to_le16(0x08), 
				0);
		if(rv < 0)
		{
			dev_err(&interface->dev,
				"could not send control command(2) message\n");
				goto err;
		}	

		usb_set_intfdata(interface, ardu);

		rv = usb_register_dev(interface, &ardu_class);
		if(rv) {
			dev_err(&interface->dev,
				"cannot register usb device\n");
			usb_set_intfdata(interface, NULL);
			goto err;
		}

		dev_info(&interface->dev,
			"ardu usb device attached to /dev/ardu%d",
			interface->minor);
		
		// ardu_print_desc(interface, id);

		return 0;

err:
	kref_put(&ardu->kref, ardu_delete);
	return rv;
}

static void ardu_disconnect(struct usb_interface *interface)
{
	struct ardu_usb *ardu;
	int minor = interface->minor;

	ardu = usb_get_intfdata(interface);
	usb_set_intfdata(interface, NULL);

	usb_deregister_dev(interface, &ardu_class);

	mutex_lock(&ardu->mutex);
	ardu->disconnected = 1;
	mutex_unlock(&ardu->mutex);

	kref_put(&ardu->kref, ardu_delete);
	
	dev_info(&interface->dev, "ardu-usb #%d is disconnected\n", minor);
}


static struct usb_driver ardu_usb_driver = {
    .name = "ardu-usb",
    .probe = ardu_probe,
    .disconnect = ardu_disconnect,
    .id_table = ardu_usb_table,
	.supports_autosuspend = 1,
};

module_usb_driver(ardu_usb_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jaekyun Jang <jaegun0103@ajou.ac.kr>");
MODULE_DESCRIPTION("arduino device driver to connect with usb");
MODULE_VERSION("1.0.4");

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/usb.h>

#define IS_NEW_METHOD_USED (1)
#define USB_VENDOR_ID (0x2341)
#define USB_PRODUCT_ID (0x0043)
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

#if (IS_NEW_METHOD_USED)
module_usb_driver(ardu_usb_driver);

#else
static int __init ardu_usb_init(void)
{
        return usb_register(&ardu_usb_driver);
}
static void __exit ardu_usb_exit(void)
{
        usb_deregister(&ardu_usb_driver);
}
module_init(ardu_usb_init);
module_exit(ardu_usb_exit);
#endif

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jaekyun Jang <jaegun0103@ajou.ac.kr>");
MODULE_DESCRIPTION("arduino device driver to connect with usb");
MODULE_VERSION("1.0.0");

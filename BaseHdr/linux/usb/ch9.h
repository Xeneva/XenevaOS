#ifndef __LINUX_USB_CH9_H__
#define __LINUX_USB_CH9_H__

#include <linux/list.h>

/* ─── USB speed ─── */
enum usb_device_speed {
    USB_SPEED_UNKNOWN = 0,
    USB_SPEED_LOW,              /* 1.5 Mb/s */
    USB_SPEED_FULL,             /* 12 Mb/s */
    USB_SPEED_HIGH,             /* 480 Mb/s */
    USB_SPEED_WIRELESS,
    USB_SPEED_SUPER,            /* 5 Gb/s */
    USB_SPEED_SUPER_PLUS,       /* 10 Gb/s */
};

/* ─── USB device state ─── */
enum usb_device_state {
    USB_STATE_NOTATTACHED = 0,
    USB_STATE_ATTACHED,
    USB_STATE_POWERED,
    USB_STATE_RECONNECTING,
    USB_STATE_UNAUTHENTICATED,
    USB_STATE_DEFAULT,
    USB_STATE_ADDRESS,
    USB_STATE_CONFIGURED,
    USB_STATE_SUSPENDED,
};

/* ─── Endpoint type ─── */
#define USB_ENDPOINT_XFER_CONTROL   0
#define USB_ENDPOINT_XFER_ISOC      1
#define USB_ENDPOINT_XFER_BULK      2
#define USB_ENDPOINT_XFER_INT       3

/* ─── Endpoint direction ─── */
#define USB_DIR_OUT   0x00
#define USB_DIR_IN    0x80

/* ─── usb_endpoint_descriptor ─── */
#pragma pack(push,1)
struct usb_endpoint_descriptor {
    u8  bLength;
    u8  bDescriptorType;
    u8  bEndpointAddress;
    u8  bmAttributes;
    u16 wMaxPacketSize;
    u8  bInterval;
};// __packed;
#pragma pack(pop)

/* ─── usb_ep ─── */
struct usb_ep {
    void* driver_data;
    const char* name;
    const struct usb_ep_ops* ops;
    struct list_head               ep_list;
    struct usb_endpoint_descriptor* desc;
    unsigned                       maxpacket : 11;
    unsigned                       maxpacket_limit : 11;
    unsigned                       max_streams : 5;
    unsigned                       mult : 2;
    unsigned                       maxburst : 5;
    u8                             address;
    bool                           caps_dir_in;
    bool                           caps_dir_out;
    bool                           caps_type_control;
    bool                           caps_type_iso;
    bool                           caps_type_bulk;
    bool                           caps_type_int;
    bool                           enabled;
    bool                           wedged;
};

/* ─── usb_request (URB equivalent at gadget layer) ─── */
struct usb_request {
    void* buf;
    unsigned             length;
    dma_addr_t           dma;
    unsigned             stream_id : 16;
    unsigned             no_interrupt : 1;
    unsigned             zero : 1;
    unsigned             short_not_ok : 1;
    void               (*complete)(struct usb_ep* ep,
        struct usb_request* req);
    void* context;
    struct list_head     list;
    unsigned             actual;
    int                  status;
};

/* ─── URB (host side) ─── */
#define URB_SHORT_NOT_OK        0x0001
#define URB_ISO_ASAP            0x0002
#define URB_NO_TRANSFER_DMA_MAP 0x0004
#define URB_ZERO_PACKET         0x0040
#define URB_NO_INTERRUPT        0x0080

struct urb {
    struct list_head        urb_list;
    struct list_head        anchor_list;
    struct usb_device* dev;
    struct usb_host_endpoint* ep;
    unsigned int            pipe;
    unsigned int            stream_id;
    int                     status;
    unsigned int            transfer_flags;
    void* transfer_buffer;
    dma_addr_t              transfer_dma;
    u32                     transfer_buffer_length;
    u32                     actual_length;
    unsigned char* setup_packet;
    dma_addr_t              setup_dma;
    int                     start_frame;
    int                     number_of_packets;
    int                     interval;
    int                     error_count;
    void* context;
    void                  (*complete)(struct urb* urb);
};

/* ─── Pipe encoding helpers ─── */
#define usb_pipein(pipe)        ((pipe) & USB_DIR_IN)
#define usb_pipeout(pipe)       (!usb_pipein(pipe))
#define usb_pipedevice(pipe)    (((pipe) >> 8) & 0x7f)
#define usb_pipeendpoint(pipe)  (((pipe) >> 15) & 0xf)
#define usb_pipetype(pipe)      (((pipe) >> 30) & 3)
#define usb_pipecontrol(pipe)   (usb_pipetype(pipe) == USB_ENDPOINT_XFER_CONTROL)
#define usb_pipebulk(pipe)      (usb_pipetype(pipe) == USB_ENDPOINT_XFER_BULK)
#define usb_pipeint(pipe)       (usb_pipetype(pipe) == USB_ENDPOINT_XFER_INT)
#define usb_pipeisoc(pipe)      (usb_pipetype(pipe) == USB_ENDPOINT_XFER_ISOC)

/* ─── usb_host_endpoint ─── */
struct usb_host_endpoint {
    struct usb_endpoint_descriptor  desc;
    struct list_head                urb_list;
    void* hcpriv;
    struct ep_device* ep_dev;
    unsigned char* extra;
    int                             extralen;
    int                             enabled;
    int                             streams;
};

/* ─── usb_device ─── */
struct usb_device {
    int                      devnum;
    enum usb_device_state    state;
    enum usb_device_speed    speed;
    struct usb_device* parent;
    struct usb_bus* bus;
    struct usb_host_endpoint ep0;
   // struct device            dev;
    u16                      descriptor_idVendor;
    u16                      descriptor_idProduct;
    char* product;
    char* manufacturer;
    char* serial;
    int                      maxchild;
    struct usb_device* children[16];
    u32                      quirks;
    void* hcpriv;
};

/* ─── usb_bus ─── */
struct usb_bus {
    struct device* controller;
    int             busnum;
    const char* bus_name;
    u8              uses_dma;
    u8              uses_pio_for_control;
    u8              otg_port;
    int             devnum_next;
    struct usb_device* root_hub;
};

#endif
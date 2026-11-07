#ifndef __LINUX_DMAENGINE_H__
#define __LINUX_DMAENGINE_H__

/*
 * DCL <linux/dmaengine.h> -- the four names struct uart_8250_dma spells out,
 * and nothing else.
 *
 * mainline's copy is 56 KB: the dma_device/dma_chan objects, the channel
 * request/release and slave-capability machinery, cyclic descriptors,
 * interleaved transfers, the whole engine. Xeneva has no DMA layer, and 8250
 * on every board in scope (iMX8MP, RPi3b+, QEMU's pl011/16550) runs PIO.
 * What the driver does need is for the *struct* to parse, because
 * 8250.h:20-44 embeds two dma_filter_fn, two dma_slave_config and two
 * dma_cookie_t as fields of uart_8250_dma -- fields whose values only matter
 * if CONFIG_SERIAL_8250_DMA code is compiled in, which it is not.
 *
 * So the four declarations below are copied from mainline rather than
 * reduced, for the reason <linux/serial_core.h>'s header comment gives: a
 * struct that disagrees with its vendor source by one field compiles cleanly
 * and then reads the wrong offset, and a type whose layout is invented here
 * is the same trap in a smaller box. The rest of the engine is absent because
 * nothing calls it -- see the same rule at the top of <linux/cleanup.h>.
 *
 *   upstream  include/linux/dmaengine.h  (mainline v7.2)
 *   extracted: enum dma_transfer_direction, enum dma_slave_buswidth,
 *              dma_cookie_t, dma_filter_fn, struct dma_slave_config
 */

/* DCL has no DMA controller; the type exists because dma_filter_fn's
 * signature names it, and every channel is NULL. */
struct dma_chan;

enum dma_transfer_direction {
	DMA_MEM_TO_MEM,
	DMA_MEM_TO_DEV,
	DMA_DEV_TO_MEM,
	DMA_DEV_TO_DEV,
	DMA_TRANS_NONE,
};

enum dma_slave_buswidth {
	DMA_SLAVE_BUSWIDTH_UNDEFINED = 0,
	DMA_SLAVE_BUSWIDTH_1_BYTE = 1,
	DMA_SLAVE_BUSWIDTH_2_BYTES = 2,
	DMA_SLAVE_BUSWIDTH_3_BYTES = 3,
	DMA_SLAVE_BUSWIDTH_4_BYTES = 4,
	DMA_SLAVE_BUSWIDTH_8_BYTES = 8,
	DMA_SLAVE_BUSWIDTH_16_BYTES = 16,
	DMA_SLAVE_BUSWIDTH_32_BYTES = 32,
	DMA_SLAVE_BUSWIDTH_64_BYTES = 64,
	DMA_SLAVE_BUSWIDTH_128_BYTES = 128,
};

typedef s32 dma_cookie_t;

typedef bool (*dma_filter_fn)(struct dma_chan *chan, void *filter_param);

struct dma_slave_config {
	enum dma_transfer_direction direction;
	phys_addr_t src_addr;
	phys_addr_t dst_addr;
	enum dma_slave_buswidth src_addr_width;
	enum dma_slave_buswidth dst_addr_width;
	u32 src_maxburst;
	u32 dst_maxburst;
	u32 src_port_window_size;
	u32 dst_port_window_size;
	bool device_fc;
	void *peripheral_config;
	size_t peripheral_size;
};

#endif /* __LINUX_DMAENGINE_H__ */

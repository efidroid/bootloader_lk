#include <err.h>
#include <debug.h>
#include <stdint.h>
#include <target.h>
#include <malloc.h>
#include <lib/bio.h>
#include <lib/heap.h>
#include <dev/keys.h>
#include <dev/fbcon.h>
#include <kernel/thread.h>
#include <platform/irqs.h>
#include <platform/timer.h>
#include <platform/iomap.h>
#include <platform/interrupts.h>
#include <atagparse.h>
#include <cmdline.h>
#include <mmc.h>
#include <partition_parser.h>
#include <dev/udc.h>
#include <arch/defines.h>
#include <stdlib.h>
#include <bootimg.h>
#include <string.h>

#include <uefiapi.h>

extern void *__ctor_list;
extern void *__ctor_end;
void call_constructors(void)
{
	void **ctor;
   
	ctor = &__ctor_list;
	while(ctor != &__ctor_end) {
		void (*func)(void);

		func = (void (*)())*ctor;

		func();
		ctor++;
	}
}

/////////////////////////////////////////////////////////////////////////
//                            PLATFORM                                 //
/////////////////////////////////////////////////////////////////////////

void api_platform_early_init(void);
void api_platform_init(void);

static void api_common_platform_early_init(void) {
	// disable all known interrupts because UEFI enables interrupts before initializing the GIC
	int i;
	for(i=0; i<NR_IRQS; i++) {
		mask_interrupt(i);
	}

	// EARLYINIT
	api_platform_early_init();

	// deal with any static constructors
	dprintf(SPEW, "calling constructors\n");
	call_constructors();

	// bring up the kernel heap
	dprintf(SPEW, "initializing heap\n");
	heap_init();

	// parse atags
	atag_parse();

#if WITH_LIB_BIO
	bio_init();
#endif

	exit_critical_section();

	// INIT
	api_platform_init();
}

__WEAK void api_platform_uninit(void) {
}

__WEAK lkapi_uefi_bootmode api_platform_get_uefi_bootmode(void) {
	switch(lkargs_get_uefi_bootmode()) {
		case LKARGS_UEFI_BM_RECOVERY:
			return LKAPI_UEFI_BM_RECOVERY;

		default:
			return LKAPI_UEFI_BM_NORMAL;
	}
}



/////////////////////////////////////////////////////////////////////////
//                              SERIAL                                 //
/////////////////////////////////////////////////////////////////////////
static int api_serial_poll_char(void) {
	return dtstc();
}

static void api_serial_write_char(char c) {
	_dputc(c);
}

static int api_serial_read_char(char* cp) {
	// return keys if available
	uint16_t code;
	uint16_t value;
	if(keys_get_next(&code, &value)==NO_ERROR) {
		*cp = (char)code;
		return 1;
	}

	// wait for next UART key
	int rc = dgetc(cp, true);
	if(rc<0) {
		return 0;
	}

	return 1;
}

/////////////////////////////////////////////////////////////////////////
//                              TIMER                                  //
/////////////////////////////////////////////////////////////////////////

static lkapi_timer_callback_t timer_callback = NULL;
static time_t timer_interval;
static volatile uint64_t perf_ticks;

static enum handler_return timer_tick(void *arg, time_t now) {
	perf_ticks += timer_interval;
	timer_callback();
	return INT_RESCHEDULE;
}

static int api_timer_register_handler(lkapi_timer_callback_t callback) {
	timer_callback = callback;
	return 0;
}

static void api_timer_set_period(unsigned long long period) {
	platform_uninit_timer();

	if(period!=0) {
		ASSERT(timer_callback);
		timer_interval = (period/10000);
		platform_set_periodic_timer(timer_tick, NULL, timer_interval);
	}
}

static void api_timer_delay_microseconds(unsigned int microseconds) {
	mdelay(microseconds/1000);
}

static void api_timer_delay_nanoseconds(unsigned int nanoseconds) {
	udelay(nanoseconds);
}

static unsigned long long api_perf_ticks(void) {
	return perf_ticks;
}

static unsigned long long api_perf_props(unsigned long long* startval, unsigned long long* endval) {
	if(startval)
		*startval = 0;

	if(endval)
		*endval = 0xffffffffffffffffULL;

	return platform_tick_rate();
}

static unsigned long long api_perf_ticks_to_ns(unsigned long long ticks) {
	return ticks * 1000000ULL;
}


/////////////////////////////////////////////////////////////////////////
//                            Interrupts                               //
/////////////////////////////////////////////////////////////////////////

static unsigned int api_int_get_dist_base(void) {
	return MSM_GIC_DIST_BASE;
}

static unsigned int api_int_get_redist_base(void) {
#ifdef MSM_GIC_REDIST_BASE
	return MSM_GIC_REDIST_BASE;
#else
	return 0;
#endif
}

static unsigned int api_int_get_cpu_base(void) {
	return MSM_GIC_CPU_BASE;
}

/////////////////////////////////////////////////////////////////////////
//                            BlockIO                                  //
/////////////////////////////////////////////////////////////////////////

#define VNOR_SIZE 0x10000

typedef struct {
	int count;
	lkapi_biodev_t* list;
} bio_iter_pdata_t;

__WEAK int api_mmc_init(void) {
	return 0;
}

static int vnor_init(void)
{
	status_t rc;

	bdev_t* dev = bio_open_by_label(DEVICE_NVVARS_PARTITION);
	if(!dev) {
		return -1;
	}

	bnum_t num_blocks = (VNOR_SIZE/dev->block_size);
	rc = bio_publish_subdevice(dev->name, "vnor", dev->block_count-num_blocks, num_blocks);

	bio_close(dev);

	return rc;
}

static int api_mmc_init_once(void) {
	static int initialized = 0;

	if(initialized)
		return 0;

	int rc = api_mmc_init();
	if(!rc) {
		initialized = 1;
		vnor_init();
	}

	return rc;
}

static int api_bio_init(lkapi_biodev_t* dev) {
	return 0;
}

static int api_bio_read(lkapi_biodev_t* dev, unsigned long long lba, unsigned long buffersize, void* buffer) {
	bdev_t* bdev = dev->api_pdata;

	if(lba>dev->num_blocks-1)
		return -1;
	if(buffersize % dev->block_size)
		return -1;
	if(lba + (buffersize/dev->block_size) > dev->num_blocks)
		return -1;
	if(!buffer)
		return -1;
	if(!buffersize)
		return 0;

	ssize_t rc = bio_read_block(bdev, buffer, lba, buffersize/dev->block_size);
	return rc != (ssize_t)buffersize;
}

static int api_bio_write(lkapi_biodev_t* dev, unsigned long long lba, unsigned long buffersize, void* buffer) {
	bdev_t* bdev = dev->api_pdata;

	if(lba>dev->num_blocks-1)
		return -1;
	if(buffersize % dev->block_size)
		return -1;
	if(lba + (buffersize/dev->block_size) > dev->num_blocks)
		return -1;
	if(!buffer)
		return -1;
	if(!buffersize)
		return 0;

	// there's no reason that UEFI should be able to do this
	// also it could be too risky because we have no experience with uncached buffers
	if(strcmp(bdev->name, "vnor"))
		ASSERT(0);

	ssize_t rc = bio_write_block(bdev, buffer, lba, buffersize/dev->block_size);
	return rc != (ssize_t)buffersize;
}

static void bio_foreach_cb(void* _pdata, const char* name) {
	bio_iter_pdata_t* pdata = _pdata;
	int is_vnor = 0;

	// open bio dev
	bdev_t* bdev = bio_open(name);
	if(!bdev) return;

	// allow vnor subdevs only
	if(bdev->is_subdev) {
		if(!strcmp(bdev->name, "vnor")) {
			is_vnor = 1;
		}
		else {
			bio_close(bdev);
			return;
		}
	}

	if(pdata->list) {
		int dev = pdata->count++;
		pdata->list[dev].type = is_vnor?LKAPI_BIODEV_TYPE_VNOR:LKAPI_BIODEV_TYPE_MMC;
		pdata->list[dev].block_size = bdev->block_size;
		pdata->list[dev].num_blocks = bdev->block_count;
		pdata->list[dev].api_pdata = bdev;
		pdata->list[dev].init = api_bio_init;
		pdata->list[dev].read = api_bio_read;
		pdata->list[dev].write = api_bio_write;
	}

	else {
		pdata->count++;
	}
}

static int api_bio_list(lkapi_biodev_t* list) {
	// initialize MMC now so we can use BIO to retrieve all devices
	api_mmc_init_once();

	bio_iter_pdata_t pdata = {
		.count = 0,
		.list = list,
	};

	bio_foreach(&bio_foreach_cb, &pdata, true);
	return pdata.count;
}

/////////////////////////////////////////////////////////////////////////
//                               LCD                                   //
/////////////////////////////////////////////////////////////////////////

static unsigned long long api_lcd_get_vram_address(void) {
	struct fbcon_config* fbcon = fbcon_display();
	return (uint32_t)fbcon->base;
}

static unsigned long long api_lcd_get_vram_size(void) {
    return LCD_VRAM_SIZE;
}

static int api_lcd_init(void) {
	target_display_init("");
	return 0;
}

static unsigned int api_lcd_get_width(void) {
	struct fbcon_config* fbcon = fbcon_display();
	return fbcon->width;
}

static unsigned int api_lcd_get_height(void) {
	struct fbcon_config* fbcon = fbcon_display();
	return fbcon->height;
}

static unsigned int api_lcd_get_density(void) {
	return LCD_DENSITY;
}

static unsigned int api_lcd_get_bpp(void) {
	struct fbcon_config* fbcon = fbcon_display();
	return fbcon->bpp;
}

static lkapi_lcd_pixelformat_t api_lcd_get_pixelformat(void) {
	struct fbcon_config* fbcon = fbcon_display();

	switch(fbcon->format) {
		case FB_FORMAT_RGB565:
			return LKAPI_LCD_PIXELFORMAT_RGB565;
		case FB_FORMAT_RGB888:
			return LKAPI_LCD_PIXELFORMAT_RGB888;
		default:
			return LKAPI_LCD_PIXELFORMAT_INVALID;
	}
}

static void api_lcd_flush(void) {
	fbcon_flush();
}

static void api_lcd_shutdown(void) {
	target_display_shutdown();
}

/////////////////////////////////////////////////////////////////////////
//                               RESET                                 //
/////////////////////////////////////////////////////////////////////////

#define NORMAL_MODE     0x77665501

static void api_reset_cold(void) {
	reboot_device(NORMAL_MODE);
}

static void api_reset_warm(void) {
	api_reset_cold();
}

static void api_reset_shutdown(void) {
	platform_halt();
}

/////////////////////////////////////////////////////////////////////////
//                               RTC                                   //
/////////////////////////////////////////////////////////////////////////

static unsigned int rtctime = 0;

static int api_rtc_init(void) {
	return 0;
}

static int api_rtc_gettime(unsigned int* time) {
	*time = rtctime + current_time()/1000;
	return 0;
}

static int api_rtc_settime(unsigned int time) {
	rtctime = time - current_time()/1000;
	return 0;
}

/////////////////////////////////////////////////////////////////////////
//                              MMAP                                   //
/////////////////////////////////////////////////////////////////////////

#include <smem.h>

typedef void* (*platform_mmap_cb_t)(void* pdata, unsigned long addr, unsigned long size, int reserved);

__WEAK void* platform_get_mmap(void* pdata, platform_mmap_cb_t cb) {
	uint32_t i;
	struct smem_ram_ptable ram_ptable;

	// Make sure RAM partition table is initialized
	ASSERT(smem_ram_ptable_init(&ram_ptable));
	for(i=0; i<ram_ptable.len; i++) {
		struct smem_ram_ptn part = ram_ptable.parts[i];

		if(part.category==SDRAM && part.type==SYS_MEMORY) {
			/* Pass along all other usable memory regions to Linux */
			pdata = cb(pdata, (paddr_t) part.start, (size_t)part.size, false);
		}
	}

	return pdata;
}

static void* api_mmap_get_dram(void* pdata, lkapi_mmap_cb_t cb) {
	return platform_get_mmap(pdata, (platform_mmap_cb_t)cb);
}

typedef struct {
	void* pdata;
	lkapi_mmap_mappings_cb_t cb;
} dram_cb_arg_t;

static void* add_dram_callback(void* pdata, unsigned long addr, unsigned long size, int reserved) {
	dram_cb_arg_t* arg = pdata;
	if(!reserved)
		arg->pdata = arg->cb(arg->pdata, addr, addr, size, LKAPI_MEMORY_WRITE_BACK);

	return pdata;
}

__WEAK void* api_mmap_get_platform_mappings(void* pdata, lkapi_mmap_mappings_cb_t cb) {
	return pdata;
}

static void* api_mmap_get_mappings(void* pdata, lkapi_mmap_mappings_cb_t cb) {
	if(platform_use_identity_mmu_mappings()) {
		// identity map
		pdata = cb(pdata, 0x0, 0x0, 0x80000000, LKAPI_MEMORY_DEVICE);
		pdata = cb(pdata, 0x80000000, 0x80000000, 0x80000000, LKAPI_MEMORY_DEVICE);
	}

	// dram map
	dram_cb_arg_t arg = {pdata, cb};
	api_mmap_get_dram(&arg, add_dram_callback);
	pdata = arg.pdata;

	// LK
	pdata = cb(pdata, MEMBASE, MEMBASE, MEMSIZE, LKAPI_MEMORY_WRITE_THROUGH);

	// platform mappings
	pdata = api_mmap_get_platform_mappings(pdata, cb);

	return pdata;
}

static void api_mmap_get_lk_range(unsigned long *addr, unsigned long *size) {
	*addr = MEMBASE;
	*size = MEMSIZE;
}

/////////////////////////////////////////////////////////////////////////
//                              BOOT                                   //
/////////////////////////////////////////////////////////////////////////

#define IS_ARM64(ptr) (ptr->magic_64 == KERNEL64_HDR_MAGIC) ? true : false

typedef void entry_func_ptr(unsigned, unsigned, unsigned*);

void generate_atags(unsigned *ptr, const char *cmdline, void *ramdisk, unsigned ramdisk_size);
unsigned char *update_cmdline(const char * cmdline);

#if DEVICE_TREE
#include <libfdt.h>
#include <dev_tree.h>

static int load_dtb(unsigned int tags_addr, unsigned int tags_size)
{
	struct dt_table *table;
	struct dt_entry dt_entry;
	uint32_t dt_hdr_size;
	unsigned int dtb_size = 0;
	unsigned char *best_match_dt_addr = NULL;

	/* offset now point to start of dt.img */
	table = (struct dt_table*)(tags_addr);

	if (dev_tree_validate(table, 2048, &dt_hdr_size) != 0) {
		dprintf(CRITICAL, "ERROR: Cannot validate Device Tree Table \n");
		return -1;
	}
	/* Find index of device tree within device tree table */
	if(dev_tree_get_entry_info(table, &dt_entry) != 0){
		dprintf(CRITICAL, "ERROR: Getting device tree address failed\n");
		return -1;
	}

	best_match_dt_addr = (unsigned char *)tags_addr + dt_entry.offset;
	dtb_size = dt_entry.size;

	/* Read device device tree in the "tags_add */
	memmove((void*) tags_addr, (void *)best_match_dt_addr, dtb_size);

	/* Everything looks fine. Return success. */
	return 0;
}
#endif

static int api_boot_create_tags(const char* cmdline, unsigned int ramdisk_addr, unsigned int ramdisk_size,
			 unsigned int tags_addr, unsigned int tags_size)
{
	char* final_cmdline;
	cmdline_addall(cmdline, false);

	int len = cmdline_length();
	final_cmdline = malloc(len);
	cmdline_generate(final_cmdline, len);

	dprintf(INFO, "cmdline: %s\n", final_cmdline);

#if DEVICE_TREE
	int ret = load_dtb(tags_addr, tags_size);
	if(ret) return -1;

	ret = update_device_tree((void *)tags_addr, final_cmdline, (void*)ramdisk_addr, ramdisk_size);
	return ret;
#else
	generate_atags((unsigned *)tags_addr, final_cmdline, (void*)ramdisk_addr, ramdisk_size);
#endif
	return 0;
}

static unsigned int api_boot_machine_type(void) {
	return board_machtype();
}

static void api_boot_update_addrs(unsigned int* kernel, unsigned int* ramdisk, unsigned int* tags) {
	/* overwrite the destination of specified for the project */
#ifdef ABOOT_IGNORE_BOOT_HEADER_ADDRS
	*kernel = ABOOT_FORCE_KERNEL_ADDR;
	*ramdisk = ABOOT_FORCE_RAMDISK_ADDR;
	*tags = ABOOT_FORCE_TAGS_ADDR;
#endif
}

static void api_boot_exec(void* kernel, unsigned int zero, unsigned int arch, unsigned int tags) {
	struct kernel64_hdr *kptr = (struct kernel64_hdr*)kernel;
	void (*entry)(unsigned, unsigned, unsigned*) = (entry_func_ptr*)kernel;

	if (IS_ARM64(kptr))
		/* Jump to a 64bit kernel */
		scm_elexec_call((paddr_t)kernel, tags);
	else
		/* Jump to a 32bit kernel */
		entry(zero, arch, (unsigned*)tags);
}

/////////////////////////////////////////////////////////////////////////
//                           USB GADGET                                //
/////////////////////////////////////////////////////////////////////////

#ifndef UEFIAPI_DISABLE_USB

#define MAX_USBFS_BULK_SIZE (32 * 1024)

typedef struct {
	int (*udc_init)(struct udc_device *devinfo);
	int (*udc_register_gadget)(struct udc_gadget *gadget);
	int (*udc_start)(void);
	int (*udc_stop)(void);

	struct udc_endpoint *(*udc_endpoint_alloc)(unsigned type, unsigned maxpkt);
	void (*udc_endpoint_free)(struct udc_endpoint *ept);
	struct udc_request *(*udc_request_alloc)(void);
	void (*udc_request_free)(struct udc_request *req);

	int (*usb_read)(void *buf, unsigned len);
	int (*usb_write)(void *buf, unsigned len);
} usb_controller_interface_t;

typedef struct {
	void *buffer;
	unsigned len;

	unsigned xfer;
	unsigned char *bufptr;
	int count;

	int txn_status;

	struct udc_endpoint* ept;
	struct udc_request *req;
	lkapi_usb_gadget_event event_success;
	lkapi_usb_gadget_event event_error;
} udc_req_context_t;

static udc_req_context_t udc_read_context = {0};
static udc_req_context_t udc_write_context = {0};
static lkapi_usb_gadget_gadget* lkapi_gadget = NULL;
static struct udc_device udc_device = {0};
static struct udc_gadget udc_gadget = {0};
static usb_controller_interface_t usb_if;
static struct udc_endpoint *in, *out;
static struct udc_request *reqin, *reqout;
static struct udc_endpoint *fastboot_endpoints[2];

static void hsusb_usb_req_internal(udc_req_context_t* context);

static void udc_status_notify(struct udc_gadget *gadget, unsigned event)
{
	if (event == UDC_EVENT_ONLINE) {
		lkapi_gadget->notify(lkapi_gadget, LKAPI_USB_GADGET_EVENT_ONLINE, NULL);
	}
	else if (event == UDC_EVENT_OFFLINE) {
		lkapi_gadget->notify(lkapi_gadget, LKAPI_USB_GADGET_EVENT_OFFLINE, NULL);
	}
}

static void req_complete(struct udc_request *req, unsigned actual, int status)
{
	udc_req_context_t* context = req->context;

	context->txn_status = status;
	req->length = actual;

	if (context->txn_status < 0) {
		dprintf(CRITICAL, "usb_read() transaction failed\n");
		lkapi_gadget->notify(lkapi_gadget, context->event_error, NULL);
		return;
	}

	context->count += req->length;
	context->bufptr += req->length;
	context->len -= req->length;

	// short transfer?
	if (req->length != context->xfer) {
		if (context->count <= 0) {
			lkapi_gadget->notify(lkapi_gadget, context->event_error, NULL);
		}
		else {
			// invalidate any cached buf data (controller updates main memory)
			if(context->ept==out)
				arch_invalidate_cache_range((addr_t)context->buffer, ROUNDUP(context->count, CACHE_LINE));
			lkapi_gadget->notify(lkapi_gadget, context->event_success, &context->count);
		}

		return;
	}

	hsusb_usb_req_internal(context);
}

static void hsusb_usb_req_internal(udc_req_context_t* context) {
	int rc;
	struct udc_request *req = context->req;

	if(context->len>0) {
		// queue request
		context->xfer = (context->len > MAX_USBFS_BULK_SIZE) ? MAX_USBFS_BULK_SIZE : context->len;
		req->buf = context->bufptr;
		req->length = context->xfer;
		req->complete = req_complete;
		req->context = context;
		rc = udc_request_queue(context->ept, req);
		if (rc < 0) {
			dprintf(CRITICAL, "udc_request_queue() failed\n");
			lkapi_gadget->notify(lkapi_gadget, context->event_error, NULL);
		}
	}

	else {
		// invalidate any cached buf data (controller updates main memory)
		if(context->ept==out)
			arch_invalidate_cache_range((addr_t)context->buffer, ROUNDUP(context->count, CACHE_LINE));
		lkapi_gadget->notify(lkapi_gadget, context->event_success, &context->count);
	}
}

static int hsusb_usb_read(void* buffer, unsigned int len) {
	memset(&udc_read_context, 0, sizeof(udc_req_context_t));

	// flush buffer to main memory before giving to udc
	arch_clean_invalidate_cache_range((addr_t) buffer, ROUNDUP(len, CACHE_LINE));

	udc_read_context.buffer = buffer;
	udc_read_context.len = len;
	udc_read_context.bufptr = buffer;
	udc_read_context.count = 0;
	udc_read_context.event_success = LKAPI_USB_GADGET_EVENT_READ_SUCCESS;
	udc_read_context.event_error = LKAPI_USB_GADGET_EVENT_READ_ERROR;
	udc_read_context.req = reqout;
	udc_read_context.ept = out;

	hsusb_usb_req_internal(&udc_read_context);
	return 0;
}

static int hsusb_usb_write(void* buffer, unsigned int len) {
	memset(&udc_write_context, 0, sizeof(udc_req_context_t));

	udc_write_context.buffer = buffer;
	udc_write_context.len = len;
	udc_write_context.bufptr = buffer;
	udc_write_context.count = 0;
	udc_write_context.event_success = LKAPI_USB_GADGET_EVENT_WRITE_SUCCESS;
	udc_write_context.event_error = LKAPI_USB_GADGET_EVENT_WRITE_ERROR;
	udc_write_context.req = reqin;
	udc_write_context.ept = in;

	// flush buffer to main memory before giving to udc
	arch_clean_invalidate_cache_range((addr_t) buffer, ROUNDUP(len, CACHE_LINE));

	hsusb_usb_req_internal(&udc_write_context);
	return 0;
}

static int api_usbgadget_init(lkapi_usb_gadget_device* device) {
	/* target specific initialization before going into fastboot. */
	target_fastboot_init();

	udc_device.vendor_id = device->vendor_id;
	udc_device.product_id = device->product_id;
	udc_device.version_id = device->version_id;
	udc_device.manufacturer = device->manufacturer;
	udc_device.product = device->product;
	udc_device.serialno = "EFIDroid";

	if(!strcmp(target_usb_controller(), "dwc"))
	{
#ifdef USB30_SUPPORT
		surf_udc_device.t_usb_if = target_usb30_init();

		/* initialize udc functions to use dwc controller */
		usb_if.udc_init            = usb30_udc_init;
		usb_if.udc_register_gadget = usb30_udc_register_gadget;
		usb_if.udc_start           = usb30_udc_start;
		usb_if.udc_stop            = usb30_udc_stop;

		usb_if.udc_endpoint_alloc  = usb30_udc_endpoint_alloc;
		usb_if.udc_request_alloc   = usb30_udc_request_alloc;
		usb_if.udc_request_free    = usb30_udc_request_free;

		usb_if.usb_read            = usb30_usb_read;
		usb_if.usb_write           = usb30_usb_write;
#else
		return -1;
#endif
	}
	else
	{
		/* initialize udc functions to use the default chipidea controller */
		usb_if.udc_init            = udc_init;
		usb_if.udc_register_gadget = udc_register_gadget;
		usb_if.udc_start           = udc_start;
		usb_if.udc_stop            = udc_stop;

		usb_if.udc_endpoint_alloc  = udc_endpoint_alloc;
		usb_if.udc_request_alloc   = udc_request_alloc;
		usb_if.udc_request_free    = udc_request_free;

		usb_if.usb_read            = hsusb_usb_read;
		usb_if.usb_write           = hsusb_usb_write;
	}

	/* register udc device */
	usb_if.udc_init(&udc_device);

	return 0;
}

static int api_usbgadget_register_gadget(lkapi_usb_gadget_gadget* gadget) {
	udc_gadget.notify = udc_status_notify;
	udc_gadget.ifc_class = gadget->ifc_class;
	udc_gadget.ifc_subclass = gadget->ifc_subclass;
	udc_gadget.ifc_protocol = gadget->ifc_protocol;
	udc_gadget.ifc_endpoints = 2;
	udc_gadget.ifc_string = gadget->ifc_string;
	udc_gadget.ept = fastboot_endpoints;

	in = usb_if.udc_endpoint_alloc(UDC_TYPE_BULK_IN, 512);
	if (!in)
		goto fail_alloc_in;
	out = usb_if.udc_endpoint_alloc(UDC_TYPE_BULK_OUT, 512);
	if (!out)
		goto fail_alloc_out;

	fastboot_endpoints[0] = in;
	fastboot_endpoints[1] = out;

	reqin = usb_if.udc_request_alloc();
	if (!reqin)
		goto fail_alloc_reqin;

	reqout = usb_if.udc_request_alloc();
	if (!reqout)
		goto fail_alloc_reqout;

	/* register gadget */
	if (usb_if.udc_register_gadget(&udc_gadget))
		goto fail_udc_register;

	lkapi_gadget = gadget;

	return 0;

fail_udc_register:
	usb_if.udc_request_free(reqout);
fail_alloc_reqout:
	usb_if.udc_request_free(reqin);
fail_alloc_reqin:
	usb_if.udc_endpoint_free(out);
fail_alloc_out:
	usb_if.udc_endpoint_free(in);
fail_alloc_in:
	return -1;
}

static int api_usbgadget_start(void) {
	return usb_if.udc_start();
}

static int api_usbgadget_stop(void) {
	return usb_if.udc_stop();
}

static void* api_usbgadget_alloc(unsigned int size) {
	return memalign(CACHE_LINE, ROUNDUP(size, CACHE_LINE));
}

static int api_usbgadget_free(void* buffer) {
	free(buffer);
	return 0;
}

static int api_usbgadget_read(void* buffer, unsigned int len) {
	return usb_if.usb_read(buffer, len);
}

static int api_usbgadget_write(void* buffer, unsigned int len) {
	return usb_if.usb_write(buffer, len);
}
#endif


/////////////////////////////////////////////////////////////////////////
//                            API TABLE                                //
/////////////////////////////////////////////////////////////////////////

lkapi_t uefiapi = {
	.platform_early_init = api_common_platform_early_init,
	.platform_uninit = api_platform_uninit,
	.platform_get_uefi_bootmode = api_platform_get_uefi_bootmode,

	.serial_poll_char = api_serial_poll_char,
	.serial_write_char = api_serial_write_char,
	.serial_read_char = api_serial_read_char,

	.timer_register_handler = api_timer_register_handler,
	.timer_set_period = api_timer_set_period,
	.timer_delay_microseconds = api_timer_delay_microseconds,
	.timer_delay_nanoseconds = api_timer_delay_nanoseconds,

	.perf_ticks = api_perf_ticks,
	.perf_props = api_perf_props,
	.perf_ticks_to_ns = api_perf_ticks_to_ns,

	.int_mask = NULL,
	.int_unmask = NULL,
	.int_register_handler = NULL,
	.int_get_dist_base = api_int_get_dist_base,
	.int_get_redist_base = api_int_get_redist_base,
	.int_get_cpu_base = api_int_get_cpu_base,

	.bio_list = api_bio_list,

	.lcd_get_vram_address = api_lcd_get_vram_address,
	.lcd_get_vram_size = api_lcd_get_vram_size,
	.lcd_init = api_lcd_init,
	.lcd_get_width = api_lcd_get_width,
	.lcd_get_height = api_lcd_get_height,
	.lcd_get_density = api_lcd_get_density,
	.lcd_get_bpp = api_lcd_get_bpp,
	.lcd_get_pixelformat = api_lcd_get_pixelformat,
	.lcd_flush = api_lcd_flush,
	.lcd_shutdown = api_lcd_shutdown,

	.reset_cold = api_reset_cold,
	.reset_warm = api_reset_warm,
	.reset_shutdown = api_reset_shutdown,

	.rtc_init = api_rtc_init,
	.rtc_gettime = api_rtc_gettime,
	.rtc_settime = api_rtc_settime,

	.mmap_get_dram = api_mmap_get_dram,
	.mmap_get_mappings = api_mmap_get_mappings,
	.mmap_get_lk_range = api_mmap_get_lk_range,

	.boot_create_tags = api_boot_create_tags,
	.boot_machine_type = api_boot_machine_type,
	.boot_update_addrs = api_boot_update_addrs,
	.boot_exec = api_boot_exec,

	.event_init = NULL,
	.event_destroy = NULL,
	.event_wait = NULL,
	.event_signal = NULL,

#ifndef UEFIAPI_DISABLE_USB
	.usbgadget_init = api_usbgadget_init,
	.usbgadget_register_gadget = api_usbgadget_register_gadget,
	.usbgadget_start = api_usbgadget_start,
	.usbgadget_stop = api_usbgadget_stop,
	.usbgadget_alloc = api_usbgadget_alloc,
	.usbgadget_free = api_usbgadget_free,
	.usbgadget_read = api_usbgadget_read,
	.usbgadget_write = api_usbgadget_write,
#endif
};

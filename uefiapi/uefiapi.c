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
#include <lib/atagparse.h>
#include <lib/cmdline.h>
#include <mmc.h>
#include <partition_parser.h>
#include <arch/defines.h>
#include <stdlib.h>
#include <bootimg.h>
#include <string.h>
#include <kernel/event.h>

#include <uefiapi.h>

static int in_lkapi_call = 0;

uint32_t uefi_entry_check(void) {
    in_lkapi_call++;

    if(in_lkapi_call==1)
        critical_section_count = !arch_ints_enabled();
    return critical_section_count;
}

void uefi_exit_check(uint32_t prev) {
    in_lkapi_call--;

    if(in_lkapi_call==0) {
        ASSERT((int)prev==critical_section_count);
        //ASSERT(arch_ints_enabled()==!critical_section_count);
    }
}

extern void *__ctor_list;
extern void *__ctor_end;
static void call_constructors(void)
{
    void **ctor;

    ctor = &__ctor_list;
    while (ctor != &__ctor_end) {
        void (*func)(void);

        func = (void (*)(void))*ctor;

        func();
        ctor++;
    }
}

/////////////////////////////////////////////////////////////////////////
//                            PLATFORM                                 //
/////////////////////////////////////////////////////////////////////////

#include <qgic.h>

__WEAK void uefiapi_platform_init_post(void) {

}

void platform_uninit(void);
void target_uninit(void);

extern int uefiapi_fn_wrapper_template;
extern int uefiapi_fn_wrapper_template_end;

void* uefiapi_make_fn_wrapper(void* fn) {
    void* template = &uefiapi_fn_wrapper_template;
    void* template_end = &uefiapi_fn_wrapper_template_end;
    uint32_t template_size = template_end-template;

    // allocate code
    void* newfn = memalign(CACHE_LINE, template_size);
    ASSERT(newfn);

    // copy code
    memcpy(newfn, template, template_size);

    // set original function pointer
    void** fnptr = newfn+template_size-sizeof(void*)*3;
    fnptr[0] = uefi_entry_check;
    fnptr[1] = fn;
    fnptr[2] = uefi_exit_check;

    return newfn;
}

static void generate_uefiapi_fn_wrappers(void) {
    void** table = (void**)&uefiapi;
    uint32_t size = sizeof(uefiapi)/sizeof(void*);

    // verify size
    ASSERT(sizeof(uefiapi)%sizeof(void*)==0);

    uint32_t i;
    for(i=0; i<size; i++) {
        // set new function pointer
        table[i] = uefiapi_make_fn_wrapper(table[i]);
    }
}

static void api_platform_early_init(void)
{
    uint32_t prev = uefi_entry_check();

    // disable all known interrupts because UEFI enables interrupts before initializing the GIC
    int i;
    for (i=0; i<NR_IRQS; i++) {
        gic_mask_interrupt(i);
    }

	// do any super early platform initialization
	platform_early_init();

	// do any super early target initialization
	target_early_init();

	dprintf(INFO, "welcome to lk\n\n");

    // deal with any static constructors
    dprintf(SPEW, "calling constructors\n");
    call_constructors();

    // bring up the kernel heap
    dprintf(SPEW, "initializing heap\n");
    heap_init();

    //__stack_chk_guard_setup();

#if WITH_LIB_ATAGPARSE
    atag_parse();
#endif

    generate_uefiapi_fn_wrappers();

    uefi_exit_check(prev);
}

static void api_platform_init(void) {
#if WITH_LIB_BIO
    bio_init();
#endif

    // initialize the rest of the platform
	dprintf(SPEW, "initializing platform\n");
	platform_init();

	// initialize the target
	dprintf(SPEW, "initializing target\n");
	target_init();

    // target specific INIT
    uefiapi_platform_init_post();

#if DEBUG_ENABLE_UEFI_FBCON
    target_display_init("");
#endif
}

__WEAK void api_platform_uninit(void)
{
    target_uninit();

	/* do any platform specific cleanup before kernel entry */
	platform_uninit();
}

__WEAK lkapi_uefi_bootmode api_platform_get_uefi_bootmode(void)
{
    switch (lkargs_get_uefi_bootmode()) {
        case LKARGS_UEFI_BM_RECOVERY:
            return LKAPI_UEFI_BM_RECOVERY;

        default:
            return LKAPI_UEFI_BM_NORMAL;
    }
}



/////////////////////////////////////////////////////////////////////////
//                              SERIAL                                 //
/////////////////////////////////////////////////////////////////////////
static int api_serial_poll_char(void)
{
    return dtstc();
}

static void api_serial_write_char(char c)
{
    _dputc(c);
}

static int api_serial_read_char(char *cp)
{
    // return keys if available
    uint16_t code;
    uint16_t value;
    if (keys_get_next(&code, &value)==NO_ERROR) {
        *cp = (char)code;
        return 1;
    }

    // wait for next UART key
    int rc = dgetc(cp, true);
    if (rc<0) {
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

static enum handler_return timer_tick(void *arg, time_t now)
{
    perf_ticks += timer_interval;
    timer_callback();
    return INT_RESCHEDULE;
}

static int api_timer_register_handler(lkapi_timer_callback_t callback)
{
    timer_callback = callback;
    return 0;
}

static void api_timer_set_period(unsigned long long period)
{
    platform_uninit_timer();

    if (period!=0) {
        ASSERT(timer_callback);
        timer_interval = (period/10000);
        platform_set_periodic_timer(timer_tick, NULL, timer_interval);
    }
}

static void api_timer_delay_microseconds(unsigned int microseconds)
{
    mdelay(microseconds/1000);
}

static void api_timer_delay_nanoseconds(unsigned int nanoseconds)
{
    udelay(nanoseconds);
}

static unsigned long long api_perf_ticks(void)
{
    return perf_ticks;
}

static unsigned long long api_perf_props(unsigned long long *startval, unsigned long long *endval)
{
    if (startval)
        *startval = 0;

    if (endval)
        *endval = 0xffffffffffffffffULL;

    return platform_tick_rate();
}

static unsigned long long api_perf_ticks_to_ns(unsigned long long ticks)
{
    return ticks * 1000000ULL;
}


/////////////////////////////////////////////////////////////////////////
//                            Interrupts                               //
/////////////////////////////////////////////////////////////////////////

static unsigned int api_int_get_dist_base(void)
{
    return MSM_GIC_DIST_BASE;
}

static unsigned int api_int_get_redist_base(void)
{
#ifdef MSM_GIC_REDIST_BASE
    return MSM_GIC_REDIST_BASE;
#else
    return 0;
#endif
}

static unsigned int api_int_get_cpu_base(void)
{
    return MSM_GIC_CPU_BASE;
}

/////////////////////////////////////////////////////////////////////////
//                            BlockIO                                  //
/////////////////////////////////////////////////////////////////////////

#define VNOR_SIZE 0x10000

typedef struct {
    int count;
    lkapi_biodev_t *list;
} bio_iter_pdata_t;

static int vnor_init(void)
{
    status_t rc;

    bdev_t *dev = bio_open_by_label(DEVICE_NVVARS_PARTITION);
    if (!dev) {
        return -1;
    }

    bnum_t num_blocks = (VNOR_SIZE/dev->block_size);
    rc = bio_publish_subdevice(dev->name, "vnor", dev->block_count-num_blocks, num_blocks);

    bio_close(dev);

    return rc;
}

static int api_bio_init(lkapi_biodev_t *dev)
{
    return 0;
}

static int api_bio_read(lkapi_biodev_t *dev, unsigned long long lba, unsigned long buffersize, void *buffer)
{
    bdev_t *bdev = dev->api_pdata;

    if (lba>dev->num_blocks-1)
        return -1;
    if (buffersize % dev->block_size)
        return -1;
    if (lba + (buffersize/dev->block_size) > dev->num_blocks)
        return -1;
    if (!buffer)
        return -1;
    if (!buffersize)
        return 0;

    ssize_t rc = bio_read_block(bdev, buffer, lba, buffersize/dev->block_size);
    return rc != (ssize_t)buffersize;
}

static int api_bio_write(lkapi_biodev_t *dev, unsigned long long lba, unsigned long buffersize, void *buffer)
{
    bdev_t *bdev = dev->api_pdata;

    if (lba>dev->num_blocks-1)
        return -1;
    if (buffersize % dev->block_size)
        return -1;
    if (lba + (buffersize/dev->block_size) > dev->num_blocks)
        return -1;
    if (!buffer)
        return -1;
    if (!buffersize)
        return 0;

    // there's no reason that UEFI should be able to do this
    // also it could be too risky because we have no experience with uncached buffers
    //if(strcmp(bdev->name, "vnor"))
    //  ASSERT(0);

    ssize_t rc = bio_write_block(bdev, buffer, lba, buffersize/dev->block_size);
    return rc != (ssize_t)buffersize;
}

static void bio_foreach_cb(void *_pdata, const char *name)
{
    bio_iter_pdata_t *pdata = _pdata;
    int is_vnor = 0;

    // open bio dev
    bdev_t *bdev = bio_open(name);
    if (!bdev) return;

    // allow vnor subdevs only
    if (bdev->is_subdev) {
        if (!strcmp(bdev->name, "vnor")) {
            is_vnor = 1;
        } else {
            bio_close(bdev);
            return;
        }
    }

    if (pdata->list) {
        int dev = pdata->count++;
        pdata->list[dev].id = -1;
        pdata->list[dev].type = is_vnor?LKAPI_BIODEV_TYPE_VNOR:LKAPI_BIODEV_TYPE_MMC;
        pdata->list[dev].block_size = bdev->block_size;
        pdata->list[dev].num_blocks = bdev->block_count;
        pdata->list[dev].api_pdata = bdev;
        pdata->list[dev].init = api_bio_init;
        pdata->list[dev].read = api_bio_read;
        pdata->list[dev].write = api_bio_write;

        uint32_t namelen = strlen(name);
        if(namelen>=3 && name[0]=='h' && name[1]=='d')
            pdata->list[dev].id = atoi(name+2);
    }

    else {
        pdata->count++;
    }
}

static int api_bio_list(lkapi_biodev_t *list)
{
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

static unsigned long long api_lcd_get_vram_address(void)
{
    struct fbcon_config *fbcon = fbcon_display();
    return (uint32_t)fbcon->base;
}

static unsigned long long api_lcd_get_vram_size(void)
{
    return LCD_VRAM_SIZE;
}

static int api_lcd_init(void)
{
#if !DEBUG_ENABLE_UEFI_FBCON
    target_display_init("");
#endif
    return 0;
}

static unsigned int api_lcd_get_width(void)
{
    struct fbcon_config *fbcon = fbcon_display();
    return fbcon->width;
}

static unsigned int api_lcd_get_height(void)
{
    struct fbcon_config *fbcon = fbcon_display();
    return fbcon->height;
}

static unsigned int api_lcd_get_density(void)
{
    return LCD_DENSITY;
}

static unsigned int api_lcd_get_bpp(void)
{
    struct fbcon_config *fbcon = fbcon_display();
    return fbcon->bpp;
}

static lkapi_lcd_pixelformat_t api_lcd_get_pixelformat(void)
{
    struct fbcon_config *fbcon = fbcon_display();

    switch (fbcon->format) {
        case FB_FORMAT_RGB565:
            return LKAPI_LCD_PIXELFORMAT_RGB565;
        case FB_FORMAT_RGB888:
            return LKAPI_LCD_PIXELFORMAT_RGB888;
        default:
            return LKAPI_LCD_PIXELFORMAT_INVALID;
    }
}

static int api_lcd_needs_flush(void)
{
    struct fbcon_config *fbcon = fbcon_display();
    return (fbcon->update_start || fbcon->update_done);
}

static void api_lcd_flush(void)
{
    fbcon_flush();
}

static void api_lcd_shutdown(void)
{
#if DISPLAY_SPLASH_SCREEN
	target_display_shutdown();
#endif
}

/////////////////////////////////////////////////////////////////////////
//                               RESET                                 //
/////////////////////////////////////////////////////////////////////////

#define NORMAL_MODE          0x77665501
#define RECOVERY_MODE        0x77665502
#define FASTBOOT_MODE        0x77665500

static void api_reset_cold(const char *reason)
{
    uint32_t reboot_reason = 0;
    if (reason) {
        if (!strcmp(reason, "recovery"))
            reboot_reason = RECOVERY_MODE;
        else if (!strcmp(reason, "bootloader"))
            reboot_reason = FASTBOOT_MODE;
        else if (!strcmp(reason, "download")) {
            if (set_download_mode(EMERGENCY_DLOAD)==0) {
                reboot_device(DLOAD);
            }
            return;
        } else return;
    }

    reboot_device(reboot_reason);
}

static void api_reset_warm(const char *reason)
{
    api_reset_cold(reason);
}

static void api_reset_shutdown(const char *reason)
{
    shutdown_device();
}

/////////////////////////////////////////////////////////////////////////
//                               RTC                                   //
/////////////////////////////////////////////////////////////////////////

static unsigned int rtctime = 0;

static int api_rtc_init(void)
{
    return 0;
}

static int api_rtc_gettime(unsigned int *time)
{
    *time = rtctime + current_time()/1000;
    return 0;
}

static int api_rtc_settime(unsigned int time)
{
    rtctime = time - current_time()/1000;
    return 0;
}

/////////////////////////////////////////////////////////////////////////
//                              MMAP                                   //
/////////////////////////////////////////////////////////////////////////

#include <platform.h>

__WEAK void *platform_get_mmap(void *pdata, platform_mmap_cb_t cb)
{
    return lkargs_get_mmap_callback(pdata, (platform_mmap_cb_t)cb);
}

static void *api_mmap_get_dram(void *pdata, lkapi_mmap_cb_t cb)
{
    return platform_get_mmap(pdata, (platform_mmap_cb_t)cb);
}

typedef struct {
    void *pdata;
    lkapi_mmap_mappings_cb_t cb;
} dram_cb_arg_t;

static void *add_dram_callback(void *pdata, unsigned long long addr, unsigned long long size, int reserved)
{
    dram_cb_arg_t *arg = pdata;
    if (!reserved)
        arg->pdata = arg->cb(arg->pdata, addr, addr, size, LKAPI_MEMORY_WRITE_BACK);

    return pdata;
}

__WEAK void *api_mmap_get_platform_mappings(void *pdata, lkapi_mmap_mappings_cb_t cb)
{
    return pdata;
}

static void *api_mmap_get_mappings(void *pdata, lkapi_mmap_mappings_cb_t cb)
{
    if (platform_use_identity_mmu_mappings()) {
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

__WEAK void *api_mmap_get_platform_lkmem(void *pdata, lkapi_mmap_lkmem_cb_t cb) {
    return pdata;
}

static void *api_mmap_get_lkmem(void *pdata, lkapi_mmap_lkmem_cb_t cb)
{
    pdata = cb(pdata, MEMBASE, MEMSIZE);
    pdata = api_mmap_get_platform_lkmem(pdata, cb);

    return pdata;
}

/////////////////////////////////////////////////////////////////////////
//                              BOOT                                   //
/////////////////////////////////////////////////////////////////////////

#include <board.h>

#define IS_ARM64(ptr) (ptr->magic_64 == KERNEL64_HDR_MAGIC) ? true : false

typedef void entry_func_ptr(unsigned, unsigned, unsigned *);

static void api_boot_update_addrs(unsigned int *kernel, unsigned int *ramdisk, unsigned int *tags)
{
    /* overwrite the destination of specified for the project */
#ifdef ABOOT_IGNORE_BOOT_HEADER_ADDRS
    *kernel = ABOOT_FORCE_KERNEL_ADDR;
    *ramdisk = ABOOT_FORCE_RAMDISK_ADDR;
    *tags = ABOOT_FORCE_TAGS_ADDR;
#endif
}

static void api_boot_exec(void *kernel, unsigned int zero, unsigned int arch, unsigned int tags)
{
    struct kernel64_hdr *kptr = (struct kernel64_hdr *)kernel;
    void (*entry)(unsigned, unsigned, unsigned *) = (entry_func_ptr *)kernel;

    if (IS_ARM64(kptr))
        /* Jump to a 64bit kernel */
        scm_elexec_call((paddr_t)kernel, tags);
    else
        /* Jump to a 32bit kernel */
        entry(zero, arch, (unsigned *)tags);
}

static unsigned int api_boot_get_machine_type(void) {
    return board_target_id();
}

static const char* api_boot_get_cmdline_extension(void) {
    return lkargs_get_command_line();
}

static void* api_boot_extend_atags(void *atags) {
    return lkargs_atag_insert_unknown(atags);
}

static void api_boot_extend_fdt(void *fdt) {
#if DEVICE_TREE
    lkargs_insert_chosen(fdt);
#endif
}

static unsigned int api_boot_get_pmic_target(unsigned short num_ent) {
    return board_pmic_target(num_ent);
}

static unsigned int api_boot_get_platform_id(void) {
#if DEVICE_TREE
    if(lkargs_has_board_info())
        return lkargs_get_platform_id();
    else
#endif
        return board_platform_id();
}

static unsigned int api_boot_get_hardware_id(void) {
#if DEVICE_TREE
    if(lkargs_has_board_info())
        return lkargs_get_variant_id();
    else
#endif
        return board_hardware_id();
}

static unsigned int api_boot_get_hardware_subtype(void) {
#if DEVICE_TREE
    if(lkargs_has_board_info() && lkargs_board_info_version()>1)
		return lkargs_get_hw_subtype();
    else
#endif
        return board_hardware_subtype();
}

static unsigned int api_boot_get_soc_version(void) {
#if DEVICE_TREE
    if(lkargs_has_board_info())
        return lkargs_get_soc_rev();
    else
#endif
        return board_soc_version();
}

static unsigned int api_boot_get_target_id(void) {
    return board_target_id();
}

static unsigned int api_boot_get_foundry_id(void) {
    return board_foundry_id();
}

static unsigned int api_boot_get_hlos_subtype(void) {
    return target_get_hlos_subtype();
}


/////////////////////////////////////////////////////////////////////////
//                           USB GADGET                                //
/////////////////////////////////////////////////////////////////////////

#include <dev/udc.h>

#ifdef USB30_SUPPORT
#include <usb30_udc.h>
#endif

#define MAX_USBFS_BULK_SIZE (32 * 1024)
#define MAX_USBSS_BULK_SIZE (0x1000000)

// this limits the USB3 API to the fastboot protocol
#define MAX_RSP_SIZE            64

typedef struct {
    int (*udc_init)(struct udc_device *devinfo);
    int (*udc_register_gadget)(struct udc_gadget *gadget);
    int (*udc_start)(void);
    int (*udc_stop)(void);

    struct udc_endpoint *(*udc_endpoint_alloc)(unsigned type, unsigned maxpkt);
    void (*udc_endpoint_free)(struct udc_endpoint *ept);
    struct udc_request *(*udc_request_alloc)(void);
    void (*udc_request_free)(struct udc_request *req);
} usb_controller_interface_t;

typedef struct {
    lkapi_udc_gadget_t *lk_udc_gadget;
    struct udc_request *req;
    event_t txn_done;
    int txn_status;
    int errno;

    int (*usb_read)(struct udc_gadget *udc_gadget, void *buf, unsigned len);
    int (*usb_write)(struct udc_gadget *udc_gadget, void *buf, unsigned len);
} udc_gadget_context_t;

static void req_complete(struct udc_request *req, unsigned actual, int status)
{
    udc_gadget_context_t *context = req->context;

    context->txn_status = status;
    req->length = actual;

    event_signal(&context->txn_done, 0);
}

#ifdef USB30_SUPPORT
static int usb30_usb_read(struct udc_gadget *udc_gadget, void *_buf, unsigned len)
{
    int r;
    struct udc_request req;
    uint32_t xfer;
    int count = 0;
    uint32_t trans_len = len;
    const char *buf = _buf;
    udc_gadget_context_t *context = udc_gadget->context;
    struct udc_endpoint *in = udc_gadget->ept[0];
    struct udc_endpoint *out = udc_gadget->ept[1];

    ASSERT(buf);
    ASSERT(len);

    if (context->errno)
        goto oops;

    dprintf(SPEW, "usb_read(): len = %d\n", len);

    while (len > 0) {
        xfer = (len > MAX_USBSS_BULK_SIZE) ? MAX_USBSS_BULK_SIZE : len;

        req.buf      = (void *) PA((addr_t)buf);
        req.length   = xfer;
        req.complete = req_complete;
        req.context  = context;

        r = usb30_udc_request_queue(out, &req);
        if (r < 0) {
            dprintf(CRITICAL, "usb_read() queue failed. r = %d\n", r);
            goto oops;
        }
        event_wait(&context->txn_done);

        if (context->txn_status < 0) {
            dprintf(CRITICAL, "usb_read() transaction failed. txn_status = %d\n",
                    context->txn_status);
            goto oops;
        }

        count += req.length;
        buf += req.length;
        len -= req.length;

        /* note: req.length is update by callback to reflect the amount of data
         * actually read.
         */
        dprintf(SPEW, "usb_read(): DONE. req.length = %d\n\n", req.length);

        /* For USB3.0 if the data transfer is less than MaxpacketSize, its
         * short packet and DWC layer generates transfer complete. App layer
         * shold handle this and continue trasnferring the data instead of treating
         * this as a transfer complete. This case is not applicable for transfers
         * which involve protocol communication to exchange information whose length
         * is always equal to MAX_RSP_SIZE. This check ensures that we dont abort
         * data transfers on short packet.
         */
        if (req.length != xfer && trans_len == MAX_RSP_SIZE) break;
    }

    /* invalidate any cached buf data (controller updates main memory) */
    arch_invalidate_cache_range((addr_t) _buf, count);

    return count;

oops:
    context->errno = -1;
    dprintf(CRITICAL, "usb_read(): DONE: ERROR: len = %d\n", len);
    return -1;
}

static int usb30_usb_write(struct udc_gadget *udc_gadget, void *buf, unsigned len)
{
    int r;
    struct udc_request req;
    udc_gadget_context_t *context = udc_gadget->context;
    struct udc_endpoint *in = udc_gadget->ept[0];
    struct udc_endpoint *out = udc_gadget->ept[1];

    ASSERT(buf);
    ASSERT(len);

    if (context->errno)
        goto oops;

    dprintf(SPEW, "usb_write(): len = %d str = %s\n", len, (char *) buf);

    /* flush buffer to main memory before giving to udc */
    arch_clean_invalidate_cache_range((addr_t) buf, len);

    req.buf      = (void *) PA((addr_t)buf);
    req.length   = len;
    req.complete = req_complete;
    req.context  = context;

    r = usb30_udc_request_queue(in, &req);
    if (r < 0) {
        dprintf(CRITICAL, "usb_write() queue failed. r = %d\n", r);
        goto oops;
    }
    event_wait(&context->txn_done);

    dprintf(SPEW, "usb_write(): DONE: len = %d req->length = %d str = %s\n",
            len, req.length, (char *) buf);

    if (context->txn_status < 0) {
        dprintf(CRITICAL, "usb_write() transaction failed. txn_status = %d\n",
                context->txn_status);
        goto oops;
    }

    return req.length;

oops:
    context->errno = -1;
    dprintf(CRITICAL, "usb_write(): DONE: ERROR: len = %d\n", len);
    return -1;
}
#endif

static int hsusb_usb_read(struct udc_gadget *udc_gadget, void *_buf, unsigned len)
{
    int r;
    unsigned xfer;
    unsigned char *buf = _buf;
    int count = 0;
    udc_gadget_context_t *context = udc_gadget->context;
    struct udc_endpoint *out = udc_gadget->ept[1];
    struct udc_request *req = context->req;

    if (context->errno)
        goto oops;

    while (len > 0) {
        xfer = (len > MAX_USBFS_BULK_SIZE) ? MAX_USBFS_BULK_SIZE : len;
        req->buf = (unsigned char *)PA((addr_t)buf);
        req->length = xfer;
        req->complete = req_complete;
        req->context = context;
        r = udc_request_queue(out, req);
        if (r < 0) {
            dprintf(INFO, "usb_read() queue failed\n");
            goto oops;
        }
        event_wait(&context->txn_done);

        if (context->txn_status < 0) {
            dprintf(INFO, "usb_read() transaction failed\n");
            goto oops;
        }

        count += req->length;
        buf += req->length;
        len -= req->length;

        /* short transfer? */
        if (req->length != xfer) break;
    }
    /*
     * Force reload of buffer from memory
     * since transaction is complete now.
     */
    arch_invalidate_cache_range((addr_t)_buf, count);
    return count;

oops:
    context->errno = -1;
    return -1;
}

static int hsusb_usb_write(struct udc_gadget *udc_gadget, void *buf, unsigned len)
{
    int r;
    uint32_t xfer;
    unsigned char *_buf = buf;
    int count = 0;
    udc_gadget_context_t *context = udc_gadget->context;
    struct udc_endpoint *in = udc_gadget->ept[0];
    struct udc_request *req = context->req;

    if (context->errno)
        goto oops;

    /* flush buffer to main memory before giving to udc */
    arch_clean_invalidate_cache_range((addr_t) buf, len);

    while (len > 0) {
        xfer = (len > MAX_USBFS_BULK_SIZE) ? MAX_USBFS_BULK_SIZE : len;
        req->buf = (unsigned char *)PA((addr_t)_buf);
        req->length = xfer;
        req->complete = req_complete;
        req->context = context;
        r = udc_request_queue(in, req);
        if (r < 0) {
            dprintf(INFO, "usb_write() queue failed\n");
            goto oops;
        }
        event_wait(&context->txn_done);
        if (context->txn_status < 0) {
            dprintf(INFO, "usb_write() transaction failed\n");
            goto oops;
        }

        count += req->length;
        _buf += req->length;
        len -= req->length;

        /* short transfer? */
        if (req->length != xfer) break;
    }

    return count;

oops:
    context->errno = -1;
    return -1;
}

static struct udc_device *usbgadget_internal_udc_device(lkapi_udc_device_t *lk_udc_device)
{
    struct udc_device *udc_device = calloc(sizeof(struct udc_device), 1);
    if (!udc_device) return NULL;

    udc_device->vendor_id = lk_udc_device->vendor_id;
    udc_device->product_id = lk_udc_device->product_id;
    udc_device->version_id = lk_udc_device->version_id;
    udc_device->manufacturer = lk_udc_device->manufacturer;
    udc_device->product = lk_udc_device->product;
    udc_device->serialno = lk_udc_device->serialno;

    if (!strcmp(target_usb_controller(), "dwc")) {
#ifdef USB30_SUPPORT
        udc_device->t_usb_if = target_usb30_init();
#else
        dprintf(CRITICAL, "USB30 needs to be enabled for this target.\n");
        ASSERT(0);
#endif
    }

    return udc_device;
}

static void udc_gadget_notify(struct udc_gadget *gadget, unsigned event)
{
    udc_gadget_context_t *context = gadget->context;

    if (event==UDC_EVENT_ONLINE)
        context->errno = 0;

    context->lk_udc_gadget->notify(context->lk_udc_gadget, event);
}

static struct udc_gadget *usbgadget_internal_udc_gadget(usb_controller_interface_t *usb_if, lkapi_udc_gadget_t *lk_udc_gadget)
{
    struct udc_gadget *udc_gadget = calloc(sizeof(struct udc_gadget), 1);
    if (!udc_gadget) return NULL;

    udc_gadget_context_t *gadget_context = calloc(sizeof(udc_gadget_context_t), 1);
    if (!gadget_context) return NULL;

    gadget_context->lk_udc_gadget = lk_udc_gadget;
    gadget_context->req = usb_if->udc_request_alloc();
    if (!gadget_context->req)
        return NULL;

    event_init(&gadget_context->txn_done, 0, EVENT_FLAG_AUTOUNSIGNAL);
    gadget_context->errno = 0;

    udc_gadget->notify = udc_gadget_notify;
    udc_gadget->context = gadget_context;
    udc_gadget->ifc_class = lk_udc_gadget->ifc_class;
    udc_gadget->ifc_subclass = lk_udc_gadget->ifc_subclass;
    udc_gadget->ifc_protocol = lk_udc_gadget->ifc_protocol;
    udc_gadget->ifc_endpoints = lk_udc_gadget->ifc_endpoints;
    udc_gadget->ifc_string = lk_udc_gadget->ifc_string;
    udc_gadget->flags = lk_udc_gadget->flags;

    struct udc_endpoint **endpoints = calloc(sizeof(struct udc_endpoint *), 2);
    if (!udc_gadget) return NULL;

    endpoints[0] = usb_if->udc_endpoint_alloc(UDC_TYPE_BULK_IN, 512);
    if (!endpoints[0])
        return NULL;
    endpoints[1] = usb_if->udc_endpoint_alloc(UDC_TYPE_BULK_OUT, 512);
    if (!endpoints[1])
        return NULL;

    udc_gadget->ept = endpoints;

    if (!strcmp(target_usb_controller(), "dwc")) {
#ifdef USB30_SUPPORT
        gadget_context->usb_read            = usb30_usb_read;
        gadget_context->usb_write           = usb30_usb_write;
#else
        dprintf(CRITICAL, "USB30 needs to be enabled for this target.\n");
        ASSERT(0);
#endif
    } else {
        gadget_context->usb_read            = hsusb_usb_read;
        gadget_context->usb_write           = hsusb_usb_write;
    }

    return udc_gadget;
}

static int usbgadget_iface_udc_init(lkapi_usbgadget_iface_t *dev, lkapi_udc_device_t *devinfo)
{
    usb_controller_interface_t *usb_if = dev->pdata;

    struct udc_device *udc_device = usbgadget_internal_udc_device(devinfo);
    if (!udc_device) return 1;

    return usb_if->udc_init(udc_device);
}

static int usbgadget_gadget_usb_read(lkapi_udc_gadget_t *gadget, void *buf, unsigned len)
{
    struct udc_gadget *udc_gadget = gadget->pdata;
    udc_gadget_context_t *context = udc_gadget->context;

    return context->usb_read(udc_gadget, buf, len);
}

static int usbgadget_gadget_usb_write(lkapi_udc_gadget_t *gadget, void *buf, unsigned len)
{
    struct udc_gadget *udc_gadget = gadget->pdata;
    udc_gadget_context_t *context = udc_gadget->context;

    return context->usb_write(udc_gadget, buf, len);
}

static int usbgadget_iface_udc_register_gadget(lkapi_usbgadget_iface_t *dev, lkapi_udc_gadget_t *lkgadget)
{
    usb_controller_interface_t *usb_if = dev->pdata;

    struct udc_gadget *gadget = usbgadget_internal_udc_gadget(usb_if, lkgadget);
    if (!gadget) return 1;

    lkgadget->pdata = gadget;
    lkgadget->usb_read = usbgadget_gadget_usb_read;
    lkgadget->usb_write = usbgadget_gadget_usb_write;

    return usb_if->udc_register_gadget(gadget);
}

static int usbgadget_iface_udc_start(lkapi_usbgadget_iface_t *dev)
{
    usb_controller_interface_t *usb_if = dev->pdata;
    return usb_if->udc_start();
}

static int usbgadget_iface_udc_stop(lkapi_usbgadget_iface_t *dev)
{
    usb_controller_interface_t *usb_if = dev->pdata;
    return usb_if->udc_stop();
}

static lkapi_usbgadget_iface_t *api_usbgadget_get_interface(void)
{
    usb_controller_interface_t *usb_if = calloc(sizeof(usb_controller_interface_t), 1);
    if (!usb_if) return NULL;

    /* target specific initialization before going into fastboot. */
    target_fastboot_init();

    if (!strcmp(target_usb_controller(), "dwc")) {
#ifdef USB30_SUPPORT
        /* initialize udc functions to use dwc controller */
        usb_if->udc_init            = usb30_udc_init;
        usb_if->udc_register_gadget = usb30_udc_register_gadget;
        usb_if->udc_start           = usb30_udc_start;
        usb_if->udc_stop            = usb30_udc_stop;

        usb_if->udc_endpoint_alloc  = usb30_udc_endpoint_alloc;
        usb_if->udc_request_alloc   = usb30_udc_request_alloc;
        usb_if->udc_request_free    = usb30_udc_request_free;
#else
        dprintf(CRITICAL, "USB30 needs to be enabled for this target.\n");
        ASSERT(0);
#endif
    } else {
        /* initialize udc functions to use the default chipidea controller */
        usb_if->udc_init            = udc_init;
        usb_if->udc_register_gadget = udc_register_gadget;
        usb_if->udc_start           = udc_start;
        usb_if->udc_stop            = udc_stop;

        usb_if->udc_endpoint_alloc  = udc_endpoint_alloc;
        usb_if->udc_request_alloc   = udc_request_alloc;
        usb_if->udc_request_free    = udc_request_free;
    }

    lkapi_usbgadget_iface_t *lk_usb_if = calloc(sizeof(lkapi_usbgadget_iface_t), 1);
    if (!lk_usb_if) return NULL;

    lk_usb_if->udc_init = usbgadget_iface_udc_init;
    lk_usb_if->udc_register_gadget = usbgadget_iface_udc_register_gadget;

    lk_usb_if->udc_start = usbgadget_iface_udc_start;
    lk_usb_if->udc_stop = usbgadget_iface_udc_stop;
    lk_usb_if->pdata = usb_if;

    return lk_usb_if;
}


/////////////////////////////////////////////////////////////////////////
//                            API TABLE                                //
/////////////////////////////////////////////////////////////////////////

lkapi_t uefiapi = {
    .platform_early_init = api_platform_early_init,
    .platform_init = api_platform_init,
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
    .lcd_needs_flush = api_lcd_needs_flush,
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
    .mmap_get_lkmem = api_mmap_get_lkmem,

    .boot_update_addrs = api_boot_update_addrs,
    .boot_exec = api_boot_exec,

    .boot_get_machine_type = api_boot_get_machine_type,
    .boot_get_cmdline_extension = api_boot_get_cmdline_extension,
    .boot_extend_atags = api_boot_extend_atags,
    .boot_extend_fdt = api_boot_extend_fdt,

    .boot_get_pmic_target = api_boot_get_pmic_target,
    .boot_get_platform_id = api_boot_get_platform_id,
    .boot_get_hardware_id = api_boot_get_hardware_id,
    .boot_get_hardware_subtype = api_boot_get_hardware_subtype,
    .boot_get_soc_version = api_boot_get_soc_version,
    .boot_get_target_id = api_boot_get_target_id,
    .boot_get_foundry_id = api_boot_get_foundry_id,
    .boot_get_hlos_subtype = api_boot_get_hlos_subtype,

    .event_init = NULL,
    .event_destroy = NULL,
    .event_wait = NULL,
    .event_signal = NULL,

    .usbgadget_get_interface = api_usbgadget_get_interface,
};

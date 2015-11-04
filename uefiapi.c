#include <err.h>
#include <debug.h>
#include <stdint.h>
#include <target.h>
#include <malloc.h>
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

__WEAK void api_platform_early_init(void) {
}
__WEAK void api_platform_init(void) {
}

__WEAK void api_common_platform_early_init(void) {
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

	exit_critical_section();

	// INIT
	api_platform_init();
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
__WEAK int api_serial_poll_char(void) {
	return dtstc();
}

__WEAK void api_serial_write_char(char c) {
	_dputc(c);
}

__WEAK int api_serial_read_char(char* cp) {
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

enum handler_return timer_tick(void *arg, time_t now) {
	timer_callback();
	return INT_RESCHEDULE;
}

__WEAK int api_timer_register_handler(lkapi_timer_callback_t callback) {
	timer_callback = callback;
	return 0;
}

__WEAK void api_timer_set_period(unsigned long long period) {
	platform_uninit_timer();

	if(period!=0) {
		ASSERT(timer_callback);
		platform_set_periodic_timer(timer_tick, NULL, (period/10000));
	}
}

__WEAK void api_timer_delay_microseconds(unsigned int microseconds) {
	mdelay(microseconds/1000);
}

__WEAK void api_timer_delay_nanoseconds(unsigned int nanoseconds) {
	udelay(nanoseconds);
}


/////////////////////////////////////////////////////////////////////////
//                            Interrupts                               //
/////////////////////////////////////////////////////////////////////////

__WEAK unsigned int api_int_get_dist_base(void) {
	return MSM_GIC_DIST_BASE;
}

__WEAK unsigned int api_int_get_redist_base(void) {
#ifdef MSM_GIC_REDIST_BASE
	return MSM_GIC_REDIST_BASE;
#else
	return 0;
#endif
}

__WEAK unsigned int api_int_get_cpu_base(void) {
	return MSM_GIC_CPU_BASE;
}

/////////////////////////////////////////////////////////////////////////
//                            BlockIO                                  //
/////////////////////////////////////////////////////////////////////////

__WEAK int api_mmc_init(lkapi_biodev_t* dev) {
	return 0;
}

static int api_mmc_read(lkapi_biodev_t* dev, unsigned long long lba, unsigned long buffersize, void* buffer) {
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

	int rc = mmc_read(BLOCK_SIZE * lba, buffer, buffersize);
	return rc != MMC_BOOT_E_SUCCESS;
}

static int api_mmc_write(lkapi_biodev_t* dev, unsigned long long lba, unsigned long buffersize, void* buffer) {
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
#if 0
	int rc = mmc_write(BLOCK_SIZE * lba, buffersize, buffer);
	dprintf(CRITICAL, "%s(%p, %llu, %lu, %p) = %d\n", __func__, dev, lba, buffersize, buffer, rc);
	return rc != MMC_BOOT_E_SUCCESS;
#else
	ASSERT(0);
	return 0;
#endif
}

#define VNOR_SIZE 0x10000
static uint64_t vnor_lba_start = 0;
static uint64_t vnor_lba_count = (VNOR_SIZE/BLOCK_SIZE);

int vnor_init(lkapi_biodev_t* dev)
{
	api_mmc_init(NULL);

	int index = INVALID_PTN;
	uint64_t ptn = 0;
	uint64_t size;

	// get partition
	index = partition_get_index(DEVICE_NVVARS_PARTITION);
	ptn = partition_get_offset(index);
	if(ptn == 0) {
		return -1;
	}

	// get size
	size = partition_get_size(index);

	// calculate vnor offset
	vnor_lba_start = (ptn + size - VNOR_SIZE)/BLOCK_SIZE;

	return 0;
}

static int vnor_read(lkapi_biodev_t* dev, unsigned long long lba, unsigned long buffersize, void* buffer) {
	int rc = mmc_read(dev->block_size * (vnor_lba_start + lba), buffer, buffersize);
	return rc != MMC_BOOT_E_SUCCESS;
}

static int vnor_write(lkapi_biodev_t* dev, unsigned long long lba, unsigned long buffersize, void* buffer) {
	int rc = mmc_write(BLOCK_SIZE * (vnor_lba_start + lba), buffersize, buffer);
	return rc != MMC_BOOT_E_SUCCESS;
}

int api_bio_list(lkapi_biodev_t* list) {
	int count = 0, dev;

	// VNOR
	dev = count++;
	if(list) {
		list[dev].type = LKAPI_BIODEV_TYPE_VNOR;
		list[dev].block_size = BLOCK_SIZE;
		list[dev].num_blocks = vnor_lba_count;
		list[dev].init = vnor_init;
		list[dev].read = vnor_read;
		list[dev].write = vnor_write;
	}

	// MMC
	dev = count++;
	if(list) {
		list[dev].type = LKAPI_BIODEV_TYPE_MMC;
		list[dev].block_size = BLOCK_SIZE;
		list[dev].num_blocks = 0;
		list[dev].init = api_mmc_init;
		list[dev].read = api_mmc_read;
		list[dev].write = api_mmc_write;
	}

	return count;
}

/////////////////////////////////////////////////////////////////////////
//                               LCD                                   //
/////////////////////////////////////////////////////////////////////////

__WEAK unsigned long long api_lcd_get_vram_address(void) {
	struct fbcon_config* fbcon = fbcon_display();
	return (uint32_t)fbcon->base;
}

__WEAK int api_lcd_init(void) {
	target_display_init("");
	return 0;
}

__WEAK unsigned int api_lcd_get_width(void) {
	struct fbcon_config* fbcon = fbcon_display();
	return fbcon->width;
}

__WEAK unsigned int api_lcd_get_height(void) {
	struct fbcon_config* fbcon = fbcon_display();
	return fbcon->height;
}

__WEAK unsigned int api_lcd_get_density(void) {
	return LCD_DENSITY;
}

__WEAK void api_lcd_flush(void) {
	fbcon_flush();
}

__WEAK void api_lcd_shutdown(void) {
	target_display_shutdown();
}

/////////////////////////////////////////////////////////////////////////
//                               RESET                                 //
/////////////////////////////////////////////////////////////////////////

#define NORMAL_MODE     0x77665501

__WEAK void api_reset_cold(void) {
	reboot_device(NORMAL_MODE);
}

__WEAK void api_reset_warm(void) {
	api_reset_cold();
}

__WEAK void api_reset_shutdown(void) {
	platform_halt();
}

/////////////////////////////////////////////////////////////////////////
//                               RTC                                   //
/////////////////////////////////////////////////////////////////////////

static unsigned int rtctime = 0;

__WEAK int api_rtc_init(void) {
	return 0;
}

__WEAK int api_rtc_gettime(unsigned int* time) {
	*time = rtctime + current_time()/1000;
	return 0;
}

__WEAK int api_rtc_settime(unsigned int time) {
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

void* platform_get_mmap(void* pdata, platform_mmap_cb_t cb);

__WEAK void* api_mmap_get_dram(void* pdata, lkapi_mmap_cb_t cb) {
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

void* api_mmap_get_mappings(void* pdata, lkapi_mmap_mappings_cb_t cb) {
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

void api_mmap_get_lk_range(unsigned long *addr, unsigned long *size) {
	*addr = MEMBASE;
	*size = MEMSIZE;
}

/////////////////////////////////////////////////////////////////////////
//                              BOOT                                   //
/////////////////////////////////////////////////////////////////////////

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

int api_boot_create_tags(const char* cmdline, unsigned int ramdisk_addr, unsigned int ramdisk_size,
			 unsigned int tags_addr, unsigned int tags_size)
{
	char* final_cmdline = (char*)update_cmdline(cmdline);
	cmdline_addall((char*)final_cmdline, false);
	free(final_cmdline);

	int len = cmdline_length();
	final_cmdline = malloc(len);
	cmdline_generate((char*)final_cmdline, len);

#if DEVICE_TREE
	int ret = load_dtb(tags_addr, tags_size);
	if(ret) return -1;

	ret = update_device_tree((void *)tags_addr,(const char *)final_cmdline, (void*)ramdisk_addr, ramdisk_size);
	return ret;
#else
	generate_atags((unsigned *)tags_addr, final_cmdline, (void*)ramdisk_addr, ramdisk_size);
#endif
	return 0;
}

unsigned int api_boot_machine_type(void) {
	return board_machtype();
}

void api_boot_update_addrs(unsigned int* kernel, unsigned int* ramdisk, unsigned int* tags) {
	/* overwrite the destination of specified for the project */
#ifdef ABOOT_IGNORE_BOOT_HEADER_ADDRS
	*kernel = ABOOT_FORCE_KERNEL_ADDR;
	*ramdisk = ABOOT_FORCE_RAMDISK_ADDR;
	*tags = ABOOT_FORCE_TAGS_ADDR;
#endif
}


/////////////////////////////////////////////////////////////////////////
//                            API TABLE                                //
/////////////////////////////////////////////////////////////////////////

lkapi_t uefiapi = {
	.platform_early_init = api_common_platform_early_init,
	.platform_get_uefi_bootmode = api_platform_get_uefi_bootmode,

	.serial_poll_char = api_serial_poll_char,
	.serial_write_char = api_serial_write_char,
	.serial_read_char = api_serial_read_char,

	.timer_register_handler = api_timer_register_handler,
	.timer_set_period = api_timer_set_period,
	.timer_delay_microseconds = api_timer_delay_microseconds,
	.timer_delay_nanoseconds = api_timer_delay_nanoseconds,

	.int_mask = NULL,
	.int_unmask = NULL,
	.int_register_handler = NULL,
	.int_get_dist_base = api_int_get_dist_base,
	.int_get_redist_base = api_int_get_redist_base,
	.int_get_cpu_base = api_int_get_cpu_base,

	.bio_list = api_bio_list,

	.lcd_get_vram_address = api_lcd_get_vram_address,
	.lcd_init = api_lcd_init,
	.lcd_get_width = api_lcd_get_width,
	.lcd_get_height = api_lcd_get_height,
	.lcd_get_density = api_lcd_get_density,
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

	.event_init = NULL,
	.event_destroy = NULL,
	.event_wait = NULL,
	.event_signal = NULL,
};

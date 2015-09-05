#include <err.h>
#include <debug.h>
#include <stdint.h>
#include <lib/heap.h>
#include <dev/keys.h>
#include <kernel/thread.h>
#include <platform/irqs.h>
#include <platform/timer.h>
#include <platform/iomap.h>
#include <platform/interrupts.h>

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

	exit_critical_section();

	// INIT
	api_platform_init();
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

__WEAK int api_bio_list(lkapi_biodev_t* list) {
	return 0;
}

/////////////////////////////////////////////////////////////////////////
//                               LCD                                   //
/////////////////////////////////////////////////////////////////////////

__WEAK unsigned long long api_lcd_get_vram_address(void) {
	return 0;
}

__WEAK int api_lcd_init(unsigned long long vramaddr) {
	return 0;
}

__WEAK unsigned int api_lcd_get_width(void) {
	return 0;
}
__WEAK unsigned int api_lcd_get_height(void) {
	return 0;
}

__WEAK unsigned int api_lcd_get_density(void) {
	return LCD_DENSITY;
}

__WEAK void api_lcd_flush(void) {
}

__WEAK void api_lcd_shutdown(void) {
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

int api_boot_create_tags(const char* cmdline, unsigned int ramdisk_addr, unsigned int ramdisk_size,
			 unsigned int tags_addr, unsigned int tags_size)
{
	char* final_cmdline = (char*)update_cmdline(cmdline);
	generate_atags((unsigned *)tags_addr, final_cmdline, (void*)ramdisk_addr, ramdisk_size);
	return 0;
}

unsigned int api_boot_machine_type(void) {
	return board_machtype();
}


/////////////////////////////////////////////////////////////////////////
//                            API TABLE                                //
/////////////////////////////////////////////////////////////////////////

lkapi_t uefiapi = {
	.platform_early_init = api_common_platform_early_init,

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
};

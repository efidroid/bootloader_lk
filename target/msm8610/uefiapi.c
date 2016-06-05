#include <err.h>
#include <debug.h>
#include <stdint.h>
#include <mmc.h>
#include <spmi.h>
#include <board.h>
#include <target.h>
#include <pm8x41.h>
#include <qtimer.h>
#include <dev/keys.h>
#include <sdhci_msm.h>
#include <target/display.h>
#include <platform/iomap.h>
#include <platform/clock.h>
#include <platform/timer.h>
#include <platform/gpio.h>
#include <partition_parser.h>
#include <rpm-smd.h>
#include <regulator.h>

#include <uefiapi.h>

#define PMIC_ARB_CHANNEL_NUM    0
#define PMIC_ARB_OWNER_ID       0

/////////////////////////////////////////////////////////////////////////
//                                KEYS                                 //
/////////////////////////////////////////////////////////////////////////

uint32_t target_volume_up(void);
uint32_t target_volume_down(void);

static int target_power_key(void)
{
	return pm8x41_get_pwrkey_is_pressed();
}

static int event_source_poll(key_event_source_t* source) {
	uint16_t value = target_power_key();
	if(keys_set_report_key(source, 0, &value)){
		keys_post_event(13, value);
	}

	value = target_volume_up();
	if(keys_set_report_key(source, 1, &value)){
		keys_post_event(KEY_VOLUMEUP, value);
	}

	value = target_volume_down();
	if(keys_set_report_key(source, 2, &value)){
		keys_post_event(KEY_VOLUMEDOWN, value);
	}

	return NO_ERROR;
}

static key_event_source_t event_source = {
	.poll = event_source_poll
};

/////////////////////////////////////////////////////////////////////////
//                            PLATFORM                                 //
/////////////////////////////////////////////////////////////////////////

extern struct mmc_device *dev;

static void rpm_smd_init_once(void) {
	static int initialized = 0;
	if(!initialized) {
		rpm_smd_init();
		initialized = 1;
	}
}

void api_platform_early_init(void) {
	// from platform_early_init, but without GIC
	board_init();
	platform_clock_init();
	qtimer_init();

	// UART
	target_early_init();
}

void api_platform_init(void) {
	// from target_init
	// Initialize PMIC driver
	spmi_init(PMIC_ARB_CHANNEL_NUM, PMIC_ARB_OWNER_ID);

	keys_init();
	keys_add_source(&event_source);
	event_source.keymap[0].enable_longpress = true;
}

void api_platform_uninit(void) {
	// from target_uninit
	mmc_put_card_to_sleep(dev);

	// Disable HC mode before jumping to kernel
	sdhci_mode_disable(&dev->host);
}

/////////////////////////////////////////////////////////////////////////
//                            BlockIO                                  //
/////////////////////////////////////////////////////////////////////////

void target_sdc_init(void);

int api_mmc_init(void) {
	// we can't do this in platform_init because this needs interrupts
	rpm_smd_init_once();

	target_sdc_init();
	return 0;
}

void* api_mmap_get_platform_mappings(void* pdata, lkapi_mmap_mappings_cb_t cb) {
	pdata = cb(pdata, MSM_IOMAP_BASE, MSM_IOMAP_BASE, (MSM_IOMAP_END - MSM_IOMAP_BASE), LKAPI_MEMORY_DEVICE);
	pdata = cb(pdata, SYSTEM_IMEM_BASE, SYSTEM_IMEM_BASE, 1*1024, LKAPI_MEMORY_DEVICE);

	return pdata;
}

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
#include <dev/fbcon.h>
#include <mipi_dsi.h>
#include <target/display.h>
#include <platform/iomap.h>
#include <platform/clock.h>
#include <platform/gpio.h>
#include <partition_parser.h>

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

/////////////////////////////////////////////////////////////////////////
//                            BlockIO                                  //
/////////////////////////////////////////////////////////////////////////

void target_sdc_init(void);

int api_mmc_init(void) {
	target_sdc_init();
	return 0;
}

void* api_mmap_get_platform_mappings(void* pdata, lkapi_mmap_mappings_cb_t cb) {
	pdata = cb(pdata, MSM_IOMAP_BASE, MSM_IOMAP_BASE, (MSM_IOMAP_END - MSM_IOMAP_BASE), LKAPI_MEMORY_DEVICE);

	return pdata;
}

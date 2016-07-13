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
#include <sdhci_msm.h>
#include <target/display.h>
#include <platform/iomap.h>
#include <platform/clock.h>
#include <platform/gpio.h>
#include <partition_parser.h>
#include <rpm-smd.h>

#include <uefiapi.h>

#define PMIC_ARB_CHANNEL_NUM    0
#define PMIC_ARB_OWNER_ID       0

static uint32_t pmic_ver;

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

void api_platform_early_init(void) {
	// from platform_early_init, but without GIC
	board_init();
	platform_clock_init();
	qtimer_init();
	scm_init();

	// UART
	target_early_init();
}

void api_platform_init(void) {
	// from target_init
	// Initialize PMIC driver
	spmi_init(PMIC_ARB_CHANNEL_NUM, PMIC_ARB_OWNER_ID);

	/* Save PM8941 version info. */
	pmic_ver = pm8x41_get_pmic_rev();

	keys_init();
	keys_add_source(&event_source);
	event_source.keymap[0].enable_longpress = true;
}

void api_platform_uninit(void) {
	// from target_uninit

	/*Keep the MMC card in sleep state before entering into kernel
	so that kernel driver will do the initialization of the card again*/
	mmc_put_card_to_sleep(dev);
	sdhci_mode_disable(&dev->host);

#if SMD_SUPPORT
	rpm_smd_uninit();
#endif
}


/////////////////////////////////////////////////////////////////////////
//                            BlockIO                                  //
/////////////////////////////////////////////////////////////////////////

void target_sdc_init(void);

#if SMD_SUPPORT
static void rpm_smd_init_once(void) {
	static int initialized = 0;
	if(!initialized) {
		rpm_smd_init();
		initialized = 1;
	}
}
#endif

int api_mmc_init(void) {
#if SMD_SUPPORT
	// we can't do this in platform_init because this needs interrupts
	rpm_smd_init_once();
#endif

	target_sdc_init();

	return 0;
}

void* api_mmap_get_platform_mappings(void* pdata, lkapi_mmap_mappings_cb_t cb) {
	pdata = cb(pdata, MSM_IOMAP_BASE, MSM_IOMAP_BASE, (MSM_IOMAP_END - MSM_IOMAP_BASE), LKAPI_MEMORY_DEVICE);
	pdata = cb(pdata, APPS_SS_BASE, APPS_SS_BASE, (APPS_SS_END - APPS_SS_BASE), LKAPI_MEMORY_DEVICE);
	pdata = cb(pdata, MSM_SHARED_IMEM_BASE, MSM_SHARED_IMEM_BASE, 1*1024, LKAPI_MEMORY_DEVICE);
	pdata = cb(pdata, RPMB_SND_RCV_BUF, RPMB_SND_RCV_BUF, RPMB_SND_RCV_BUF_SZ*1024, LKAPI_MEMORY_DEVICE);

	return pdata;
}

void *api_mmap_get_platform_lkmem(void *pdata, lkapi_mmap_lkmem_cb_t cb) {
    pdata = cb(pdata, RPMB_SND_RCV_BUF, RPMB_SND_RCV_BUF_SZ*1024);
    return pdata;
}

#include <err.h>
#include <board.h>
#include <stdint.h>
#include <pm8x41.h>
#include <dev/keys.h>
#include <platform/iomap.h>

#include <uefiapi.h>

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

void uefiapi_platform_init_post(void) {
	keys_add_source(&event_source);
	event_source.keymap[0].enable_longpress = true;
}

void* api_mmap_get_platform_mappings(void* pdata, lkapi_mmap_mappings_cb_t cb) {
	pdata = cb(pdata, MSM_IOMAP_BASE, MSM_IOMAP_BASE, (MSM_IOMAP_END - MSM_IOMAP_BASE), LKAPI_MEMORY_DEVICE);
	pdata = cb(pdata, SYSTEM_IMEM_BASE, SYSTEM_IMEM_BASE, 1*MB, LKAPI_MEMORY_DEVICE);
	pdata = cb(pdata, MSM_SHARED_BASE, MSM_SHARED_BASE, 2*MB, LKAPI_MEMORY_WRITE_THROUGH);

	return pdata;
}

void *api_mmap_get_platform_lkmem(void *pdata, lkapi_mmap_lkmem_cb_t cb) {
	pdata = cb(pdata, MSM_SHARED_BASE, 2*MB);

	return pdata;
}

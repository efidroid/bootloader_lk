#include <err.h>
#include <board.h>
#include <stdint.h>
#include <dev/keys.h>
#include <dev/pm8921.h>
#include <platform/iomap.h>

#include <uefiapi.h>

/////////////////////////////////////////////////////////////////////////
//                                KEYS                                 //
/////////////////////////////////////////////////////////////////////////

static int target_power_key(void)
{
    uint8_t ret = 0;

    pm8921_pwrkey_status(&ret);
    return ret;
}

static int event_source_poll(key_event_source_t *source)
{
    uint16_t value = target_power_key();
    if (keys_set_report_key(source, 0, &value)) {
        keys_post_event(13, value);
    }

    return NO_ERROR;
}

static key_event_source_t event_source = {
    .poll = event_source_poll
};

/////////////////////////////////////////////////////////////////////////
//                            PLATFORM                                 //
/////////////////////////////////////////////////////////////////////////

void uefiapi_platform_init_post(void)
{
    keys_add_source(&event_source);
    event_source.keymap[0].enable_longpress = true;
}

void *api_mmap_get_platform_mappings(void *pdata, lkapi_mmap_mappings_cb_t cb)
{
    pdata = cb(pdata, MSM_IOMAP_BASE, MSM_IOMAP_BASE, (MSM_IOMAP_END - MSM_IOMAP_BASE), LKAPI_MEMORY_DEVICE);
    pdata = cb(pdata, MSM_IMEM_BASE, MSM_IMEM_BASE, 1*MB, LKAPI_MEMORY_DEVICE);

    return pdata;
}

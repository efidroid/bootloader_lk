#include <err.h>
#include <board.h>
#include <stdint.h>
#include <dev/newkeys.h>
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

static int event_source_poll(newkey_event_source_t *source)
{
    newkeys_set_report_key(source, 13, target_power_key());

    return NO_ERROR;
}

static newkey_event_source_t event_source = {
    .poll = event_source_poll
};

/////////////////////////////////////////////////////////////////////////
//                            PLATFORM                                 //
/////////////////////////////////////////////////////////////////////////

void uefiapi_platform_init_post(void)
{
    newkeys_add_source(&event_source);
}

void* uefiapi_mmap_add_platform_mappings(void *pdata, lkapi_mmap_add_cb_t cb)
{
    // iomap
    pdata = cb(pdata, MSM_IOMAP_BASE, (MSM_IOMAP_END - MSM_IOMAP_BASE), LKAPI_MMAP_RANGEFLAG_RESERVED,
                      LKAPI_MEMORYATTR_DEVICE, 0, LKAPI_MMAP_RANGEFLAG_UNUSED|LKAPI_MMAP_RANGEFLAG_DRAM);

    // imem
    pdata = cb(pdata, MSM_IMEM_BASE, 1*MB, LKAPI_MMAP_RANGEFLAG_RESERVED,
                      LKAPI_MEMORYATTR_DEVICE, 0, LKAPI_MMAP_RANGEFLAG_UNUSED|LKAPI_MMAP_RANGEFLAG_DRAM);

    return pdata;
}

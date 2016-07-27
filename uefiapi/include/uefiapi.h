#ifndef UEFIAPI_H
#define UEFIAPI_H

#include <LittleKernelApi.h>
#include <kernel/thread.h>
#include <assert.h>

extern lkapi_t uefiapi;
extern int critical_enter_status;

static inline __ALWAYS_INLINE void enter_critical_section(void)
{
    int enabled = arch_ints_enabled();
    arch_disable_ints();

    // UEFI wants them enabled, but we thought they were disabled
    if(enabled && critical_section_count>=1) {
        critical_section_count = 0;
    }

    critical_section_count++;
    critical_enter_status = enabled;
}

static inline __ALWAYS_INLINE void exit_critical_section(void)
{
    ASSERT(arch_ints_enabled()==0);
    ASSERT(critical_section_count>0);

    critical_section_count--;
    if (critical_section_count == 0 && critical_enter_status==1)
        arch_enable_ints();
}

#endif

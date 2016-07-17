#ifndef UEFIAPI_H
#define UEFIAPI_H

#include <LittleKernelApi.h>

extern lkapi_t uefiapi;

uint32_t uefi_entry_check(void);
void uefi_exit_check(uint32_t prev);
void* uefiapi_make_fn_wrapper(void* fn);

#endif

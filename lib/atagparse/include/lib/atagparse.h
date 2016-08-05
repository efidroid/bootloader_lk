#ifndef ATAGPARSE_H
#define ATAGPARSE_H

#include <platform.h>

typedef void* (*lkargs_mmap_cb_t)(void* pdata, uint64_t addr, uint64_t size, bool reserved);

typedef enum {
    LKARGS_UEFI_BM_NORMAL = 0,
    LKARGS_UEFI_BM_RECOVERY,
} lkargs_uefi_bootmode;

const char* lkargs_get_command_line(void);
struct list_node* lkargs_get_command_line_list(void);
const char* lkargs_get_panel_name(const char* key);
lkargs_uefi_bootmode lkargs_get_uefi_bootmode(void);
void* lkargs_get_tags_backup(void);
size_t lkargs_get_tags_backup_size(void);
void atag_parse(void);
int qciditem_get(const char* name, uint32_t* datap);
uint32_t qciditem_get_zero(const char* name);

bool lkargs_has_meminfo(void);
unsigned *lkargs_gen_meminfo_atags(unsigned *ptr);
uint32_t lkargs_gen_meminfo_fdt(void *fdt, uint32_t memory_node_offset);
void* lkargs_get_mmap_callback(void* pdata, lkargs_mmap_cb_t cb);
int lkargs_insert_chosen(void* fdt);
void* lkargs_atag_insert_unknown(void* tags);

#endif // ATAGPARSE_H

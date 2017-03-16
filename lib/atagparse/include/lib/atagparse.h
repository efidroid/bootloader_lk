#ifndef ATAGPARSE_H
#define ATAGPARSE_H

#include <platform.h>

typedef void *(*lkargs_mmap_cb_t)(void *pdata, uint64_t addr, uint64_t size);

#define LKARGS_BOOTMODE_NORMAL 0
#define LKARGS_BOOTMODE_RECOVERY 1
#define LKARGS_BOOTMODE_CHARGER 2

// init
void atag_parse(void);

// raw tags
const void *lkargs_get_tags_backup(void);
size_t lkargs_get_tags_backup_size(void);

// parsed data
const char *lkargs_get_command_line(int is_recovery);
struct list_node *lkargs_get_command_line_list(void);
char *lkargs_get_panel_name(const char *key);
unsigned int lkargs_get_uefi_bootmode(void);
const char *lkargs_get_uefi_bootpart(void);

// qcid
int qciditem_get(const char *name, uint32_t *datap);
uint32_t qciditem_get_zero(const char *name);

// atag information
void *lkargs_get_mmap_callback(void *pdata, lkargs_mmap_cb_t cb);
int lkargs_insert_chosen(void *fdt);
void *lkargs_atag_insert_unknown(void *tags);

#endif // ATAGPARSE_H

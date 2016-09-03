#ifndef ATAGPARSE_H
#define ATAGPARSE_H

#include <platform.h>

typedef void *(*lkargs_mmap_cb_t)(void *pdata, uint64_t addr, uint64_t size, bool reserved);

// init
void atag_parse(void);

// raw tags
void *lkargs_get_tags_backup(void);
size_t lkargs_get_tags_backup_size(void);

// parsed data
const char *lkargs_get_command_line(void);
struct list_node *lkargs_get_command_line_list(void);
char *lkargs_get_panel_name(const char *key);
const char *lkargs_get_uefi_bootpart(void);

// qcid
int qciditem_get(const char *name, uint32_t *datap);
uint32_t qciditem_get_zero(const char *name);

// atag information
void *lkargs_get_mmap_callback(void *pdata, lkargs_mmap_cb_t cb);
int lkargs_insert_chosen(void *fdt);
void *lkargs_atag_insert_unknown(void *tags);

#endif // ATAGPARSE_H

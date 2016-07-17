#include <debug.h>
#include <string.h>
#include <malloc.h>
#include <board.h>
#include <lib/atagparse.h>
#include <lib/cmdline.h>
#include "atags.h"

#if DEVICE_TREE
#include <libfdt.h>
#include <dev_tree.h>
#endif

typedef struct {
    uint64_t start;
    uint64_t size;
} meminfo_t;

typedef struct {
    uint32_t version;
    uint32_t chipset;
    uint32_t platform;
    uint32_t subtype;
    uint32_t revNum;
    uint32_t pmic_model[4];
} efidroid_fdtinfo_t;

// lk boot args
extern uint32_t lk_boot_args[4];

// atags backup
static void*  tags_copy = NULL;
static size_t tags_size = 0;

// parsed data: common
static uint32_t machinetype = 0;
static char* command_line = NULL;
static struct list_node cmdline_list;
static lkargs_uefi_bootmode uefi_bootmode = LKARGS_UEFI_BM_NORMAL;
static meminfo_t* meminfo = NULL;
static size_t meminfo_count = 0;
// parsed data: fdt
#if DEVICE_TREE
static uint32_t platform_id = 0;
static uint32_t variant_id = 0;
static uint32_t hw_subtype = 0;
static uint32_t soc_rev = 0;
static bool has_board_info = false;
static bool board_info_version = 0;
#endif

uint32_t lkargs_get_machinetype(void) {
	return machinetype;
}

const char* lkargs_get_command_line(void) {
	return command_line;
}

struct list_node* lkargs_get_command_line_list(void) {
	return &cmdline_list;
}

const char* lkargs_get_panel_name(const char* key) {
    const char* value = cmdline_get(&cmdline_list, key);
    if(!value) return NULL;

    const char* name;
    const char* pch=strrchr(value,':');
    if(!pch) name = pch;
    else name = pch+1;

    return name;
}

#if DEVICE_TREE
uint32_t lkargs_get_platform_id(void) {
	return platform_id;
}

uint32_t lkargs_get_variant_id(void) {
	return variant_id;
}

uint32_t lkargs_get_hw_subtype(void) {
    return hw_subtype;
}

uint32_t lkargs_get_soc_rev(void) {
	return soc_rev;
}
#endif

lkargs_uefi_bootmode lkargs_get_uefi_bootmode(void) {
	return uefi_bootmode;
}

void* lkargs_get_tags_backup(void) {
    return tags_copy;
}
size_t lkargs_get_tags_backup_size(void) {
    return tags_size;
}

// backup functions
static int save_atags(const struct tag *tags)
{
	const struct tag *t = tags;
	for (; t->hdr.size; t = tag_next(t));
	t++;
	tags_size = ((uint32_t)t)-((uint32_t)tags);

	tags_copy = malloc(tags_size);
	if(!tags_copy) {
		dprintf(CRITICAL, "Error saving atags!\n");
		return -1;
	}

	memcpy(tags_copy, tags, tags_size);
	return 0;
}

int atags_check_header(void* tags) {
    struct tag *atags = (struct tag *)tags;
    return atags->hdr.tag!=ATAG_CORE;
}

#if DEVICE_TREE
static int save_fdt(void* fdt)
{
	tags_size = fdt_totalsize(fdt);
	tags_copy = malloc(tags_size);
	if(!tags_copy) {
		dprintf(CRITICAL, "Error saving fdt!\n");
		return -1;
	}

	memcpy(tags_copy, fdt, tags_size);
	return 0;
}
#endif

// parse ATAGS
static int parse_atag_core(const struct tag *tag)
{
	return 0;
}

static void add_meminfo(uint64_t start, uint64_t size) {
	meminfo = realloc(meminfo, (++meminfo_count)*sizeof(*meminfo));
	ASSERT(meminfo);

	meminfo[meminfo_count-1].start = start;
	meminfo[meminfo_count-1].size = size;
}

static int parse_atag_mem32(const struct tag *tag)
{
	dprintf(INFO, "0x%08x-0x%08x\n", tag->u.mem.start, tag->u.mem.start+tag->u.mem.size);

	add_meminfo((uint64_t)tag->u.mem.start, (uint64_t)tag->u.mem.size);

	return 0;
}

static int parse_atag_cmdline(const struct tag *tag)
{
	command_line = malloc(COMMAND_LINE_SIZE+1);
	if(!tags_copy) {
		dprintf(CRITICAL, "Error allocating cmdline memory!\n");
		return -1;
	}

	strlcpy(command_line, tag->u.cmdline.cmdline, COMMAND_LINE_SIZE);

	return 0;
}

static struct tagtable tagtable[] = {
	{ATAG_CORE, parse_atag_core},
	{ATAG_MEM, parse_atag_mem32},
	{ATAG_CMDLINE, parse_atag_cmdline},
};

static int parse_atag(const struct tag *tag)
{
	struct tagtable *t;
	struct tagtable *t_end = tagtable+ARRAY_SIZE(tagtable);

	for (t = tagtable; t < t_end; t++)
		if (tag->hdr.tag == t->tag) {
			t->parse(tag);
			break;
		}

	return t < t_end;
}

static struct tagtable* get_tagtable_entry(const struct tag *tag)
{
	struct tagtable *t;
	struct tagtable *t_end = tagtable+ARRAY_SIZE(tagtable);

	for (t = tagtable; t < t_end; t++) {
		if (tag->hdr.tag == t->tag) {
			return t;
		}
	}

	return NULL;
}

static void parse_atags(const struct tag *t)
{
	for (; t->hdr.size; t = tag_next(t))
		if (!parse_atag(t))
			dprintf(INFO, "Ignoring unrecognised tag 0x%08x\n",
				t->hdr.tag);
}

void* lkargs_atag_insert_unknown(void* tags) {
	struct tag *tag = (struct tag *)tags;
	const struct tag *t;

	if(!tags_copy || atags_check_header(tags_copy))
		return tag;

	for (t=tags_copy; t->hdr.size; t=tag_next(t)) {
		if (!get_tagtable_entry(t)) {
			tag = tag_next(tag);
			memcpy(tag, t, t->hdr.size*sizeof(uint32_t));
		}
	}

	return tag;
}

static unsigned *target_mem_atag_create(unsigned *ptr, uint32_t size, uint32_t addr)
{
	*ptr++ = 4;
	*ptr++ = ATAG_MEM;
	*ptr++ = size;
	*ptr++ = addr;

	return ptr;
}

unsigned *lkargs_gen_meminfo_atags(unsigned *ptr)
{
	uint8_t i = 0;

	for (i = 0; i < meminfo_count; i++)
	{
		ptr = target_mem_atag_create(ptr,
					(uint32_t)meminfo[i].size,
					(uint32_t)meminfo[i].start);
	}

	return ptr;
}

void* lkargs_get_mmap_callback(void* pdata, platform_mmap_cb_t cb) {
	uint32_t i;

	ASSERT(meminfo);

	for(i=0; i<meminfo_count; i++) {
		pdata = cb(pdata, meminfo[i].start, meminfo[i].size, false);
	}

	return pdata;
}

bool lkargs_has_meminfo(void) {
	return !!meminfo;
}

// parse FDT
#if DEVICE_TREE
struct dt_entry_v1
{
	uint32_t platform_id;
	uint32_t variant_id;
	uint32_t soc_rev;
	uint32_t offset;
	uint32_t size;
};

static int fdt_get_cell_sizes(void* fdt, uint32_t* out_addr_cell_size, uint32_t* out_size_cell_size) {
	int rc;
	int len;
	const uint32_t *valp;
	uint32_t offset;
	uint32_t addr_cell_size = 0;
	uint32_t size_cell_size = 0;

	// get root node offset
	rc = fdt_path_offset(fdt, "/");
	if (rc<0) return -1;
	offset = rc;

	// find the #address-cells size
	valp = fdt_getprop(fdt, offset, "#address-cells", &len);
	if (len<=0 || !valp) {
		if (len == -FDT_ERR_NOTFOUND)
			addr_cell_size = 2;
		else return -1;
	}
	else {
		addr_cell_size = fdt32_to_cpu(*valp);
	}

	// find the #size-cells size
	valp = fdt_getprop(fdt, offset, "#size-cells", &len);
	if (len<=0 || !valp) {
		if (len == -FDT_ERR_NOTFOUND)
			size_cell_size = 2;
		else return -1;
	}
	else {
		size_cell_size = fdt32_to_cpu(*valp);
	}

	*out_addr_cell_size = addr_cell_size;
	*out_size_cell_size = size_cell_size;

	return 0;
}

static int parse_fdt(void* fdt)
{
	int ret = 0;
	uint32_t offset;
	int len;

	// get memory node
	ret = fdt_path_offset(fdt, "/memory");
	if (ret < 0)
	{
		dprintf(CRITICAL, "Could not find memory node.\n");
	}
	else {
		offset = ret;

		uint32_t addr_cell_size = 1;
		uint32_t size_cell_size = 1;
		fdt_get_cell_sizes(fdt, &addr_cell_size, &size_cell_size);
		if(addr_cell_size>2 || size_cell_size>2) {
			dprintf(CRITICAL, "unsupported cell sizes\n");
			goto next;
		}

		// get reg node
		const uint32_t* reg = fdt_getprop(fdt, offset, "reg", &len);
		if(!reg) {
			dprintf(CRITICAL, "Could not find reg node.\n");
		}
		else {
			uint32_t regpos = 0;
			while(regpos<len/sizeof(uint32_t)) {
				uint64_t base = fdt32_to_cpu(reg[regpos++]);
				if(addr_cell_size==2) {
					base = base<<32;
					base = fdt32_to_cpu(reg[regpos++]);
				}

				uint64_t size = fdt32_to_cpu(reg[regpos++]);
				if(size_cell_size==2) {
					size = size<<32;
					size = fdt32_to_cpu(reg[regpos++]);
				}

				dprintf(INFO, "0x%016llx-0x%016llx\n", base, base+size);
				if(base>0xffffffff || size>0xffffffff) {
					dprintf(CRITICAL, "address range exceeds 32bit address space\n");
				}
				else {
					add_meminfo(base, size);
				}
			}
		}
	}

next:
	// get chosen node
	ret = fdt_path_offset(fdt, "/chosen");
	if (ret < 0)
	{
		dprintf(CRITICAL, "Could not find chosen node.\n");
		return ret;
	}
	else {
		offset = ret;

		// get bootargs
		const char* bootargs = (const char *)fdt_getprop(fdt, offset, "bootargs", &len);
		if (bootargs && len>0)
		{
			command_line = malloc(len);
			memcpy(command_line, bootargs, len);
		}
	}

    // get root node
	ret = fdt_path_offset(fdt, "/");
	if (ret < 0)
	{
		dprintf(CRITICAL, "Could not find root node.\n");
	}
    offset = ret;

    // get socinfo property
    int len_socinfo;
    const struct fdt_property* prop_socinfo = fdt_get_property(fdt, offset, "efidroid-soc-info", &len_socinfo);
    if(!prop_socinfo) {
		dprintf(CRITICAL, "Could not find efidroid-soc-info.\n");
	}
	else {
        const efidroid_fdtinfo_t* socinfo = (const efidroid_fdtinfo_t*)prop_socinfo->data;

		platform_id = fdt32_to_cpu(socinfo->chipset);
		variant_id = fdt32_to_cpu(socinfo->platform);
		hw_subtype = fdt32_to_cpu(socinfo->subtype);
		soc_rev = fdt32_to_cpu(socinfo->revNum);
        board_info_version = fdt32_to_cpu(socinfo->version);
		has_board_info = true;

		dprintf(INFO, "platform_id=%u variant_id=%u hw_subtype=%u soc_rev=%X version=%u\n", platform_id, variant_id, hw_subtype, soc_rev, board_info_version);
	}

	return 0;
}

bool lkargs_has_board_info(void) {
	return has_board_info;
}

bool lkargs_board_info_version(void) {
	return board_info_version;
}

uint32_t lkargs_gen_meminfo_fdt(void *fdt, uint32_t memory_node_offset)
{
	unsigned int i;
	int ret = 0;

	for (i = 0 ; i < meminfo_count; i++)
	{
		ret = dev_tree_add_mem_info(fdt,
									memory_node_offset,
									meminfo[i].start,
									meminfo[i].size);

		if (ret)
		{
			dprintf(CRITICAL, "Failed to add memory info\n");
			goto out;
		}
	}

out:
    return ret;
}
#endif

#if DEVICE_TREE
static int lkargs_fdt_insert_properties(void *fdtdst, int offsetdst, const void* fdtsrc, int offsetsrc)
{
	int len;
	int offset;
	int ret;

	offset = offsetsrc;
	for (offset = fdt_first_property_offset(fdtsrc, offset);
			(offset >= 0);
			(offset = fdt_next_property_offset(fdtsrc, offset)))
	{
		const struct fdt_property *prop;

		if (!(prop = fdt_get_property_by_offset(fdtsrc, offset, &len))) {
			offset = -FDT_ERR_INTERNAL;
			break;
		}

		const char* name = fdt_string(fdtsrc, fdt32_to_cpu(prop->nameoff));
		dprintf(SPEW, "PROP: %s\n", name);

		// blacklist our nodes
		if(!strcmp(name, "bootargs"))
			continue;
		if(!strcmp(name, "linux,initrd-start"))
			continue;
		if(!strcmp(name, "linux,initrd-end"))
			continue;

		// set prop
		ret = fdt_setprop(fdtdst, offsetdst, name, prop->data, len);
		if(ret) {
			dprintf(CRITICAL, "can't set prop: %s\n", fdt_strerror(ret));
			continue;
		}
	}

	return 0;
}

static int lkargs_fdt_insert_nodes(void *fdt, int target_offset) {
	int depth;
	int ret = 0;
	uint32_t source_offset_chosen;
	uint32_t target_offset_node;
	int offset;

	// get chosen node in source
	ret = fdt_path_offset(tags_copy, "/chosen");
	if (ret < 0) {
		dprintf(CRITICAL, "Could not find chosen node.\n");
		return ret;
	}
	source_offset_chosen = ret;

	offset = source_offset_chosen;
	for (depth = 0; (offset >= 0) && (depth >= 0);
			offset = fdt_next_node(tags_copy, offset, &depth))
	{
		const char *name = fdt_get_name(tags_copy, offset, NULL);
		dprintf(SPEW, "NODE: %s\n", name);

		// get/create node
		if(!strcmp(name, "chosen")) {
			ret = target_offset;
		}
		else {
			ret = fdt_subnode_offset(fdt, target_offset, name);
			if (ret < 0) {
				dprintf(SPEW, "creating node %s.\n", name);
				ret = fdt_add_subnode(fdt, target_offset, name);
				if (ret < 0) {
					dprintf(CRITICAL, "can't create node %s: %s\n", name, fdt_strerror(ret));
					continue;
				}
			}
		}
		target_offset_node = ret;

		// insert all properties
		lkargs_fdt_insert_properties(fdt, target_offset_node, tags_copy, offset);
	}

	return 0;
}

int lkargs_insert_chosen(void* fdt) {
	int ret = 0;
	uint32_t target_offset_chosen;

	if(!tags_copy || fdt_check_header(tags_copy))
		return 0;

	// get chosen node
	ret = fdt_path_offset(fdt, "/chosen");
	if (ret < 0) {
		dprintf(CRITICAL, "Could not find chosen node.\n");
		return ret;
	}
	target_offset_chosen = ret;

	// insert all nodes
	return lkargs_fdt_insert_nodes(fdt, target_offset_chosen);
	}
#endif

void atag_parse(void) {
	dprintf(INFO, "bootargs: 0x%x 0x%x 0x%x 0x%x\n",
		lk_boot_args[0],
		lk_boot_args[1],
		lk_boot_args[2],
		lk_boot_args[3]
	);

	// init cmdline lib
	cmdline_init(&cmdline_list);

	// machine type
	machinetype = lk_boot_args[1];
	dprintf(INFO, "machinetype: %u\n", machinetype);

	void* tags = (void*)lk_boot_args[2];

#if DEVICE_TREE
	// fdt
	if(!fdt_check_header(tags)) {
		save_fdt(tags);
		parse_fdt(tags);
	} else
#endif

	// atags
	if (!atags_check_header(tags)) {
		save_atags(tags);
		parse_atags(tags);
	}

	// unknown
	else {
		dprintf(CRITICAL, "Invalid atags!\n");
		return;
	}

	dprintf(INFO, "cmdline=[%s]\n", command_line);
#ifndef PLATFORM_MSM7X27A
	dprintf(INFO, "[orig] platform_id=%u variant_id=%u hw_subtype=%u soc_rev=%X\n", board_platform_id(), board_hardware_id(), board_hardware_subtype(), board_soc_version());
#endif

	// add to global cmdline lib
	cmdline_addall(&cmdline_list, command_line, true);

	// get bootmode
	const char* bootmode = cmdline_get(&cmdline_list, "uefi.bootmode");
	if(bootmode) {
		dprintf(INFO, "uefi.bootmode = [%s]\n", bootmode);

		if(!strcmp(bootmode, "recovery"))
			uefi_bootmode = LKARGS_UEFI_BM_RECOVERY;

		cmdline_remove(&cmdline_list, "uefi.bootmode");
	}
}

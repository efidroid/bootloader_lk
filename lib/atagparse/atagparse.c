#include <debug.h>
#include <string.h>
#include <malloc.h>
#include <board.h>
#include <atagparse.h>
#include <cmdline.h>
#include "atags.h"

#if DEVICE_TREE
#include <libfdt.h>
#include <dev_tree.h>
#endif

typedef struct {
    uint32_t start;
    uint32_t size;
} meminfo_t;

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
static uint32_t soc_rev = 0;
static bool has_board_info = false;
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

static void add_meminfo(uint32_t start, uint32_t size) {
	meminfo = realloc(meminfo, (++meminfo_count)*sizeof(*meminfo));
	ASSERT(meminfo);

	meminfo[meminfo_count-1].start = start;
	meminfo[meminfo_count-1].size = size;
}

static int parse_atag_mem32(const struct tag *tag)
{
	dprintf(INFO, "0x%08x-0x%08x\n", tag->u.mem.start, tag->u.mem.start+tag->u.mem.size);

	add_meminfo(tag->u.mem.start, tag->u.mem.size);

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
					meminfo[i].size,
					meminfo[i].start);
	}

	return ptr;
}

void* lkargs_get_mmap_callback(void* pdata, platform_mmap_cb_t cb) {
	uint32_t i;

	ASSERT(meminfo);

	for(i=0; i<meminfo_count; i++) {
		pdata = cb(pdata, (paddr_t) meminfo[i].start, (size_t)meminfo[i].size, false);
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

static struct dt_entry* get_dt_entry(void *dtb, uint32_t dtb_size)
{
	int root_offset;
	const void *prop = NULL;
	const char *plat_prop = NULL;
	const char *board_prop = NULL;
	const char *pmic_prop = NULL;
	char *model = NULL;
	struct dt_entry *cur_dt_entry = NULL;
	struct dt_entry *dt_entry_array = NULL;
	struct board_id *board_data = NULL;
	struct plat_id *platform_data = NULL;
	struct pmic_id *pmic_data = NULL;
	int len;
	int len_board_id;
	int len_plat_id;
	int min_plat_id_len = 0;
	int len_pmic_id;
	uint32_t dtb_ver;
	uint32_t num_entries = 0;
	uint32_t i, j, k, n;
	uint32_t msm_data_count;
	uint32_t board_data_count;
	uint32_t pmic_data_count;

	root_offset = fdt_path_offset(dtb, "/");
	if (root_offset < 0)
		return NULL;

	prop = fdt_getprop(dtb, root_offset, "model", &len);
	if (prop && len > 0) {
		model = (char *) malloc(sizeof(char) * len);
		ASSERT(model);
		strlcpy(model, prop, len);
	} else {
		dprintf(INFO, "model does not exist in device tree\n");
	}
	/* Find the pmic-id prop from DTB , if pmic-id is present then
	* the DTB is version 3, otherwise find the board-id prop from DTB ,
	* if board-id is present then the DTB is version 2 */
	pmic_prop = (const char *)fdt_getprop(dtb, root_offset, "qcom,pmic-id", &len_pmic_id);
	board_prop = (const char *)fdt_getprop(dtb, root_offset, "qcom,board-id", &len_board_id);
	if (pmic_prop && (len_pmic_id > 0) && board_prop && (len_board_id > 0)) {
		if ((len_pmic_id % PMIC_ID_SIZE) || (len_board_id % BOARD_ID_SIZE))
		{
			dprintf(CRITICAL, "qcom,pmic-id(%d) or qcom,board-id(%d) in device tree is not a multiple of (%d %d)\n",
				len_pmic_id, len_board_id, PMIC_ID_SIZE, BOARD_ID_SIZE);
			return NULL;
		}
		dtb_ver = DEV_TREE_VERSION_V3;
		min_plat_id_len = PLAT_ID_SIZE;
	} else if (board_prop && len_board_id > 0) {
		if (len_board_id % BOARD_ID_SIZE)
		{
			dprintf(CRITICAL, "qcom,board-id in device tree is (%d) not a multiple of (%d)\n",
				len_board_id, BOARD_ID_SIZE);
			return NULL;
		}
		dtb_ver = DEV_TREE_VERSION_V2;
		min_plat_id_len = PLAT_ID_SIZE;
	} else {
		dtb_ver = DEV_TREE_VERSION_V1;
		min_plat_id_len = DT_ENTRY_V1_SIZE;
	}

	/* Get the msm-id prop from DTB */
	plat_prop = (const char *)fdt_getprop(dtb, root_offset, "qcom,msm-id", &len_plat_id);
	if (!plat_prop || len_plat_id <= 0) {
		dprintf(INFO, "qcom,msm-id entry not found\n");
		return NULL;
	} else if (len_plat_id % min_plat_id_len) {
		dprintf(INFO, "qcom,msm-id in device tree is (%d) not a multiple of (%d)\n",
			len_plat_id, min_plat_id_len);
		return NULL;
	}

	/*
	 * If DTB version is '1' look for <x y z> pair in the DTB
	 * x: platform_id
	 * y: variant_id
	 * z: SOC rev
	 */
	if (dtb_ver == DEV_TREE_VERSION_V1) {
		cur_dt_entry = (struct dt_entry *)
				malloc(sizeof(struct dt_entry));

		if (!cur_dt_entry) {
			dprintf(CRITICAL, "Out of memory\n");
			return NULL;
		}
		memset(cur_dt_entry, 0, sizeof(struct dt_entry));

		if(len_plat_id>DT_ENTRY_V1_SIZE) {
			dprintf(INFO, "Found multiple dtentries!\n");
		}

		{
			cur_dt_entry->platform_id = fdt32_to_cpu(((const struct dt_entry_v1 *)plat_prop)->platform_id);
			cur_dt_entry->variant_id = fdt32_to_cpu(((const struct dt_entry_v1 *)plat_prop)->variant_id);
			cur_dt_entry->soc_rev = fdt32_to_cpu(((const struct dt_entry_v1 *)plat_prop)->soc_rev);
			cur_dt_entry->board_hw_subtype =
				fdt32_to_cpu(((const struct dt_entry_v1 *)plat_prop)->variant_id) >> 0x18;
			cur_dt_entry->pmic_rev[0] = board_pmic_target(0);
			cur_dt_entry->pmic_rev[1] = board_pmic_target(1);
			cur_dt_entry->pmic_rev[2] = board_pmic_target(2);
			cur_dt_entry->pmic_rev[3] = board_pmic_target(3);
			cur_dt_entry->offset = (uint32_t)dtb;
			cur_dt_entry->size = dtb_size;

			dprintf(SPEW, "Found an appended flattened device tree (%s - %u %u 0x%x)\n",
				*model ? model : "unknown",
				cur_dt_entry->platform_id, cur_dt_entry->variant_id, cur_dt_entry->soc_rev);

		}
	}
	/*
	 * If DTB Version is '3' then we have split DTB with board & msm data & pmic
	 * populated saperately in board-id & msm-id & pmic-id prop respectively.
	 * Extract the data & prepare a look up table
	 */
	else if (dtb_ver == DEV_TREE_VERSION_V2 || dtb_ver == DEV_TREE_VERSION_V3) {
		board_data_count = (len_board_id / BOARD_ID_SIZE);
		msm_data_count = (len_plat_id / PLAT_ID_SIZE);
		/* If dtb version is v2.0, the pmic_data_count will be <= 0 */
		pmic_data_count = (len_pmic_id / PMIC_ID_SIZE);

		/* If we are using dtb v3.0, then we have split board, msm & pmic data in the DTB
		*  If we are using dtb v2.0, then we have split board & msmdata in the DTB
		*/
		board_data = (struct board_id *) malloc(sizeof(struct board_id) * (len_board_id / BOARD_ID_SIZE));
		ASSERT(board_data);
		platform_data = (struct plat_id *) malloc(sizeof(struct plat_id) * (len_plat_id / PLAT_ID_SIZE));
		ASSERT(platform_data);
		if (dtb_ver == DEV_TREE_VERSION_V3) {
			pmic_data = (struct pmic_id *) malloc(sizeof(struct pmic_id) * (len_pmic_id / PMIC_ID_SIZE));
			ASSERT(pmic_data);
		}
		i = 0;

		/* Extract board data from DTB */
		for(i = 0 ; i < board_data_count; i++) {
			board_data[i].variant_id = fdt32_to_cpu(((struct board_id *)board_prop)->variant_id);
			board_data[i].platform_subtype = fdt32_to_cpu(((struct board_id *)board_prop)->platform_subtype);
			/* For V2/V3 version of DTBs we have platform version field as part
			 * of variant ID, in such case the subtype will be mentioned as 0x0
			 * As the qcom, board-id = <0xSSPMPmPH, 0x0>
			 * SS -- Subtype
			 * PM -- Platform major version
			 * Pm -- Platform minor version
			 * PH -- Platform hardware CDP/MTP
			 * In such case to make it compatible with LK algorithm move the subtype
			 * from variant_id to subtype field
			 */
			if (board_data[i].platform_subtype == 0)
				board_data[i].platform_subtype =
					fdt32_to_cpu(((struct board_id *)board_prop)->variant_id) >> 0x18;

			len_board_id -= sizeof(struct board_id);
			board_prop += sizeof(struct board_id);
		}

		/* Extract platform data from DTB */
		for(i = 0 ; i < msm_data_count; i++) {
			platform_data[i].platform_id = fdt32_to_cpu(((struct plat_id *)plat_prop)->platform_id);
			platform_data[i].soc_rev = fdt32_to_cpu(((struct plat_id *)plat_prop)->soc_rev);
			len_plat_id -= sizeof(struct plat_id);
			plat_prop += sizeof(struct plat_id);
		}

		if (dtb_ver == DEV_TREE_VERSION_V3 && pmic_prop) {
			/* Extract pmic data from DTB */
			for(i = 0 ; i < pmic_data_count; i++) {
				pmic_data[i].pmic_version[0]= fdt32_to_cpu(((struct pmic_id *)pmic_prop)->pmic_version[0]);
				pmic_data[i].pmic_version[1]= fdt32_to_cpu(((struct pmic_id *)pmic_prop)->pmic_version[1]);
				pmic_data[i].pmic_version[2]= fdt32_to_cpu(((struct pmic_id *)pmic_prop)->pmic_version[2]);
				pmic_data[i].pmic_version[3]= fdt32_to_cpu(((struct pmic_id *)pmic_prop)->pmic_version[3]);
				len_pmic_id -= sizeof(struct pmic_id);
				pmic_prop += sizeof(struct pmic_id);
			}

			/* We need to merge board & platform data into dt entry structure */
			num_entries = msm_data_count * board_data_count * pmic_data_count;
		} else {
			/* We need to merge board & platform data into dt entry structure */
			num_entries = msm_data_count * board_data_count;
		}

		if ((((uint64_t)msm_data_count * (uint64_t)board_data_count * (uint64_t)pmic_data_count) !=
			msm_data_count * board_data_count * pmic_data_count) ||
			(((uint64_t)msm_data_count * (uint64_t)board_data_count) != msm_data_count * board_data_count)) {

			free(board_data);
			free(platform_data);
			if (pmic_data)
				free(pmic_data);
			if (model)
				free(model);
			return NULL;
		}

		dt_entry_array = (struct dt_entry*) malloc(sizeof(struct dt_entry) * num_entries);
		ASSERT(dt_entry_array);

		/* If we have '<X>; <Y>; <Z>' as platform data & '<A>; <B>; <C>' as board data.
		 * Then dt entry should look like
		 * <X ,A >;<X, B>;<X, C>;
		 * <Y ,A >;<Y, B>;<Y, C>;
		 * <Z ,A >;<Z, B>;<Z, C>;
		 */
		i = 0;
		k = 0;
		n = 0;
		for (i = 0; i < msm_data_count; i++) {
			for (j = 0; j < board_data_count; j++) {
				if (dtb_ver == DEV_TREE_VERSION_V3 && pmic_prop) {
					for (n = 0; n < pmic_data_count; n++) {
						dt_entry_array[k].platform_id = platform_data[i].platform_id;
						dt_entry_array[k].soc_rev = platform_data[i].soc_rev;
						dt_entry_array[k].variant_id = board_data[j].variant_id;
						dt_entry_array[k].board_hw_subtype = board_data[j].platform_subtype;
						dt_entry_array[k].pmic_rev[0]= pmic_data[n].pmic_version[0];
						dt_entry_array[k].pmic_rev[1]= pmic_data[n].pmic_version[1];
						dt_entry_array[k].pmic_rev[2]= pmic_data[n].pmic_version[2];
						dt_entry_array[k].pmic_rev[3]= pmic_data[n].pmic_version[3];
						dt_entry_array[k].offset = (uint32_t)dtb;
						dt_entry_array[k].size = dtb_size;
						k++;
					}

				} else {
					dt_entry_array[k].platform_id = platform_data[i].platform_id;
					dt_entry_array[k].soc_rev = platform_data[i].soc_rev;
					dt_entry_array[k].variant_id = board_data[j].variant_id;
					dt_entry_array[k].board_hw_subtype = board_data[j].platform_subtype;
					dt_entry_array[k].pmic_rev[0]= board_pmic_target(0);
					dt_entry_array[k].pmic_rev[1]= board_pmic_target(1);
					dt_entry_array[k].pmic_rev[2]= board_pmic_target(2);
					dt_entry_array[k].pmic_rev[3]= board_pmic_target(3);
					dt_entry_array[k].offset = (uint32_t)dtb;
					dt_entry_array[k].size = dtb_size;
					k++;
				}
			}
		}

		if (num_entries>0) {
			if(num_entries>1) {
				dprintf(INFO, "Found multiple dtentries!\n");
			}

			dprintf(SPEW, "Found an appended flattened device tree (%s - %u %u %u 0x%x)\n",
				*model ? model : "unknown",
				dt_entry_array[i].platform_id, dt_entry_array[i].variant_id, dt_entry_array[i].board_hw_subtype, dt_entry_array[i].soc_rev);

			// allocate dt entry
			cur_dt_entry = (struct dt_entry *) malloc(sizeof(struct dt_entry));
			if (!cur_dt_entry) {
				dprintf(CRITICAL, "Out of memory\n");
				return NULL;
			}

			// copy entry
			memcpy(cur_dt_entry, &dt_entry_array[0], sizeof(struct dt_entry));
		}

		free(board_data);
		free(platform_data);
		if (pmic_data)
			free(pmic_data);
		free(dt_entry_array);
	}
	if (model)
		free(model);
	return cur_dt_entry;
}

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
					add_meminfo((uint32_t)base, (uint32_t)size);
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

	// get dt entry
	struct dt_entry* dt_entry = get_dt_entry(fdt, fdt_totalsize(fdt));
	if(!dt_entry) {
		dprintf(CRITICAL, "Could not find dt entry.\n");
	}
	else {
		platform_id = dt_entry->platform_id;
		variant_id = dt_entry->variant_id;
		soc_rev = dt_entry->soc_rev;
		has_board_info = true;

		dprintf(INFO, "platform_id=%d variant_id=%d soc_rev=%X\n", platform_id, variant_id, soc_rev);
	}

	return 0;
}

bool lkargs_has_board_info(void) {
	return has_board_info;
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
	dprintf(INFO, "[orig] platform_id=%d variant_id=%d soc_rev=%X\n", board_platform_id(), board_hardware_id(), board_soc_version());
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

#include <debug.h>
#include <string.h>
#include <malloc.h>
#include <board.h>
#include <target.h>
#include <stdlib.h>
#include <baseband.h>
#include <boot_device.h>
#include <partition_parser.h>
#include <lib/atagparse.h>
#include <lib/cmdline.h>
#include "atags.h"

#include <libfdt.h>
#include <dev_tree.h>

#ifdef MDTP_SUPPORT
#include "mdtp.h"
#endif

typedef struct {
    struct list_node node;

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

typedef struct {
    struct list_node node;

    const char *name;
    uint32_t value;
} qcid_item_t;

typedef struct {
    uint32_t pmic_model;
    uint32_t pmic_minor;
    uint32_t pmic_major;
} qcpmicinfo_t;

typedef struct {
    // platform_id (x)
    uint32_t msm_id;
    uint32_t foundry_id;

    // variant_id (y)
    uint32_t platform_hw;
    uint32_t platform_major;
    uint32_t platform_minor;
    //uint32_t platform_subtype;

    // soc_rev (z)
    uint32_t soc_rev;

    // platform_subtype (y')
    uint32_t subtype;
    uint32_t ddr;
    uint32_t panel;
    uint32_t bootdev;

    // pmic_rev
    qcpmicinfo_t pmic_rev[4];
} qchwinfo_t;

// lk boot args
extern uint32_t lk_boot_args[4];

// atags backup
static void  *tags_copy = NULL;
static size_t tags_size = 0;

// parsed data: internal
static char *command_line = NULL;
static struct list_node meminfo;
static int is_firststage = 0;

// parsed data: accessible via public functions
static struct list_node cmdline_list;
static char *uefi_bootpart = NULL;
static struct list_node qciditem_list;

extern int get_target_boot_params(const char *cmdline, const char *part,
                                  char *buf, int buflen);
static void generate_qcom_cmdline(int is_recovery);

static void qciditem_add(const char *name, uint32_t value)
{
    qcid_item_t *item = malloc(sizeof(qcid_item_t));
    ASSERT(item);

    item->name = strdup(name);
    item->value = value;

    list_add_tail(&qciditem_list, &item->node);
}

int qciditem_get(const char *name, uint32_t *datap)
{
    qcid_item_t *entry;
    list_for_every_entry(&qciditem_list, entry, qcid_item_t, node) {
        if (!strcmp(entry->name, name)) {
            *datap = entry->value;
            return 0;
        }
    }

    return -1;
}

uint32_t qciditem_get_zero(const char *name)
{
    uint32_t data = 0;
    qciditem_get(name, &data);
    return data;
}


const char *lkargs_get_command_line(int is_recovery)
{
    static char *prev = NULL;
    if (prev) {
        free(prev);
        prev = NULL;
    }

    // we can't do this earlier because all hw needs to be initialized
    if (is_firststage)
        generate_qcom_cmdline(is_recovery);

    // cmdline
    size_t cmdline_len = cmdline_length(&cmdline_list);
    if (cmdline_len) {
        prev = malloc(cmdline_len);
        if (prev) {
            prev[0] = 0;
            cmdline_generate(&cmdline_list, prev, cmdline_len);
        }
    }

    return prev;
}

struct list_node *lkargs_get_command_line_list(void)
{
    return &cmdline_list;
}

static char *strsep(char **stringp, const char *delim)
{
    if (*stringp == NULL) { return NULL; }
    char *token_start = *stringp;
    *stringp = strpbrk(token_start, delim);
    if (*stringp) {
        **stringp = '\0';
        (*stringp)++;
    }
    return token_start;
}

char *lkargs_get_panel_name(const char *key)
{
    const char *value = cmdline_get(&cmdline_list, key);
    if (!value) return NULL;

    char *valdup = strdup(value);
    if (!valdup) return NULL;

    char *token;
    char *ret = NULL;
    while ((token = strsep(&valdup, ":"))) {
        if (strncmp(token, "qcom,", 4)) continue;

        ret = strdup(token);
        break;
    }

    return ret;
}

const char *lkargs_get_uefi_bootpart(void)
{
    if (uefi_bootpart)
        return uefi_bootpart;
    return "";
}

const void *lkargs_get_tags_backup(void)
{
    return tags_copy;
}
size_t lkargs_get_tags_backup_size(void)
{
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
    if (!tags_copy) {
        dprintf(CRITICAL, "Error saving atags!\n");
        return -1;
    }

    memcpy(tags_copy, tags, tags_size);
    return 0;
}

static int atags_check_header(void *tags)
{
    struct tag *atags = (struct tag *)tags;
    return atags->hdr.tag!=ATAG_CORE;
}

static int save_fdt(void *fdt)
{
    tags_size = fdt_totalsize(fdt);
    tags_copy = malloc(tags_size);
    if (!tags_copy) {
        dprintf(CRITICAL, "Error saving fdt!\n");
        return -1;
    }

    memcpy(tags_copy, fdt, tags_size);
    return 0;
}

// parse ATAGS
static int parse_atag_core(const struct tag *tag)
{
    return 0;
}

static void add_meminfo(uint64_t start, uint64_t size)
{
    dprintf(INFO, "0x%016llx-0x%016llx\n", start, start+size-1);

    meminfo_t *item = malloc(sizeof(meminfo_t));
    ASSERT(item);

    item->start = start;
    item->size = size;

    list_add_tail(&meminfo, &item->node);
}

static int parse_atag_mem32(const struct tag *tag)
{
    add_meminfo((uint64_t)tag->u.mem.start, (uint64_t)tag->u.mem.size);
    return 0;
}

static int parse_atag_cmdline(const struct tag *tag)
{
    command_line = malloc(COMMAND_LINE_SIZE+1);
    if (!tags_copy) {
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

static struct tagtable *get_tagtable_entry(const struct tag *tag)
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

void *lkargs_atag_insert_unknown(void *tags)
{
    struct tag *tag = (struct tag *)tags;
    const struct tag *t;

    if (!tags_copy || atags_check_header(tags_copy))
        return tag;

    for (t=tags_copy; t->hdr.size; t=tag_next(t)) {
        if (!get_tagtable_entry(t)) {
            tag = tag_next(tag);
            memcpy(tag, t, t->hdr.size*sizeof(uint32_t));
        }
    }

    return tag;
}

void *lkargs_get_mmap_callback(void *pdata, lkargs_mmap_cb_t cb)
{
    meminfo_t *entry;
    list_for_every_entry(&meminfo, entry, meminfo_t, node) {
        pdata = cb(pdata, entry->start, entry->size, false);
    }

    return pdata;
}

// parse FDT
static int fdt_get_cell_sizes(void *fdt, uint32_t *out_addr_cell_size, uint32_t *out_size_cell_size)
{
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
    } else {
        addr_cell_size = fdt32_to_cpu(*valp);
    }

    // find the #size-cells size
    valp = fdt_getprop(fdt, offset, "#size-cells", &len);
    if (len<=0 || !valp) {
        if (len == -FDT_ERR_NOTFOUND)
            size_cell_size = 2;
        else return -1;
    } else {
        size_cell_size = fdt32_to_cpu(*valp);
    }

    *out_addr_cell_size = addr_cell_size;
    *out_size_cell_size = size_cell_size;

    return 0;
}

static void print_qchwinfo(const char *prefix, qchwinfo_t *hwinfo)
{
    dprintf(INFO, "%splat=%u/%u variant=%u/%u/%u socrev=%x subtype=%u/%u/%u/%u pmic_rev=<%u/%u/%u> <%u/%u/%u> <%u/%u/%u> <%u/%u/%u>\n",
            prefix?:"",
            hwinfo->msm_id, hwinfo->foundry_id,
            hwinfo->platform_hw, hwinfo->platform_major, hwinfo->platform_minor,
            hwinfo->soc_rev,
            hwinfo->subtype, hwinfo->ddr, hwinfo->panel, hwinfo->bootdev,
            hwinfo->pmic_rev[0].pmic_model, hwinfo->pmic_rev[0].pmic_minor, hwinfo->pmic_rev[0].pmic_major,
            hwinfo->pmic_rev[1].pmic_model, hwinfo->pmic_rev[1].pmic_minor, hwinfo->pmic_rev[1].pmic_major,
            hwinfo->pmic_rev[2].pmic_model, hwinfo->pmic_rev[2].pmic_minor, hwinfo->pmic_rev[2].pmic_major,
            hwinfo->pmic_rev[3].pmic_model, hwinfo->pmic_rev[3].pmic_minor, hwinfo->pmic_rev[3].pmic_major
           );
}

static int parse_fdt(void *fdt, qchwinfo_t **hwinfo)
{
    int ret = 0;
    uint32_t offset;
    int len;
    uint32_t i;

    // get memory node
    ret = fdt_path_offset(fdt, "/memory");
    if (ret < 0) {
        dprintf(CRITICAL, "Could not find memory node.\n");
    } else {
        offset = ret;

        uint32_t addr_cell_size = 1;
        uint32_t size_cell_size = 1;
        fdt_get_cell_sizes(fdt, &addr_cell_size, &size_cell_size);
        if (addr_cell_size>2 || size_cell_size>2) {
            dprintf(CRITICAL, "unsupported cell sizes\n");
            goto next;
        }

        // get reg node
        const uint32_t *reg = fdt_getprop(fdt, offset, "reg", &len);
        if (!reg) {
            dprintf(CRITICAL, "Could not find reg node.\n");
        } else {
            uint32_t regpos = 0;
            while (regpos<len/sizeof(uint32_t)) {
                uint64_t base = fdt32_to_cpu(reg[regpos++]);
                if (addr_cell_size==2) {
                    base = base<<32;
                    base = fdt32_to_cpu(reg[regpos++]);
                }

                uint64_t size = fdt32_to_cpu(reg[regpos++]);
                if (size_cell_size==2) {
                    size = size<<32;
                    size = fdt32_to_cpu(reg[regpos++]);
                }

                add_meminfo(base, size);
            }
        }
    }

next:
    // get chosen node
    ret = fdt_path_offset(fdt, "/chosen");
    if (ret < 0) {
        dprintf(CRITICAL, "Could not find chosen node.\n");
        return ret;
    } else {
        offset = ret;

        // get bootargs
        const char *bootargs = (const char *)fdt_getprop(fdt, offset, "bootargs", &len);
        if (bootargs && len>0) {
            command_line = malloc(len);
            memcpy(command_line, bootargs, len);
        }
    }

    // get root node
    ret = fdt_path_offset(fdt, "/");
    if (ret < 0) {
        dprintf(CRITICAL, "Could not find root node.\n");
    }
    offset = ret;

    // get socinfo property
    int len_socinfo;
    const struct fdt_property *prop_socinfo = fdt_get_property(fdt, offset, "efidroid-soc-info", &len_socinfo);
    if (!prop_socinfo) {
        dprintf(CRITICAL, "Could not find efidroid-soc-info.\n");
    } else {
        // read info from fdt
        const efidroid_fdtinfo_t *socinfo = (const efidroid_fdtinfo_t *)prop_socinfo->data;
        uint32_t platform_id = fdt32_to_cpu(socinfo->chipset);
        uint32_t variant_id = fdt32_to_cpu(socinfo->platform);
        uint32_t hw_subtype = fdt32_to_cpu(socinfo->subtype);
        uint32_t soc_rev = fdt32_to_cpu(socinfo->revNum);
        uint32_t pmic_model[4] = {
            fdt32_to_cpu(socinfo->pmic_model[0]),
            fdt32_to_cpu(socinfo->pmic_model[1]),
            fdt32_to_cpu(socinfo->pmic_model[2]),
            fdt32_to_cpu(socinfo->pmic_model[3]),
        };

        // if subtype is 0, we have to use the subtype id from the variant_id
        if (hw_subtype==0) {
            hw_subtype = (variant_id&0xff000000)>>24;
        }

        // build hwinfo_tags
        qchwinfo_t *hwinfo_tags = calloc(1, sizeof(qchwinfo_t));
        ASSERT(hwinfo_tags);
        hwinfo_tags->msm_id = platform_id&0x0000ffff;
        hwinfo_tags->foundry_id = (platform_id&0x00ff0000)>>16;
        hwinfo_tags->platform_hw = variant_id&0x000000ff;
        hwinfo_tags->platform_minor = (variant_id&0x0000ff00)>>8;
        hwinfo_tags->platform_major = (variant_id&0x00ff0000)>>16;
        hwinfo_tags->platform_minor = (soc_rev&0xff);
        hwinfo_tags->platform_major = (soc_rev>>16)&0xff;
        hwinfo_tags->soc_rev = soc_rev;
        hwinfo_tags->subtype = hw_subtype&0x000000ff;
        hwinfo_tags->ddr = (hw_subtype&0x700)>>8;
        hwinfo_tags->panel = (hw_subtype&0x1800)>>11;
        hwinfo_tags->bootdev = (hw_subtype&0xf0000)>>16;
        for (i=0; i<4; i++) {
            hwinfo_tags->pmic_rev[i].pmic_model = (pmic_model[i]&0x000000ff);
            hwinfo_tags->pmic_rev[i].pmic_minor = (pmic_model[i]&0x0000ff00)>>8;
            hwinfo_tags->pmic_rev[i].pmic_major = (pmic_model[i]&0x00ff0000)>>16;
        }
        *hwinfo = hwinfo_tags;
    }

    return 0;
}

static int lkargs_fdt_insert_properties(void *fdtdst, int offsetdst, const void *fdtsrc, int offsetsrc)
{
    int len;
    int offset;
    int ret;

    offset = offsetsrc;
    for (offset = fdt_first_property_offset(fdtsrc, offset);
            (offset >= 0);
            (offset = fdt_next_property_offset(fdtsrc, offset))) {
        const struct fdt_property *prop;

        if (!(prop = fdt_get_property_by_offset(fdtsrc, offset, &len))) {
            offset = -FDT_ERR_INTERNAL;
            break;
        }

        const char *name = fdt_string(fdtsrc, fdt32_to_cpu(prop->nameoff));
        dprintf(SPEW, "PROP: %s\n", name);

        // blacklist our nodes
        if (!strcmp(name, "bootargs"))
            continue;
        if (!strcmp(name, "linux,initrd-start"))
            continue;
        if (!strcmp(name, "linux,initrd-end"))
            continue;

        // set prop
        ret = fdt_setprop(fdtdst, offsetdst, name, prop->data, len);
        if (ret) {
            dprintf(CRITICAL, "can't set prop: %s\n", fdt_strerror(ret));
            continue;
        }
    }

    return 0;
}

static int lkargs_fdt_insert_nodes(void *fdt, int target_offset)
{
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
            offset = fdt_next_node(tags_copy, offset, &depth)) {
        const char *name = fdt_get_name(tags_copy, offset, NULL);
        dprintf(SPEW, "NODE: %s\n", name);

        // get/create node
        if (!strcmp(name, "chosen")) {
            ret = target_offset;
        } else {
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

int lkargs_insert_chosen(void *fdt)
{
    int ret = 0;
    uint32_t target_offset_chosen;

    if (!tags_copy || fdt_check_header(tags_copy))
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

__WEAK void *lkargs_platform_get_mmap(void *pdata, lkargs_mmap_cb_t cb)
{
    unsigned int i;
    ram_partition ptn_entry;

    // Make sure RAM partition table is initialized
    if (!smem_ram_ptable_init_v1()) {
        ASSERT(0);
    }

    // add meminfo
    for (i = 0; i<smem_get_ram_ptable_len(); i++) {
        smem_get_ram_ptable_entry(&ptn_entry, i);
        if (ptn_entry.category==SDRAM && ptn_entry.type==SYS_MEMORY) {
            cb(pdata, ptn_entry.start, ptn_entry.size, 0);
        }
    }

    return pdata;
}

static void *add_meminfo_cb(void *pdata, uint64_t addr, uint64_t size, bool reserved)
{
    add_meminfo(addr, size);
    return pdata;
}

#if VERIFIED_BOOT
#if !VBOOT_MOTA
// indexed based on enum values, green is 0 by default
struct verified_boot_verity_mode vbvm[] = {
    {false, "logging"},
    {true, "enforcing"},
};
struct verified_boot_state_name vbsn[] = {
    {GREEN, "green"},
    {ORANGE, "orange"},
    {YELLOW,"yellow"},
    {RED,"red" },
};
#endif
#endif

static void generate_qcom_cmdline(int is_recovery)
{
    char sn_buf[13];
    char display_panel_buf[128];
    char target_boot_params[64];

    // clear the cmdline list
    cmdline_free(&cmdline_list);

    // boot device
    if (target_is_emmc_boot()) {
#if USE_BOOTDEV_CMDLINE
        char *boot_dev_buf = (char *) malloc(sizeof(char) * 64);
        ASSERT(boot_dev_buf);
        platform_boot_dev_cmdline(boot_dev_buf);
        cmdline_add(&cmdline_list, "androidboot.bootdevice", boot_dev_buf, 1);
        free(boot_dev_buf);
#else
        cmdline_add(&cmdline_list, "androidboot.emmc", "true", 1);
#endif
    }

    // serial number
    sn_buf[0] = 0;
    target_serialno((unsigned char *)sn_buf);
    cmdline_add(&cmdline_list, "androidboot.serialno", sn_buf, 1);

    // verity
#if VERIFIED_BOOT
#if !VBOOT_MOTA
    cmdline_add(&cmdline_list, "androidboot.verifiedbootstate", vbsn[boot_verify_get_state()].name, 1);
    cmdline_add(&cmdline_list, "androidboot.veritymode", vbvm[device.verity_mode].name, 1);
#endif
#endif

    // mdtp
#ifdef MDTP_SUPPORT
    bool is_mdtp_activated = 0;
    mdtp_activated(&is_mdtp_activated);
    if (is_mdtp_activated)
        cmdline_add(&cmdline_list, "mdtp", NULL, 1);
#endif

    // bootmode
    // this probably has to be added to aboot since we don't have access to this from here
#if 0
    if (boot_into_ffbm) {
        cmdline_add(&cmdline_list, "androidboot.mode", ffbm_mode_string, 1);
        /* reduce kernel console messages to speed-up boot */
        cmdline_add(&cmdline_list, "quiet", NULL, 1);
    } else if (boot_reason_alarm) {
        cmdline_add(&cmdline_list, "androidboot.alarmboot", "true", 1);
    } else if (device.charger_screen_enabled && target_pause_for_battery_charge()) {
        cmdline_add(&cmdline_list, "androidboot.mode", "charger", 1);
    }
#endif

    // rootfs
    if (get_target_boot_params("", is_recovery?"recoveryfs":"system", target_boot_params,sizeof(target_boot_params)) == 0) {
        cmdline_addall(&cmdline_list, target_boot_params, 1);
    }

    // baseband
    switch (target_baseband()) {
        case BASEBAND_APQ:
            cmdline_add(&cmdline_list, "androidboot.baseband", "apq", 1);
            break;

        case BASEBAND_MSM:
            cmdline_add(&cmdline_list, "androidboot.baseband", "msm", 1);
            break;

        case BASEBAND_CSFB:
            cmdline_add(&cmdline_list, "androidboot.baseband", "csfb", 1);
            break;

        case BASEBAND_SVLTE2A:
            cmdline_add(&cmdline_list, "androidboot.baseband", "svlte2a", 1);
            break;

        case BASEBAND_MDM:
            cmdline_add(&cmdline_list, "androidboot.baseband", "mdm", 1);
            break;

        case BASEBAND_MDM2:
            cmdline_add(&cmdline_list, "androidboot.baseband", "mdm2", 1);
            break;

        case BASEBAND_SGLTE:
            cmdline_add(&cmdline_list, "androidboot.baseband", "sglte", 1);
            break;

        case BASEBAND_SGLTE2:
            cmdline_add(&cmdline_list, "androidboot.baseband", "sglte2", 1);
            break;

        case BASEBAND_DSDA:
            cmdline_add(&cmdline_list, "androidboot.baseband", "dsda", 1);
            break;

        case BASEBAND_DSDA2:
            cmdline_add(&cmdline_list, "androidboot.baseband", "dsda2", 1);
            break;
    }

    // display panel
    display_panel_buf[0] = 0;
    if (target_display_panel_node(display_panel_buf, sizeof(display_panel_buf)) && strlen(display_panel_buf)) {
        cmdline_addall(&cmdline_list, display_panel_buf, 1);
    }

    // warmboot
    if (target_warm_boot()) {
        cmdline_add(&cmdline_list, "qpnp-power-on.warm_boot", "1", 1);
    }
}

static int handle_firststage(void)
{
    // Make sure RAM partition table is initialized
    if (!smem_ram_ptable_init_v1()) {
        ASSERT(0);
    }

    // add meminfo
    lkargs_platform_get_mmap(NULL, add_meminfo_cb);

    is_firststage = 1;

    return 0;
}

void atag_parse(void)
{
    uint32_t i;
    qchwinfo_t *hwinfo_tags = NULL;
    qchwinfo_t *hwinfo_lk = NULL;
    uint32_t machinetype;

    dprintf(INFO, "bootargs: 0x%x 0x%x 0x%x 0x%x\n",
            lk_boot_args[0],
            lk_boot_args[1],
            lk_boot_args[2],
            lk_boot_args[3]
           );

    // init
    cmdline_init(&cmdline_list);
    list_initialize(&qciditem_list);
    list_initialize(&meminfo);

    machinetype = board_machtype();
    if (machinetype==LINUX_MACHTYPE_UNKNOWN) {
        // this board probably doesn't have official atags support
        machinetype = board_target_id();
    }

    // fdt
    void *tags = (void *)lk_boot_args[2];
    if (!fdt_check_header(tags)) {
        save_fdt(tags);
        parse_fdt(tags, &hwinfo_tags);
    }

    // atags
    else if (!atags_check_header(tags)) {
        // use provided machine type
        machinetype = lk_boot_args[1];

        save_atags(tags);
        parse_atags(tags);
    }

    // unknown
    else {
        dprintf(CRITICAL, "Invalid atags - assume 1st-stage boot.\n");
        handle_firststage();
    }
    dprintf(INFO, "machinetype: %u\n", machinetype);

    // print cmdline
    dprintf(INFO, "cmdline=[%s]\n", command_line);
    if (command_line) {
        cmdline_addall(&cmdline_list, command_line, true);

        // this is for recovery mode only and we'll add it ourselves
        cmdline_remove(&cmdline_list, "gpt");

        // this happens if we were loaded by EFIDroid
        cmdline_remove(&cmdline_list, "rdinit");
        cmdline_remove(&cmdline_list, "multiboot.debug");
    }

    // get bootmode
    const char *bootpart = cmdline_get(&cmdline_list, "uefi.bootpart");
    if (bootpart) {
        dprintf(INFO, "uefi.bootpart = [%s]\n", bootpart);

        // copy value
        uefi_bootpart = strdup(bootpart);

        // remove from list so it doesn't end up in the kernel cmdline
        cmdline_remove(&cmdline_list, "uefi.bootpart");
    }

    // build and print hwinfo_lk
    {
        uint32_t platform_id = board_platform_id();
        uint32_t foundry_id = (platform_id&0x00ff0000)>>16;
        foundry_id = MAX(foundry_id, board_foundry_id());
        uint32_t soc_rev = board_soc_version();

        // allocate
        hwinfo_lk = calloc(1, sizeof(qchwinfo_t));
        ASSERT(hwinfo_lk);

        hwinfo_lk->msm_id = platform_id&0x0000ffff;
        hwinfo_lk->foundry_id = foundry_id;
        hwinfo_lk->platform_hw = board_hardware_id();
        hwinfo_lk->platform_minor = (soc_rev&0xff);
        hwinfo_lk->platform_major = (soc_rev>>16)&0xff;
        hwinfo_lk->soc_rev = soc_rev;
        hwinfo_lk->subtype = board_hardware_subtype();
        hwinfo_lk->ddr = board_get_ddr_subtype();
        hwinfo_lk->panel = platform_detect_panel();
        hwinfo_lk->bootdev = platform_get_boot_dev();
        for (i=0; i<4; i++) {
            uint32_t pmic_model = board_pmic_target(i);
            hwinfo_lk->pmic_rev[i].pmic_model = (pmic_model&0x000000ff);
            hwinfo_lk->pmic_rev[i].pmic_minor = (pmic_model&0x0000ff00)>>8;
            hwinfo_lk->pmic_rev[i].pmic_major = (pmic_model&0x00ff0000)>>16;
        }

        print_qchwinfo("[LK]   ", hwinfo_lk);
    }

    // build info based on hwinfo_lk
    uint32_t platform_id = hwinfo_lk->msm_id | (hwinfo_lk->foundry_id<<16); // EQ
    uint32_t platform_hw = hwinfo_lk->platform_hw; // EQ
    uint32_t subtype = hwinfo_lk->subtype; // EQ
    uint32_t platform_subtype = (hwinfo_lk->subtype) | (hwinfo_lk->ddr << 8) | (hwinfo_lk->panel << 11) | (hwinfo_lk->bootdev << 16); // EQ
    uint32_t soc_rev = hwinfo_lk->soc_rev; // LE
    uint32_t variant_id_platform_hw = hwinfo_lk->platform_hw; // LE
    uint32_t variant_id_platform_minor = hwinfo_lk->platform_minor; // LE
    uint32_t variant_id_platform_major = hwinfo_lk->platform_major; // LE
    uint32_t variant_id_subtype = hwinfo_lk->subtype; // LE
    uint32_t pmicrev1 = (hwinfo_lk->pmic_rev[0].pmic_model) | (hwinfo_lk->pmic_rev[0].pmic_minor << 8) | (hwinfo_lk->pmic_rev[0].pmic_major << 16); // LE
    uint32_t pmicrev2 = (hwinfo_lk->pmic_rev[1].pmic_model) | (hwinfo_lk->pmic_rev[1].pmic_minor << 8) | (hwinfo_lk->pmic_rev[1].pmic_major << 16); // LE
    uint32_t pmicrev3 = (hwinfo_lk->pmic_rev[2].pmic_model) | (hwinfo_lk->pmic_rev[2].pmic_minor << 8) | (hwinfo_lk->pmic_rev[2].pmic_major << 16); // LE
    uint32_t pmicrev4 = (hwinfo_lk->pmic_rev[3].pmic_model) | (hwinfo_lk->pmic_rev[3].pmic_minor << 8) | (hwinfo_lk->pmic_rev[3].pmic_major << 16); // LE
    uint32_t foundry_id = hwinfo_lk->foundry_id; // EQ

    // improve info using hwinfo_tags
    if (hwinfo_tags) {
        // print hwinfo
        print_qchwinfo("[TAGS] ", hwinfo_tags);

        // use exact-match values from tags
        platform_id = hwinfo_tags->msm_id | (hwinfo_tags->foundry_id<<16);
        platform_hw = hwinfo_tags->platform_hw;
        subtype = hwinfo_tags->subtype;
        platform_subtype = (hwinfo_tags->subtype) | (hwinfo_tags->ddr << 8) | (hwinfo_tags->panel << 11) | (hwinfo_tags->bootdev << 16);
        foundry_id = hwinfo_tags->foundry_id;

        // use LE values from tags if they are bigger
        if (hwinfo_tags->soc_rev > hwinfo_lk->soc_rev)
            soc_rev = hwinfo_tags->soc_rev;
        // variant_id
        if (hwinfo_tags->platform_hw > variant_id_platform_hw)
            variant_id_platform_hw = hwinfo_tags->platform_hw;
        if (hwinfo_tags->platform_minor > variant_id_platform_minor)
            variant_id_platform_minor = hwinfo_tags->platform_minor;
        if (hwinfo_tags->platform_major > variant_id_platform_major)
            variant_id_platform_major = hwinfo_tags->platform_major;
        if (hwinfo_tags->subtype > variant_id_subtype)
            variant_id_subtype = hwinfo_tags->subtype;

        // always use pmicrev from LK until we tested this
    }

    // add info to qciditem list
    uint32_t variant_id = (variant_id_platform_hw) | (variant_id_platform_minor << 8) | (variant_id_platform_major << 16) | (variant_id_subtype << 24);
    qciditem_add("qcom,platform_id", platform_id); // libboot_qcdt_platform_id
    qciditem_add("qcom,platform_hw", platform_hw); // libboot_qcdt_hardware_id
    qciditem_add("qcom,subtype", subtype); // libboot_qcdt_hardware_subtype
    qciditem_add("qcom,platform_subtype", platform_subtype); // libboot_qcdt_get_hlos_subtype
    qciditem_add("qcom,soc_rev", soc_rev); // libboot_qcdt_soc_version
    qciditem_add("qcom,variant_id", variant_id); // libboot_qcdt_target_id
    qciditem_add("qcom,pmic_rev1", pmicrev1); // libboot_qcdt_pmic_target
    qciditem_add("qcom,pmic_rev2", pmicrev2); // libboot_qcdt_pmic_target
    qciditem_add("qcom,pmic_rev3", pmicrev3); // libboot_qcdt_pmic_target
    qciditem_add("qcom,pmic_rev4", pmicrev4); // libboot_qcdt_pmic_target
    qciditem_add("qcom,foundry_id", foundry_id); // libboot_qcdt_foundry_id
    qciditem_add("qcom,machtype", machinetype);

    // cleanup
    free(hwinfo_lk);
    free(hwinfo_tags);
}

#include <stdlib.h>
#include <string.h>

#include <platform.h>
#include <target.h>
#include <reboot.h>
#include <smem.h>
#include <mmc.h>
#include <partition_parser.h>
#include <platform/timer.h>
#include <platform/iomap.h>
#include <dev/fbcon.h>
#include <linux/elf.h>
#include <kernel/thread.h>
#include <boot_stats.h>
#include <lib/hex2unsigned.h>

#if WITH_LIB_BIO
#include <lib/bio.h>
#endif

#ifdef WITH_LIB_ATAGPARSE
#include <lib/atagparse.h>
#endif

#ifdef WITH_LIB_BOOT
#include <lib/boot.h>
#endif

#ifdef WITH_LIB_PRAM
#include <lib/persistent_ram.h>
#endif

#include "fastboot.h"
#include "bootimg.h"

const char *smem_attr2str(int i)
{
    switch (i) {
        case DEFAULT_ATTRB:
            return "default";
        case READ_ONLY:
            return "ro";
        case READWRITE:
            return "rw";
        default:
            return "-";
    }
}

const char *smem_category2str(int i)
{
    switch (i) {
        case DEFAULT_CATEGORY:
            return "default";
        case SMI:
            return "smi";
        case EBI1:
            return "ebi1";
        case EBI2:
            return "ebi2";
        case QDSP6:
            return "qdsp6";
        case IRAM:
            return "iram";
        case IMEM:
            return "imem";
        case EBI0_CS0:
            return "ebi0_cs0";
        case EBI0_CS1:
            return "ebi0_cs1";
        case EBI1_CS0:
            return "ebi1_cs0";
        case EBI1_CS1:
            return "ebi1_cs1";
        case SDRAM:
            return "sdram";
        default:
            return "-";
    }
}

const char *smem_domain2str(int i)
{
    switch (i) {
        case DEFAULT_DOMAIN:
            return "default";
        case APPS_DOMAIN:
            return "apps";
        case MODEM_DOMAIN:
            return "modem";
        case SHARED_DOMAIN:
            return "shared";
        default:
            return "-";
    }
}

const char *smem_type2str(int i)
{
    switch (i) {
        case SYS_MEMORY:
            return "sys";
        case BOOT_REGION_MEMORY1:
            return "boot1";
        case BOOT_REGION_MEMORY2:
            return "boot2";
        case APPSBL_MEMORY:
            return "appsbl";
        case APPS_MEMORY:
            return "apps";
        default:
            return "-";
    }
}

/* fastboot command function pointer */
typedef void (*fastboot_cmd_fn) (const char *, void *, unsigned);

struct fastboot_cmd_desc {
    const char *name;
    fastboot_cmd_fn cb;
};

void cmd_oem_reboot_recovery(const char *arg, void *data, unsigned sz)
{
    fastboot_okay("");
    reboot_device(RECOVERY_MODE);
}

void cmd_oem_reboot_download(const char *arg, void *data, unsigned sz)
{
    fastboot_okay("");
    if (set_download_mode(EMERGENCY_DLOAD)) {
        dprintf(CRITICAL,"dload mode not supported by target\n");
    } else {
        reboot_device(DLOAD);
        dprintf(CRITICAL,"Failed to reboot into dload mode\n");
    }
}

void cmd_poweroff(const char *arg, void *data, unsigned sz)
{
    fastboot_info("You have 5s to unplug your USB cable :)");
    fastboot_okay("");
    mdelay(5000);
    shutdown_device();
}

#if WITH_DEBUG_LOG_BUF
void cmd_oem_lk_log(const char *arg, void *data, unsigned sz)
{
    fastboot_send_string_human(lk_log_getbuf(), lk_log_getsize());
    fastboot_okay("");
}
#endif

static char *get_human_size(double size, char *buf)
{
    int i = 0;
    const char *units[] = {"B", "kB", "MB", "GB", "TB", "PB", "EB", "ZB", "YB"};
    while (size>=1024) {
        size/=1024;
        i++;
    }
    sprintf(buf, "%.4u%.2s", (uint32_t)size, units[i]);
    return buf;
}

static void cmd_oem_ram_ptable(const char *arg, void *data, unsigned sz)
{
    unsigned int i;
    ram_partition ptn_entry;
    char buf[200];

    // Make sure RAM partition table is initialized
    if (!smem_ram_ptable_init_v1()) {
        fastboot_fail("error reading RAM ptable");
        return;
    }

    // print header
    fastboot_send_string_human("ID\tAddress                              \t  Size\tAttr\tCat\tDomain\tType\tParts\n", 0);

    // print table
    for (i = 0; i<smem_get_ram_ptable_len(); i++) {
        smem_get_ram_ptable_entry(&ptn_entry, i);

        char sizebuf[1024];
        snprintf(buf, sizeof(buf), "%u:\t0x%016llx-0x%016llx\t%s\t%s\t%s\t%s\t%s\t%u\n", i,
                 ptn_entry.start, ptn_entry.start+ptn_entry.size,
                 get_human_size(ptn_entry.size, sizebuf), smem_attr2str(ptn_entry.attr),
                 smem_category2str(ptn_entry.category), smem_domain2str(ptn_entry.domain),
                 smem_type2str(ptn_entry.type), ptn_entry.num_partitions);
        fastboot_send_string_human(buf, 0);
    }

    fastboot_okay("");
}

static void cmd_oem_fbconfig(const char *arg, void *data, unsigned sz)
{
    struct fbcon_config *config = fbcon_display();
    char buf[1024];

    fastboot_info("fbcon_config:");

    snprintf(buf, sizeof(buf), "\tbase: %p (end: %p)", (void *)config->base, config->base + (config->width * config->height * config->bpp/3));
    fastboot_info(buf);
    snprintf(buf, sizeof(buf), "\twidth: %u", config->width);
    fastboot_info(buf);
    snprintf(buf, sizeof(buf), "\theight: %u", config->height);
    fastboot_info(buf);
    snprintf(buf, sizeof(buf), "\tstride: %u", config->stride);
    fastboot_info(buf);
    snprintf(buf, sizeof(buf), "\tbpp: %u", config->bpp);
    fastboot_info(buf);
    snprintf(buf, sizeof(buf), "\tformat: %u", config->format);
    fastboot_info(buf);
    snprintf(buf, sizeof(buf), "\tupdate_start: %p", config->update_start);
    fastboot_info(buf);
    snprintf(buf, sizeof(buf), "\tupdate_done: %p", config->update_done);
    fastboot_info(buf);

    fastboot_okay("");
}

static void cmd_oem_bootaddresses(const char *arg, void *data, unsigned sz)
{
#ifdef ABOOT_IGNORE_BOOT_HEADER_ADDRS
    char buf[1024];

    snprintf(buf, sizeof(buf), "kernel: 0x%08x", ABOOT_FORCE_KERNEL_ADDR);
    fastboot_info(buf);
    snprintf(buf, sizeof(buf), "kernel64: 0x%016x", ABOOT_FORCE_KERNEL64_ADDR);
    fastboot_info(buf);
    snprintf(buf, sizeof(buf), "ramdisk: 0x%08x", ABOOT_FORCE_RAMDISK_ADDR);
    fastboot_info(buf);
    snprintf(buf, sizeof(buf), "tags: 0x%08x", ABOOT_FORCE_TAGS_ADDR);
    fastboot_info(buf);
#else
    fastboot_info("from boot image");
#endif

    fastboot_okay("");
}

typedef struct {
    uint32_t image_id;
    uint32_t header_vsn_num;
    uint32_t image_src;
    uint32_t image_dest_ptr;
    uint32_t image_size;
    uint32_t code_size;
    uint32_t signature_ptr;
    uint32_t signature_size;
    uint32_t cert_chain_ptr;
    uint32_t cert_chain_size;
} qcom_bootimg_t;

static const char *qcombootimg2str(uint32_t id)
{
    switch (id) {
        case 0:
            return "none";
        case 1:
            return "oem sbl";
        case 2:
            return "amss";
        case 3:
            return "qcsbl";
        case 4:
            return "hash";
        case 5:
            return "appsbl";
        case 6:
            return "apps";
        case 7:
            return "hostdl";
        case 8:
            return "dsp1";
        case 9:
            return "fsbl";
        case 10:
            return "dbl";
        case 11:
            return "osbl";
        case 12:
            return "dsp2";
        case 13:
            return "ehostdl";
        case 14:
            return "nandprg";
        case 15:
            return "norprg";
        case 16:
            return "ramfs1";
        case 17:
            return "ramfs2";
        case 18:
            return "adsp q5";
        case 19:
            return "apps kernel";
        case 20:
            return "backup ramfs";
        case 21:
            return "sbl1";
        case 22:
            return "sbl2";
        case 23:
            return "rpm";
        case 24:
            return "sbl3";
        case 25:
            return "tz";
        case 26:
            return "ssd keys";
        case 27:
            return "gen";
        case 28:
            return "dsp3";
        case 29:
            return "acdb";
        case 30:
            return "sdi";
        case 31:
            return "mba";
        default:
            return "unknown";
    }
}

#define SBL1_CODEWORD 0x844BDCD1
#define SBL1_MAGIC    0x73D71034

typedef struct {
    uint32_t codeword;
    uint32_t magic;
    uint32_t reserved1[3];

    uint32_t image_src;
    uint32_t image_dest_ptr;
    uint32_t image_size;
    uint32_t code_size;
    uint32_t sig_ptr;
    uint32_t sig_size;
    uint32_t cert_chain_ptr;
    uint32_t cert_chain_size;
    uint32_t oem_root_cert_sel;
    uint32_t oem_num_root_certs;
    uint32_t reserved2[5];
} qcom_sbl1_header_t;

static void cmd_oem_findbootimages(const char *arg, void *data, unsigned sz)
{
    char buf[1024];
    uint32_t readsize = 0;
    readsize = MAX(readsize, sizeof(qcom_bootimg_t));
    readsize = MAX(readsize, sizeof(boot_img_hdr));
    readsize = MAX(readsize, sizeof(qcom_sbl1_header_t));
    readsize = MAX(readsize, sizeof(Elf32_Ehdr));
    readsize = MAX(readsize, sizeof(Elf64_Ehdr));
    readsize = ROUNDUP(readsize, mmc_get_device_blocksize());

    // allocate memory
    qcom_bootimg_t *bootimg = (qcom_bootimg_t *) memalign(CACHE_LINE, readsize);
    if (!bootimg) {
        fastboot_okay("error allocating memory");
        return;
    }
    struct boot_img_hdr *aimg = (struct boot_img_hdr *)bootimg;
    qcom_sbl1_header_t *sbl1img = (qcom_sbl1_header_t *)bootimg;
    Elf32_Ehdr *elf32hdr = (Elf32_Ehdr *)bootimg;
    Elf64_Ehdr *elf64hdr = (Elf64_Ehdr *)bootimg;

    unsigned i = 0;
    unsigned count = partition_get_count();
    for (i = 0; i < count; i++) {
        // get offset
        uint64_t offset = partition_get_offset(i);
        if (!offset)
            continue;

        // read
        uint64_t partsize = partition_get_size(i);
        if (partsize<readsize)
            continue;

        if (mmc_read(offset, (uint32_t *)bootimg, readsize))
            continue;


        // android
        if (!memcmp(aimg->magic, BOOT_MAGIC, BOOT_MAGIC_SIZE)) {
            snprintf(buf, sizeof(buf), "found Android image on %s", partition_get_name(i));
            fastboot_info(buf);
            snprintf(buf, sizeof(buf), "\tKernel: addr:%08x sz:%08x", aimg->kernel_addr, aimg->kernel_size);
            fastboot_info(buf);
            snprintf(buf, sizeof(buf), "\tRamdisk: addr:%08x sz:%08x", aimg->ramdisk_addr, aimg->ramdisk_size);
            fastboot_info(buf);
            snprintf(buf, sizeof(buf), "\tSecond: addr:%08x sz:%08x", aimg->second_addr, aimg->second_size);
            fastboot_info(buf);
            snprintf(buf, sizeof(buf), "\tTags Addr:%08x, DTB sz:%08x", aimg->tags_addr, aimg->dt_size);
            fastboot_info(buf);
            snprintf(buf, sizeof(buf), "\tpagesize:%u", aimg->page_size);
            fastboot_info(buf);
        }

        // QCOM SBL1
        else if (sbl1img->codeword==SBL1_CODEWORD && sbl1img->magic==SBL1_MAGIC) {
            snprintf(buf, sizeof(buf), "found QCOM SBL1 image on %s", partition_get_name(i));
            fastboot_info(buf);
            snprintf(buf, sizeof(buf), "\tImage: src:%08x dst:%08x sz:%08x", sbl1img->image_src, sbl1img->image_dest_ptr, sbl1img->image_size);
            fastboot_info(buf);
            snprintf(buf, sizeof(buf), "\tSignature: src:%08x sz:%08x", sbl1img->sig_ptr, sbl1img->sig_size);
            fastboot_info(buf);
            snprintf(buf, sizeof(buf), "\tCERT chain: src:%08x sz:%08x", sbl1img->cert_chain_ptr, sbl1img->cert_chain_size);
            fastboot_info(buf);
            snprintf(buf, sizeof(buf), "\tcode size: %08x", sbl1img->code_size);
            fastboot_info(buf);
            snprintf(buf, sizeof(buf), "\tOEM root cert: sel:%08x num:%08x", sbl1img->oem_root_cert_sel, sbl1img->oem_num_root_certs);
            fastboot_info(buf);
        }

        // ELF32
        else if (elf32hdr->e_ident[EI_CLASS] == ELFCLASS32) {
            snprintf(buf, sizeof(buf), "found ELF32 image on %s", partition_get_name(i));
            fastboot_info(buf);
            snprintf(buf, sizeof(buf), "\tEntry: 0x%08x", elf32hdr->e_entry);
            fastboot_info(buf);
        }

        // ELF64
        else if (elf64hdr->e_ident[EI_CLASS] == ELFCLASS64) {
            snprintf(buf, sizeof(buf), "found ELF64 image on %s", partition_get_name(i));
            fastboot_info(buf);
            snprintf(buf, sizeof(buf), "\tEntry: 0x%016llx", elf64hdr->e_entry);
            fastboot_info(buf);
        }

        // QCOM MBN
        else if (bootimg->image_id<=0x7FFFFFFF && bootimg->image_size>0 && partsize >= bootimg->image_size &&
                 bootimg->image_size == (bootimg->code_size + bootimg->signature_size + bootimg->cert_chain_size)) {
            snprintf(buf, sizeof(buf), "found QCOM MBN image on %s", partition_get_name(i));
            fastboot_info(buf);
            snprintf(buf, sizeof(buf), "\tID:%u(%s) version:%u", bootimg->image_id, qcombootimg2str(bootimg->image_id), bootimg->header_vsn_num);
            fastboot_info(buf);
            snprintf(buf, sizeof(buf), "\tImage: src:%08x dst:%08x sz:%08x", bootimg->image_src, bootimg->image_dest_ptr, bootimg->image_size);
            fastboot_info(buf);
            snprintf(buf, sizeof(buf), "\tSignature: src:%08x sz:%08x", bootimg->signature_ptr, bootimg->signature_size);
            fastboot_info(buf);
            snprintf(buf, sizeof(buf), "\tCERT chain: src:%08x sz:%08x", bootimg->cert_chain_ptr, bootimg->cert_chain_size);
            fastboot_info(buf);
            snprintf(buf, sizeof(buf), "\tcode size: %08x", bootimg->code_size);
            fastboot_info(buf);
        }
    }

    free(bootimg);
    fastboot_okay("");
}

static void bio_foreach_cb(void *pdata, const char *name)
{
    char buf[1024];

    bdev_t *dev = bio_open(name);
    if (!dev) return;

    snprintf(buf, sizeof(buf),
             "%s(%s) sz:%lld bsz:%zd ref:%d sub:%d",
             dev->name, dev->label, dev->size, dev->block_size, dev->ref, dev->is_subdev
            );
    fastboot_info(buf);
}

static void cmd_oem_dump_partitiontable(const char *arg, void *data, unsigned sz)
{
    char buf[1024];
    unsigned i = 0;
    extern struct partition_entry *partition_entries;

    if (!strcmp(arg, "qcom")) {
        for (i = 0; i < partition_get_count(); i++) {
            snprintf(buf, sizeof(buf),
                     "%d: %s sz:%llu (%llu-%llu) type:%u",
                     i,
                     partition_entries[i].name,
                     partition_entries[i].size,
                     partition_entries[i].first_lba,
                     partition_entries[i].last_lba,
                     partition_entries[i].dtype
                    );
            fastboot_info(buf);
        }
    }

    else {
        bio_foreach(bio_foreach_cb, NULL, true);
    }

    fastboot_okay("");
}

static void cmd_oem_memfill(const char *arg, void *data, unsigned sz)
{
    uint32_t i;
    uint32_t testbase = efidroid_hex2unsigned(arg);
    arg += 9;
    uint32_t length = efidroid_hex2unsigned(arg);
    for (i = 0; i < length; i++) {
        *(volatile uint8_t *)(testbase + i) = 0xff;
    }
    fastboot_okay("");
}

#ifdef WITH_LIB_PRAM
static void cmd_oem_lastkmsg(const char *arg, void *data, unsigned sz)
{
    char buf[MAX_RSP_SIZE];

    void *oldbuf = persistent_ram_old();
    if (oldbuf) {
        fastboot_send_string_human(oldbuf, persistent_ram_old_size());
    } else {
        snprintf(buf, sizeof(buf), "last_kmsg not found");
        fastboot_info(buf);
    }

    fastboot_okay("");
}
#endif

#if defined(WITH_LIB_ATAGPARSE) && defined(WITH_LIB_BASE64)
static void cmd_oem_dumpatags(const char *arg, void *data, unsigned sz)
{
    const void *tags = lkargs_get_tags_backup();
    size_t tags_size = lkargs_get_tags_backup_size();

    if (tags && tags_size)
        fastboot_send_buf(tags, tags_size);

    fastboot_okay("");
}
#endif

#if defined(WITH_LIB_BASE64)
static void cmd_oem_dumpmem(const char *arg, void *data, unsigned sz)
{
    uint32_t addr = efidroid_hex2unsigned(arg);
    arg += 9;
    uint32_t size = efidroid_hex2unsigned(arg);

    if (addr && size)
        fastboot_send_buf((void *)addr, size);

    fastboot_okay("");
}
#endif

#ifdef WITH_LIB_BOOT
#define IS_ARM64(ptr) (((struct kernel64_hdr *)(ptr))->magic_64 == KERNEL64_HDR_MAGIC) ? true : false

typedef void libboot_entry_func_ptr(unsigned, unsigned, unsigned);
void libboot_platform_heap_init(void *base, size_t len);
void target_uninit(void);
void platform_uninit(void);

static void boot_jump(bootimg_context_t *context)
{
    void (*entry)(unsigned, unsigned, unsigned) = (libboot_entry_func_ptr *)(PA((addr_t)context->kernel_addr));

    /* Perform target specific cleanup */
    target_uninit();

    /* Turn off splash screen if enabled */
#if DISPLAY_SPLASH_SCREEN
    target_display_shutdown();
#endif

    dprintf(INFO, "booting linux @ %p, ramdisk @ %p (%lu), tags/device tree @ %p\n",
            entry, (void *)context->ramdisk_addr, context->ramdisk_size, (void *)context->tags_addr);

    enter_critical_section();

    /* do any platform specific cleanup before kernel entry */
    platform_uninit();

    arch_disable_cache(UCACHE);

#if ARM_WITH_MMU
    arch_disable_mmu();
#endif
    bs_set_timestamp(BS_KERNEL_ENTRY);

    if (IS_ARM64(context->kernel_addr))
        // Jump to a 64bit kernel
        scm_elexec_call((paddr_t)context->kernel_addr, context->kernel_arguments[2]);
    else
        // Jump to a 32bit kernel
        entry(context->kernel_arguments[0], context->kernel_arguments[1], context->kernel_arguments[2]);
}

static void print_error_stack(void)
{
    // print errors
    uint32_t i;
    char **error_stack = libboot_error_stack_get();
    for (i=0; i<libboot_error_stack_count(); i++)
        printf("[%d] %s\n", i, error_stack[i]);
    libboot_error_stack_reset();
}

static void update_ker_tags_rdisk_addr(bootimg_context_t *context, bool is_arm64)
{
    /* overwrite the destination of specified for the project */
#ifdef ABOOT_IGNORE_BOOT_HEADER_ADDRS
    if (is_arm64)
        context->kernel_addr = ABOOT_FORCE_KERNEL64_ADDR;
    else
        context->kernel_addr = ABOOT_FORCE_KERNEL_ADDR;
    context->ramdisk_addr = ABOOT_FORCE_RAMDISK_ADDR;
    context->tags_addr = ABOOT_FORCE_TAGS_ADDR;
#endif
}

static void *libboot_add_custom_atags(void *tags)
{
    return lkargs_atag_insert_unknown(tags);
}

static void libboot_patch_fdt(void *fdt)
{
    lkargs_insert_chosen(fdt);
}

static void cmd_boot(const char *arg, void *data, unsigned sz)
{
    // init
    libboot_platform_heap_init(data + sz, target_get_max_flash_size() - sz);
    libboot_init();

    // setup context
    bootimg_context_t context;
    libboot_init_context(&context);
    context.add_custom_atags = libboot_add_custom_atags;
    context.patch_fdt = libboot_patch_fdt;

    // identify type
    int rc = libboot_identify_memory(data, sz, &context);
    if (!rc) {
        // load image
        rc = libboot_load(&context);
        if (!rc) {

            // update loading addresses
            update_ker_tags_rdisk_addr(&context, IS_ARM64(context.kernel_data));

            // add to cmdline
            libboot_cmdline_addall(&context.cmdline, lkargs_get_command_line(0), 1);

            // prepare for boot
            rc = libboot_prepare(&context);
            if (!rc) {
                // just in case one got ignored
                print_error_stack();

                // disable fastboot
                fastboot_okay("");
                fastboot_stop();

                // BOOT :)
                boot_jump(&context);
                dprintf(CRITICAL, "BOOT RETURNED\n");
            }
        }
    }

    // print errors
    print_error_stack();

    // cleanup
    libboot_free_context(&context);
    libboot_uninit();

    fastboot_fail("can't boot");
}
#endif

void aboot_fastboot_register_commands_ex(void)
{
    int i;

    struct fastboot_cmd_desc cmd_list[] = {
        /* By default the enabled list is empty. */
        {"", NULL},
        /* move commands enclosed within the below ifndef to here
         * if they need to be enabled in user build.
         */
#ifndef DISABLE_FASTBOOT_CMDS
        {"oem reboot-recovery", cmd_oem_reboot_recovery},
        {"oem reboot-download", cmd_oem_reboot_download},
        {"oem poweroff", cmd_poweroff},
#if WITH_DEBUG_LOG_BUF
        {"oem lk_log", cmd_oem_lk_log},
#endif
        {"oem ram-ptable", cmd_oem_ram_ptable},
        {"oem fbconfig", cmd_oem_fbconfig},
        {"oem bootaddresses", cmd_oem_bootaddresses},
        {"oem findbootimages", cmd_oem_findbootimages},
        {"oem dump-partitiontable", cmd_oem_dump_partitiontable},
#ifdef WITH_LIB_PRAM
        {"oem last_kmsg", cmd_oem_lastkmsg},
#endif
        {"oem memfill", cmd_oem_memfill},
#if defined(WITH_LIB_ATAGPARSE) && defined(WITH_LIB_BASE64)
        {"oem dump-atags", cmd_oem_dumpatags},
#endif
#if defined(WITH_LIB_BASE64)
        {"oem dump-mem", cmd_oem_dumpmem},
#endif

        // these work because fastboot checks the last commands first
#ifdef WITH_LIB_BOOT
        {"boot", cmd_boot},
#endif
#endif
    };

    int fastboot_cmds_count = sizeof(cmd_list)/sizeof(cmd_list[0]);
    for (i = 1; i < fastboot_cmds_count; i++)
        fastboot_register(cmd_list[i].name,cmd_list[i].cb);
}

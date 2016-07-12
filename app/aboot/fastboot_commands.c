#include <stdlib.h>
#include <string.h>

#include <platform.h>
#include <target.h>
#include <reboot.h>
#include <smem.h>
#include <mmc.h>
#include <partition_parser.h>
#include <platform/timer.h>
#include <dev/fbcon.h>
#include <atagparse.h>

#if WITH_LIB_BIO
#include <lib/bio.h>
#endif

#include "fastboot.h"
#include "bootimg.h"

/* fastboot command function pointer */
typedef void (*fastboot_cmd_fn) (const char *, void *, unsigned);

struct fastboot_cmd_desc {
	const char * name;
	fastboot_cmd_fn cb;
};

unsigned hex2unsigned(const char *x);

void cmd_oem_reboot_recovery(const char *arg, void *data, unsigned sz)
{
	fastboot_okay("");
	reboot_device(RECOVERY_MODE);
}

void cmd_oem_reboot_download(const char *arg, void *data, unsigned sz)
{
	fastboot_okay("");
	if (set_download_mode(EMERGENCY_DLOAD))
	{
		dprintf(CRITICAL,"dload mode not supported by target\n");
	}
	else
	{
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

static char* get_human_size(double size, char *buf) {
	int i = 0;
	const char* units[] = {"B", "kB", "MB", "GB", "TB", "PB", "EB", "ZB", "YB"};
	while (size>=1024) {
		size/=1024;
		i++;
	}
	sprintf(buf, "%.4u%.2s", (uint32_t)size, units[i]);
	return buf;
}

static void cmd_oem_ram_ptable(const char *arg, void *data, unsigned sz)
{
	struct smem_ram_ptable ram_ptable;
	unsigned int i;
	char buf[MAX_RSP_SIZE];

	// Make sure RAM partition table is initialized
	if(!smem_ram_ptable_init(&ram_ptable)) {
		fastboot_fail("error reading RAM ptable");
		return;
	}

	// print header
	fastboot_info("ID\tAddress              \t  Size\tAttr\tCat\tDomain\tType\tParts");

	// print table
	for(i = 0; i<ram_ptable.len; i++) {
		char sizebuf[1024];
		snprintf(buf, sizeof(buf), "%u:\t0x%08x-0x%08x\t%s\t%s\t%s\t%s\t%s\t%u", i,
				ram_ptable.parts[i].start, ram_ptable.parts[i].start+ram_ptable.parts[i].size,
				get_human_size(ram_ptable.parts[i].size, sizebuf), smem_attr2str(ram_ptable.parts[i].attr),
				smem_category2str(ram_ptable.parts[i].category), smem_domain2str(ram_ptable.parts[i].domain),
				smem_type2str(ram_ptable.parts[i].type), ram_ptable.parts[i].num_partitions);
		fastboot_info(buf);
	}

	fastboot_okay("");
}

static void cmd_oem_fbconfig(const char *arg, void *data, unsigned sz)
{
	struct fbcon_config* config = fbcon_display();
	char buf[1024];

	fastboot_info("fbcon_config:");

	snprintf(buf, sizeof(buf), "\tbase: %p (end: %p)", (void*)config->base, config->base + (config->width * config->height * config->bpp/3));
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

static const char* qcombootimg2str(uint32_t id) {
	switch(id) {
		case 0: return "none";
		case 1: return "oem sbl";
		case 2: return "amss";
		case 3: return "qcsbl";
		case 4: return "hash";
		case 5: return "appsbl";
		case 6: return "apps";
		case 7: return "hostdl";
		case 8: return "dsp1";
		case 9: return "fsbl";
		case 10: return "dbl";
		case 11: return "osbl";
		case 12: return "dsp2";
		case 13: return "ehostdl";
		case 14: return "nandprg";
		case 15: return "norprg";
		case 16: return "ramfs1";
		case 17: return "ramfs2";
		case 18: return "adsp q5";
		case 19: return "apps kernel";
		case 20: return "backup ramfs";
		case 21: return "sbl1";
		case 22: return "sbl2";
		case 23: return "rpm";
		case 24: return "sbl3";
		case 25: return "tz";
		case 26: return "ssd keys";
		case 27: return "gen";
		case 28: return "dsp3";
		case 29: return "acdb";
		case 30: return "sdi";
		case 31: return "mba";
		default: return "unknown";
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
	readsize = ROUNDUP(readsize, mmc_get_device_blocksize());

	// allocate memory
	qcom_bootimg_t* bootimg = (qcom_bootimg_t*) memalign(CACHE_LINE, readsize);
	if(!bootimg) {
		fastboot_okay("error allocating memory");
		return;
	}
	struct boot_img_hdr* aimg = (struct boot_img_hdr*)bootimg;
	qcom_sbl1_header_t* sbl1img = (qcom_sbl1_header_t*)bootimg;

	unsigned i = 0;
	unsigned count = partition_get_count();
	for (i = 0; i < count; i++) {
		// get offset
		uint64_t offset = partition_get_offset(i);
		if(!offset)
			continue;

		// read
		uint64_t partsize = partition_get_size(i);
		if(partsize<readsize)
			continue;

		if (mmc_read(offset, (uint32_t*)bootimg, readsize))
			continue;


		// android
		if(!memcmp(aimg->magic, BOOT_MAGIC, BOOT_MAGIC_SIZE)) {
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
		else if(sbl1img->codeword==SBL1_CODEWORD && sbl1img->magic==SBL1_MAGIC) {
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

		// QCOM MBN
		else if(bootimg->image_id<=0x7FFFFFFF && bootimg->image_size>0 && partsize >= bootimg->image_size &&
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

static void bio_foreach_cb(void* pdata, const char* name) {
	char buf[1024];

	bdev_t* dev = bio_open(name);
	if(!dev) return;

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

	if(!strcmp(arg, "qcom")) {
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

#define PERSISTENT_RAM_SIG (0x43474244) /* DBGC */
struct persistent_ram_buffer {
	uint32_t    sig;
	int    start;
	int    size;
	uint8_t     data[0];
};

static void cmd_oem_memfill(const char *arg, void *data, unsigned sz) {
	uint32_t i;
	uint32_t testbase = hex2unsigned(arg);
	arg += 9;
	uint32_t length = hex2unsigned(arg);
	for (i = 0; i < length; i++) {
		*(volatile uint8_t*)(testbase + i) = 0xff;
	}
	fastboot_okay("");
}

static void cmd_oem_lastkmsg(const char *arg, void *data, unsigned sz) {
	char buf[MAX_RSP_SIZE];
	uint32_t addr;

#ifdef PERSISTENT_RAM_ADDR
	addr = PERSISTENT_RAM_ADDR;
#else
	addr = hex2unsigned(arg);
#endif

	struct persistent_ram_buffer* rambuf = (void*)addr;
	if(rambuf->sig==PERSISTENT_RAM_SIG) {
		snprintf(buf, sizeof(buf), "found last_kmsg at %p", rambuf);
		fastboot_info(buf);


		uint8_t* data = &rambuf->data[0];
		fastboot_send_string_human(data, rambuf->size);
	}
	else {
		snprintf(buf, sizeof(buf), "last_kmsg not found at %p", rambuf);
		fastboot_info(buf);
	}

	fastboot_okay("");
}

static void cmd_oem_dumpatags(const char *arg, void *data, unsigned sz) {
	void* tags = lkargs_get_tags_backup();
	size_t tags_size = lkargs_get_tags_backup_size();

	if(tags && tags_size)
		fastboot_send_buf(tags, tags_size);

	fastboot_okay("");
}

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
						{"oem last_kmsg", cmd_oem_lastkmsg},
						{"oem memfill", cmd_oem_memfill},
						{"oem dump-atags", cmd_oem_dumpatags},
#endif
						};

	int fastboot_cmds_count = sizeof(cmd_list)/sizeof(cmd_list[0]);
	for (i = 1; i < fastboot_cmds_count; i++)
		fastboot_register(cmd_list[i].name,cmd_list[i].cb);
}

LOCAL_DIR := $(GET_LOCAL_DIR)

LIBBOOT_DIR := $(EFIDROID_TOP)/modules/libboot
INCLUDES += -I$(LIBBOOT_DIR)/include
INCLUDES += -I$(LIBBOOT_DIR)/include_private
INCLUDES += -I$(LOCAL_DIR)/include

OBJS += \
	$(LOCAL_DIR)/platform.o \
	$(LOCAL_DIR)/heap.o \
	$(LIBBOOT_DIR)/boot.o \
	$(LIBBOOT_DIR)/cmdline.o \
	$(LIBBOOT_DIR)/qcdt.o \
	$(LIBBOOT_DIR)/cksum/crc32.o \
	$(LIBBOOT_DIR)/loaders/android.o \
	$(LIBBOOT_DIR)/loaders/efi.o \
	$(LIBBOOT_DIR)/loaders/elf.o \
	$(LIBBOOT_DIR)/loaders/gzip.o \
	$(LIBBOOT_DIR)/loaders/qcmbn.o \
	$(LIBBOOT_DIR)/loaders/zimage.o \
	$(LIBBOOT_DIR)/tagloaders/atags.o \
	$(LIBBOOT_DIR)/tagloaders/fdt.o \
	$(LIBBOOT_DIR)/tagloaders/qcdt.o

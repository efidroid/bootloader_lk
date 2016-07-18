LOCAL_DIR := $(GET_LOCAL_DIR)

INCLUDES += -I$(LK_TOP_DIR)/app/aboot

MODULES += \
    lib/bio \
    lib/partition

OBJS += \
	$(LOCAL_DIR)/fastboot_commands.o

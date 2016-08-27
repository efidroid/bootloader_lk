LOCAL_DIR := $(GET_LOCAL_DIR)

SHIMOBJS += \
    $(LOCAL_DIR)/target_display.o \
    $(LOCAL_DIR)/oem_panel.o

OBJS += \
    $(LOCAL_DIR)/dtb_panel_reader.o

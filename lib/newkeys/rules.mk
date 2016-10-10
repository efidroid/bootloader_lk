LOCAL_DIR := $(GET_LOCAL_DIR)

INCLUDES := -I$(LOCAL_DIR)/include $(INCLUDES)

OBJS += \
	$(LOCAL_DIR)/newkeys.o

LOCAL_DIR := $(GET_LOCAL_DIR)

INCLUDES += -I$(LOCAL_DIR)/include

OBJS += \
	$(LOCAL_DIR)/hex2unsigned.o

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULES += \
	lib/openssl

ifndef WITH_KERNEL_UEFIAPI
OBJS += \
	$(LOCAL_DIR)/app.o
endif

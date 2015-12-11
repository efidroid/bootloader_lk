LOCAL_DIR := $(GET_LOCAL_DIR)

MODULES += \
	lib/bio \
	lib/partition

INCLUDES += \
	-I$(EDK2_API_INC) \
	-I$(LOCAL_DIR)/include \
	-I$(LK_TOP_DIR)/app/aboot

DEFINES += EDK2_BASE=$(EDK2_BASE)
DEFINES += EDK2_SIZE=$(EDK2_SIZE)

OBJS += \
	$(LOCAL_DIR)/uefiapi.o \
	$(LOCAL_DIR)/main.o \
	$(LOCAL_DIR)/event.o \
	$(LOCAL_DIR)/mutex.o

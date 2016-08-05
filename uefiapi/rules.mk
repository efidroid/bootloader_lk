LOCAL_DIR := $(GET_LOCAL_DIR)

MODULES += \
	lib/bio \
	lib/partition

INCLUDES += \
	-I$(EDK2_API_INC) \
	-I$(LOCAL_DIR)/include \
	-I$(LK_TOP_DIR)/app/aboot

OBJS += \
	$(LOCAL_DIR)/uefiapi.o \
	$(LOCAL_DIR)/main.o \
	$(LOCAL_DIR)/event.o \
	$(LOCAL_DIR)/mutex.o \
	$(LOCAL_DIR)/target/$(TARGET).o

SHIMOBJS += \
	$(LOCAL_DIR)/shim.o

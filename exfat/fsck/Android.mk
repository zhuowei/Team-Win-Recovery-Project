LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_MODULE := fsckexfat
LOCAL_MODULE_STEM := fsck.exfat
LOCAL_MODULE_CLASS := RECOVERY_EXECUTABLES
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/sbin
LOCAL_CFLAGS = -D_FILE_OFFSET_BITS=64
LOCAL_SRC_FILES =  main.c
LOCAL_C_INCLUDES += $(LOCAL_PATH) \
					$(commands_recovery_local_path)/exfat/libexfat \
					$(commands_recovery_local_path)/fuse/include
LOCAL_SHARED_LIBRARIES := libexfat_twrp

include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)

LOCAL_MODULE := exfat-fuse-static
LOCAL_MODULE_TAGS := eng
LOCAL_CFLAGS = -D_FILE_OFFSET_BITS=64
LOCAL_SRC_FILES = main.c
LOCAL_C_INCLUDES += $(LOCAL_PATH) \
					$(commands_recovery_local_path)/exfat/libexfat \
					$(commands_recovery_local_path)/fuse/include
LOCAL_STATIC_LIBRARIES += libfusetwrp libz libc libexfat
LOCAL_FORCE_STATIC_EXECUTABLE := true

include $(BUILD_EXECUTABLE)

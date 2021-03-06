LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES:= \
    qcamera_test.cpp \

LOCAL_SHARED_LIBRARIES:= \
    libdl \
    libui \
    libutils \
    libcutils \
    libbinder \
    libmedia \
    libui \
    libgui \
    libcamera_client \
    libskia \
    libstagefright \
    libstagefright_foundation \

ifneq (1,$(filter 1,$(shell echo "$$(( $(PLATFORM_SDK_VERSION) >= 18 ))" )))

LOCAL_SHARED_LIBRARIES += \
    libmedia_native
ifneq ($(TARGET_KERNEL_VERSION),$(filter $(TARGET_KERNEL_VERSION),3.18 4.4 4.9))
LOCAL_SHARED_LIBRARIES += libion
endif
LOCAL_32_BIT_ONLY := $(BOARD_QTI_CAMERA_32BIT_ONLY)
LOCAL_CFLAGS += -DUSE_JB_MR1

endif

ifneq ($(TARGET_KERNEL_VERSION),$(filter $(TARGET_KERNEL_VERSION),3.18 4.4 4.9))
LOCAL_C_INCLUDES += \
        system/core/libion/kernel-headers \
        system/core/libion/include
endif

LOCAL_HAL_TOP := $(LOCAL_PATH)/../../..

LOCAL_C_INCLUDES += \
    external/skia/include/core \
    external/skia/include/images \
    $(TARGET_OUT_HEADERS)/qcom/display \
    frameworks/av/include/media/stagefright \
    frameworks/native/include/media/openmax \
    $(LOCAL_HAL_TOP)/QCamera2/stack/common \
    $(LOCAL_HAL_TOP)/QCamera2/stack/mm-camera-interface/inc \
    $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include

LOCAL_ADDITIONAL_DEPENDENCIES := $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr

LOCAL_MODULE:= camera_test
LOCAL_VENDOR_MODULE := true
include $(SDCLANG_COMMON_DEFS)
LOCAL_MODULE_TAGS:= tests

LOCAL_CFLAGS += -Wall -Wextra -Werror -Wno-unused-parameter
LOCAL_CFLAGS += -O0

ifeq (1,$(filter 1,$(shell echo "$$(( $(PLATFORM_SDK_VERSION) >= 20 ))" )))

LOCAL_CFLAGS += -DUSE_SDK_20_OR_HIGHER

ifeq ($(TARGET_USES_AOSP),true)
LOCAL_CFLAGS += -DVANILLA_HAL
endif

endif

#include $(BUILD_EXECUTABLE)

ifneq ($(filter msm8960,$(TARGET_BOARD_PLATFORM)),)
ifeq ($(TARGET_QCOM_AUDIO_VARIANT),caf)
AUDIO_ROOT := $(call my-dir)
include $(call all-subdir-makefiles)
endif
endif
ifeq ($(call is-board-platform,msm7630_surf),true)
    include $(AUDIO_HW_ROOT)/msm7630/Android.mk
endif

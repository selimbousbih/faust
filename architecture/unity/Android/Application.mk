APP_OPTIM := release
APP_STL := c++_static
LOCAL_PATH := $(call my-dir)
APP_CPPFLAGS += -fexceptions
APP_CPPFLAGS += -std=c++11
APP_PLATFORM := android-21
APP_BUILD_SCRIPT := $(LOCAL_PATH)/Android.mk

APP_ABI := armeabi-v7a x86

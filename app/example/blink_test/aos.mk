NAME := blink_test

$(NAME)_MBINS_TYPE := app
$(NAME)_VERSION := 1.0.1
$(NAME)_SUMMARY := blink
$(NAME)_SOURCES := blink.c

$(NAME)_COMPONENTS += osal_aos

GLOBAL_DEFINES += AOS_NO_WIFI

$(NAME)_INCLUDES += ./

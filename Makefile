#
# This is a project Makefile. It is assumed the directory this Makefile resides in is a
# project subdirectory.
#

PROJECT_NAME := blepod

EXTRA_CFLAGS += --save-temps

include $(IDF_PATH)/make/project.mk


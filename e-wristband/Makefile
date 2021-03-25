#
# This is a project Makefile. It is assumed the directory this Makefile resides in is a
# project subdirectory.
#

PROJECT_NAME := onoff_client

EXTRA_COMPONENT_DIRS := $(IDF_PATH)/examples/bluetooth/esp_ble_mesh/common_components/button \
                        $(IDF_PATH)/examples/bluetooth/esp_ble_mesh/common_components/example_init \
                        $(IDF_PATH)/examples/bluetooth/esp_ble_mesh/common_components/example_nvs

include $(IDF_PATH)/make/project.mk

SPIFFS_IMAGE_FLASH_IN_PROJECT := 1
$(eval $(call spiffs_create_partition_image,storage,font))

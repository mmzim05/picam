################################################################################
#
# picam-app
#
################################################################################

PICAM_APP_VERSION = 1.0
PICAM_APP_SITE    = $(BR2_EXTERNAL_PICAM_PATH)/../src
PICAM_APP_SITE_METHOD = local

PICAM_APP_DEPENDENCIES = libcamera libjpeg-turbo

PICAM_APP_CONF_OPTS = \
	-DCMAKE_BUILD_TYPE=Release

define PICAM_APP_INSTALL_INIT_SYSV
	$(INSTALL) -D -m 0755 $(BR2_EXTERNAL_PICAM_PATH)/board/picam/S99picam \
		$(TARGET_DIR)/etc/init.d/S99picam
endef

$(eval $(cmake-package))

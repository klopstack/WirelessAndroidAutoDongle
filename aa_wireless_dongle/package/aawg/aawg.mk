AAWG_VERSION = 1.0
AAWG_SITE = $(BR2_EXTERNAL_AA_WIRELESS_DONGLE_PATH)/package/aawg/src
AAWG_SITE_METHOD = local
AAWG_DEPENDENCIES = dbus-cxx-custom protobuf mosquitto

AAWG_GIT_SHA := $(shell cd "$(AAWG_SITE)" >/dev/null 2>&1 && git rev-parse --short HEAD 2>/dev/null || echo unknown)
AAWG_BUILD_TIME_UTC := $(shell date -u +"%Y-%m-%dT%H:%M:%SZ" 2>/dev/null || echo unknown)

define AAWG_BUILD_CMDS
    $(MAKE) $(TARGET_CONFIGURE_OPTS) PROTOC=$(HOST_DIR)/bin/protoc \
        CXXFLAGS="$(TARGET_CXXFLAGS) -DAAWG_VERSION_STR=\\\"$(AAWG_VERSION)\\\" -DAAWG_GIT_SHA=\\\"$(AAWG_GIT_SHA)\\\" -DAAWG_BUILD_TIME_UTC=\\\"$(AAWG_BUILD_TIME_UTC)\\\"" \
        -C $(@D)
endef

define AAWG_INSTALL_TARGET_CMDS
    $(INSTALL) -D -m 0755 $(@D)/aawgd  $(TARGET_DIR)/usr/bin
    $(INSTALL) -D -m 0755 $(@D)/aawgd-mqtt  $(TARGET_DIR)/usr/bin
endef

$(eval $(generic-package))

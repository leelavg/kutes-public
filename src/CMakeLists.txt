# to properly link nappgui
include(${NAPPGUI_ROOT_PATH}/prj/NAppCompilers.cmake)
# to set NAP_TARGET_SRC_EXTENSION
include(${NAPPGUI_ROOT_PATH}/prj/NAppTarget.cmake)
# to correctly detect platform
nap_config_compiler()
nap_desktop_app(kutes "osapp;yyjson" NRC_NONE)

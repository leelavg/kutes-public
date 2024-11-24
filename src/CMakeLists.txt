include(${NAPPGUI_ROOT_PATH}/prj/NAppCompilers.cmake) # to properly link nappgui
include(${NAPPGUI_ROOT_PATH}/prj/NAppTarget.cmake) # to set NAP_TARGET_SRC_EXTENSION
nap_desktop_app(kutes "osapp;yyjson" NRC_NONE)

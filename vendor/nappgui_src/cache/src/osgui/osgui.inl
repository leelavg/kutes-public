/*
 * NAppGUI Cross-platform C SDK
 * 2015-2025 Francisco Garcia Collado
 * MIT Licence
 * https://nappgui.com/en/legal/license.html
 *
 * File: osgui.inl
 *
 */

/* Operating system native gui */

#include "osgui.ixx"

__EXTERN_C

void _osgui_start_imp(void);

void _osgui_finish_imp(void);

Font *_osgui_create_default_font(void);

gui_size_t _osgui_size_font(const real32_t font_size);

vkey_t _osgui_vkey_from_text(const char_t *text);

void _osgui_select_text(const int32_t st, const int32_t ed, int32_t *platform_st, int32_t *platform_ed);

void _osgui_attach_menubar(OSWindow *window, OSMenu *menu);

void _osgui_detach_menubar(OSWindow *window, OSMenu *menu);

void _osgui_change_menubar(OSWindow *window, OSMenu *previous_menu, OSMenu *new_menu);

void _osgui_message_loop_imp(void);

bool_t _osgui_is_pre_initialized_imp(void);

void _osgui_pre_initialize_imp(void);

__END_C

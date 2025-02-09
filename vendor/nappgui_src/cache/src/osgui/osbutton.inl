/*
 * NAppGUI Cross-platform C SDK
 * 2015-2024 Francisco Garcia Collado
 * MIT Licence
 * https://nappgui.com/en/legal/license.html
 *
 * File: osbutton.inl
 *
 */

/* Operating System native button */

#include "osgui.ixx"

__EXTERN_C

bool_t _osbutton_text_allowed(const uint32_t flags);

bool_t _osbutton_image_allowed(const uint32_t flags);

void _osbutton_set_default(OSButton *button, const bool_t is_default);

void _osbutton_detach_and_destroy(OSButton **button, OSPanel *panel);

__END_C

/*
 * NAppGUI Cross-platform C SDK
 * 2015-2024 Francisco Garcia Collado
 * MIT Licence
 * https://nappgui.com/en/legal/license.html
 *
 * File: osupdown.m
 *
 */

/* Operating System native updown */

#include "osupdown.h"
#include "osupdown_osx.inl"
#include "oscontrol_osx.inl"
#include "osgui_osx.inl"
#include "ospanel_osx.inl"
#include "oswindow_osx.inl"
#include <core/event.h>
#include <core/heap.h>
#include <sewer/cassert.h>

#if !defined(__MACOS__)
#error This file is only for OSX
#endif

@interface OSXUpDown : NSStepper
{
  @public
    double value;
    Listener *OnClick;
}
@end

/*---------------------------------------------------------------------------*/

@implementation OSXUpDown

/*---------------------------------------------------------------------------*/

- (IBAction)onClickUpDown:(id)sender
{
    cassert_no_null(sender);
    cassert(sender == self);
    if ([self isEnabled] == YES && self->OnClick != NULL)
    {
        EvButton params;
        double lvalue = [self doubleValue];
        params.state = ekGUI_ON;
        if (lvalue > self->value)
            params.index = 0;
        else
            params.index = 1;
        params.text = "";
        listener_event(self->OnClick, ekGUI_EVENT_UPDOWN, cast(self, OSUpDown), &params, NULL, OSUpDown, EvButton, void);
        self->value = lvalue;
    }
}

/*---------------------------------------------------------------------------*/

- (void)mouseDown:(NSEvent *)theEvent
{
    if (_oswindow_mouse_down(cast(self, OSControl)) == TRUE)
    {
        [super mouseDown:theEvent];
    }

    _oswindow_restore_focus([self window]);
}

@end

/*---------------------------------------------------------------------------*/

OSUpDown *osupdown_create(const uint32_t flags)
{
    OSXUpDown *updown = [[OSXUpDown alloc] initWithFrame:NSMakeRect(0.f, 0.f, 16.f, 16.f)];
    unref(flags);
    heap_auditor_add("OSXUpDown");
    _oscontrol_init(updown);
    [updown setTarget:updown];
    [updown setAction:@selector(onClickUpDown:)];
    [updown setAutorepeat:YES];
    [updown setDoubleValue:0.];
    [updown setMaxValue:1e20];
    [updown setMinValue:-1e20];
    [updown setIncrement:1.];
    _oscontrol_cell_set_control_size([updown cell], ekGUI_SIZE_REGULAR);
    updown->OnClick = NULL;
    updown->value = 0.;
    return cast(updown, OSUpDown);
}

/*---------------------------------------------------------------------------*/

void osupdown_destroy(OSUpDown **updown)
{
    OSXUpDown *lupdown = nil;
    cassert_no_null(updown);
    lupdown = *dcast(updown, OSXUpDown);
    cassert_no_null(lupdown);
    listener_destroy(&lupdown->OnClick);
    [lupdown release];
    heap_auditor_delete("OSXUpDown");
    *updown = NULL;
}

/*---------------------------------------------------------------------------*/

void osupdown_OnClick(OSUpDown *updown, Listener *listener)
{
    cassert_no_null(updown);
    cassert_no_null(listener);
    listener_update(&cast(updown, OSXUpDown)->OnClick, listener);
}

/*---------------------------------------------------------------------------*/

void osupdown_tooltip(OSUpDown *updown, const char_t *text)
{
    cassert_no_null(updown);
    _oscontrol_tooltip_set(cast(updown, OSXUpDown), text);
}

/*---------------------------------------------------------------------------*/

void osupdown_attach(OSUpDown *updown, OSPanel *panel)
{
    _ospanel_attach_control(panel, cast(updown, NSView));
}

/*---------------------------------------------------------------------------*/

void osupdown_detach(OSUpDown *updown, OSPanel *panel)
{
    _ospanel_detach_control(panel, cast(updown, NSView));
}

/*---------------------------------------------------------------------------*/

void osupdown_visible(OSUpDown *updown, const bool_t visible)
{
    _oscontrol_set_visible(cast(updown, NSView), visible);
}

/*---------------------------------------------------------------------------*/

void osupdown_enabled(OSUpDown *updown, const bool_t enabled)
{
    _oscontrol_set_enabled(cast(updown, NSControl), enabled);
}

/*---------------------------------------------------------------------------*/

void osupdown_size(const OSUpDown *updown, real32_t *width, real32_t *height)
{
    _oscontrol_get_size(cast(updown, NSView), width, height);
}

/*---------------------------------------------------------------------------*/

void osupdown_origin(const OSUpDown *updown, real32_t *x, real32_t *y)
{
    _oscontrol_get_origin(cast(updown, NSView), x, y);
}

/*---------------------------------------------------------------------------*/

void osupdown_frame(OSUpDown *updown, const real32_t x, const real32_t y, const real32_t width, const real32_t height)
{
    _oscontrol_set_frame(cast(updown, NSView), x, y, width, height);
}

/*---------------------------------------------------------------------------*/

BOOL _osupdown_is(NSView *view)
{
    return [view isKindOfClass:[OSXUpDown class]];
}

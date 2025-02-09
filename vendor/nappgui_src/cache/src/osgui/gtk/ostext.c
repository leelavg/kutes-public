/*
 * NAppGUI Cross-platform C SDK
 * 2015-2024 Francisco Garcia Collado
 * MIT Licence
 * https://nappgui.com/en/legal/license.html
 *
 * File: ostext.c
 *
 */

/* Operating System native text view */

#include "ostext.h"
#include "ostext.inl"
#include "osglobals_gtk.inl"
#include "oscontrol_gtk.inl"
#include "ospanel_gtk.inl"
#include "ossplit_gtk.inl"
#include "ostext_gtk.inl"
#include "oswindow_gtk.inl"
#include <draw2d/color.h>
#include <draw2d/font.h>
#include <core/event.h>
#include <core/heap.h>
#include <core/strings.h>
#include <sewer/cassert.h>

#if !defined(__GTK3__)
#error This file is only for GTK Toolkit
#endif

struct _ostext_t
{
    OSControl control;
    char_t ffamily[64];
    real32_t fsize;
    uint32_t fstyle;
    color_t color;
    color_t bgcolor;
    align_t align;
    gint lspacing_px;
    gint bfspace_px;
    gint afspace_px;
    int32_t select_start;
    int32_t select_end;
    bool_t show_select;
    GtkTextTag *tag_attribs;
    GtkTextTag *def_attribs;
    bool_t global_attribs;
    bool_t launch_event;
    GtkWidget *tview;
    GtkTextBuffer *buffer;
    gchar *text_cache;
    GtkCssProvider *css_font;
    GtkCssProvider *css_color;
    GtkCssProvider *css_bgcolor;
    GtkCssProvider *css_pgcolor;
    GtkCssProvider *css_bdcolor;
    OSControl *capture;
    Listener *OnFilter;
    Listener *OnFocus;
};

static real32_t i_PANGO_TO_PIXELS = -1;

/*---------------------------------------------------------------------------*/

static real32_t i_device_to_pixels(void)
{
    if (i_PANGO_TO_PIXELS < 0)
    {
        /* This object is owned by Pango and must not be freed */
        PangoFontMap *fontmap = pango_cairo_font_map_get_default();
        real32_t dpi = (real32_t)pango_cairo_font_map_get_resolution(cast(fontmap, PangoCairoFontMap));
        i_PANGO_TO_PIXELS = (dpi / 72.f) / PANGO_SCALE;
    }

    return i_PANGO_TO_PIXELS;
}

/*---------------------------------------------------------------------------*/

static gboolean i_OnPressed(GtkWidget *widget, GdkEventButton *event, OSText *view)
{
    unref(widget);
    if (_oswindow_mouse_down(cast(view, OSControl)) == TRUE)
    {
        if (view->capture != NULL)
        {
            if (view->capture->type == ekGUI_TYPE_SPLITVIEW)
            {
                _ossplit_OnPress(cast(view->capture, OSSplit), event);
            }

            return TRUE;
        }
        /*
        The handler will be called before the default handler of the signal.
        This is the default behaviour
        */
        else
        {
            return FALSE;
        }
    }

    return TRUE;
}

/*---------------------------------------------------------------------------*/

static void i_OnFilter(OSText *view, GtkTextIter *ed_iter, const char_t *text, const int32_t len)
{
    cassert_no_null(view);
    cassert_no_null(ed_iter);
    cassert_no_null(text);

    if (view->launch_event == TRUE && gtk_widget_is_sensitive(view->control.widget) && view->OnFilter != NULL)
    {
        EvText params;
        EvTextFilter result;
        params.text = text;
        params.cpos = (uint32_t)gtk_text_iter_get_offset(ed_iter);
        params.len = len;
        result.apply = FALSE;
        result.text[0] = '\0';
        result.cpos = UINT32_MAX;
        listener_event(view->OnFilter, ekGUI_EVENT_TXTFILTER, view, &params, &result, OSText, EvText, EvTextFilter);

        if (result.apply == TRUE)
        {
            bool_t prev = view->launch_event;
            GtkTextIter st_iter;
            view->launch_event = FALSE;
            gtk_text_buffer_get_start_iter(view->buffer, &st_iter);
            gtk_text_iter_forward_chars(&st_iter, params.cpos - len);
            gtk_text_buffer_delete(view->buffer, &st_iter, ed_iter);
            gtk_text_buffer_insert(view->buffer, ed_iter, result.text, -1);
            view->launch_event = prev;
        }
    }
}

/*---------------------------------------------------------------------------*/

static void i_OnBufferInsert(GtkTextBuffer *buffer, GtkTextIter *location, gchar *text, gint len, OSText *view)
{
    cassert_no_null(view);
    cassert_unref(view->buffer == buffer, buffer);
    i_OnFilter(view, location, cast_const(text, char_t), (int32_t)len);

    /* Set the default attribs to new writted text */
    if (view->def_attribs != NULL)
    {
        GtkTextIter start, end;
        start = *location;
        gtk_text_iter_backward_chars(&start, len);
        end = *location;

        /* Only apply defaults if text is writted at beginning or end.
         * In between, will be applied the current format */
        if (gtk_text_iter_is_start(&start) == TRUE || gtk_text_iter_is_end(&end) == TRUE)
            gtk_text_buffer_apply_tag(view->buffer, view->def_attribs, &start, &end);
    }
}

/*---------------------------------------------------------------------------*/

static void i_OnBufferDelete(GtkTextBuffer *buffer, GtkTextIter *start, GtkTextIter *end, OSText *view)
{
    unref(buffer);
    unref(start);
    unref(end);
    unref(view);
}

/*---------------------------------------------------------------------------*/

static void i_set_wrap_mode(GtkWidget *tview, const bool_t wrap)
{
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(tview), wrap == TRUE ? GTK_WRAP_WORD_CHAR : GTK_WRAP_NONE);
}

/*---------------------------------------------------------------------------*/

OSText *ostext_create(const uint32_t flags)
{
    OSText *view = heap_new0(OSText);
    GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
    GtkWidget *top = NULL;
    GtkWidget *focus = NULL;
    unref(flags);
    view->tview = gtk_text_view_new();
    view->select_start = INT32_MAX;
    view->select_end = INT32_MAX;
    view->show_select = FALSE;

    /* A parent widget can "capture" the mouse */
    {
        GtkWidget *vscroll = gtk_scrolled_window_get_vscrollbar(GTK_SCROLLED_WINDOW(scrolled));
        GtkWidget *hscroll = gtk_scrolled_window_get_hscrollbar(GTK_SCROLLED_WINDOW(scrolled));
        g_signal_connect(G_OBJECT(vscroll), "button-press-event", G_CALLBACK(i_OnPressed), view);
        g_signal_connect(G_OBJECT(hscroll), "button-press-event", G_CALLBACK(i_OnPressed), view);
        g_signal_connect(G_OBJECT(scrolled), "button-press-event", G_CALLBACK(i_OnPressed), view);
        g_signal_connect(G_OBJECT(view->tview), "button-press-event", G_CALLBACK(i_OnPressed), view);
    }

    gtk_widget_show(view->tview);
    view->buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view->tview));
    g_signal_connect_after(G_OBJECT(view->buffer), "insert-text", G_CALLBACK(i_OnBufferInsert), (gpointer)view);
    g_signal_connect(G_OBJECT(view->buffer), "delete-range", G_CALLBACK(i_OnBufferDelete), (gpointer)view);
    gtk_text_view_set_editable(GTK_TEXT_VIEW(view->tview), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view->tview), GTK_WRAP_WORD);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(view->tview), 5);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(view->tview), 5);
    i_set_wrap_mode(view->tview, TRUE);
    gtk_container_add(GTK_CONTAINER(scrolled), view->tview);
    top = scrolled;
    focus = view->tview;

    /* Creating the frame (border) view */
    {
        GtkWidget *frame = gtk_frame_new(NULL);
        cassert(gtk_widget_get_has_window(frame) == FALSE);
        gtk_container_add(GTK_CONTAINER(frame), top);
        gtk_widget_show(top);

        {
            String *css = _osglobals_frame_focus_css();
            view->css_bdcolor = _oscontrol_css_provider(tc(css));
            str_destroy(&css);
        }

        g_object_set_data(G_OBJECT(scrolled), "OSControl", &view->control);
        gtk_widget_set_state_flags(frame, GTK_STATE_FLAG_FOCUSED, FALSE);
        top = frame;
    }

    view->launch_event = TRUE;
    _oscontrol_init(&view->control, ekGUI_TYPE_TEXTVIEW, top, focus, TRUE);
    return view;
}

/*---------------------------------------------------------------------------*/

static GtkWidget *i_scrolled_window(const OSText *view)
{
    GtkWidget *scrolled = NULL;
    cassert_no_null(view);
    scrolled = view->control.widget;
    cassert(GTK_IS_FRAME(scrolled) == TRUE);
    scrolled = gtk_bin_get_child(GTK_BIN(scrolled));
    cassert(GTK_IS_SCROLLED_WINDOW(scrolled));
    return scrolled;
}

/*---------------------------------------------------------------------------*/

void ostext_destroy(OSText **view)
{
    cassert_no_null(view);
    cassert_no_null(*view);
    listener_destroy(&(*view)->OnFilter);
    listener_destroy(&(*view)->OnFocus);
    gtk_container_remove(GTK_CONTAINER(i_scrolled_window(*view)), (*view)->tview);

    if ((*view)->text_cache != NULL)
    {
        g_free((*view)->text_cache);
        (*view)->text_cache = NULL;
    }

    _oscontrol_destroy_css_provider(&(*view)->css_font);
    _oscontrol_destroy_css_provider(&(*view)->css_color);
    _oscontrol_destroy_css_provider(&(*view)->css_bgcolor);
    _oscontrol_destroy_css_provider(&(*view)->css_pgcolor);
    _oscontrol_destroy_css_provider(&(*view)->css_bdcolor);
    _oscontrol_destroy(*dcast(view, OSControl));
    heap_delete(view, OSText);
}

/*---------------------------------------------------------------------------*/

void ostext_OnFilter(OSText *view, Listener *listener)
{
    cassert_no_null(view);
    listener_update(&view->OnFilter, listener);
}

/*---------------------------------------------------------------------------*/

void ostext_OnFocus(OSText *view, Listener *listener)
{
    cassert_no_null(view);
    listener_update(&view->OnFocus, listener);
}

/*---------------------------------------------------------------------------*/

static ___INLINE gint i_size_pango(const real32_t size, const uint32_t fstyle)
{
    static gint val = 0;
    if ((fstyle & ekFPOINTS) == ekFPOINTS)
    {
        val = (gint)(size * (real32_t)PANGO_SCALE);
    }
    else
    {
        cassert((fstyle & ekFPIXELS) == ekFPIXELS);
        val = (gint)(size / i_device_to_pixels());
    }

    return val;
}

/*---------------------------------------------------------------------------*/

static GtkTextTag *i_tag_attribs(OSText *view)
{
    GtkTextTag *tag = NULL;
    GValue gtrue = G_VALUE_INIT;

    cassert_no_null(view);
    tag = gtk_text_buffer_create_tag(view->buffer, NULL, NULL);
    g_value_init(&gtrue, G_TYPE_BOOLEAN);
    g_value_set_boolean(&gtrue, TRUE);

    {
        GValue gvalue = G_VALUE_INIT;
        g_value_init(&gvalue, G_TYPE_STRING);
        g_value_set_string(&gvalue, (gchar *)view->ffamily);
        g_object_set_property(G_OBJECT(tag), "family-set", &gtrue);
        g_object_set_property(G_OBJECT(tag), "family", &gvalue);
        g_value_unset(&gvalue);
    }

    {
        GValue gvalue = G_VALUE_INIT;
        g_value_init(&gvalue, G_TYPE_INT);
        g_value_set_int(&gvalue, i_size_pango(view->fsize, view->fstyle));
        g_object_set_property(G_OBJECT(tag), "size-set", &gtrue);
        g_object_set_property(G_OBJECT(tag), "size", &gvalue);
        g_value_unset(&gvalue);
    }

    if (view->fstyle & ekFBOLD)
    {
        GValue gvalue = G_VALUE_INIT;
        g_value_init(&gvalue, G_TYPE_INT);
        g_value_set_int(&gvalue, PANGO_WEIGHT_BOLD);
        g_object_set_property(G_OBJECT(tag), "weight-set", &gtrue);
        g_object_set_property(G_OBJECT(tag), "weight", &gvalue);
        g_value_unset(&gvalue);
    }

    if (view->fstyle & ekFITALIC)
    {
        GValue gvalue = G_VALUE_INIT;
        g_value_init(&gvalue, PANGO_TYPE_STYLE);
        g_value_set_enum(&gvalue, PANGO_STYLE_ITALIC);
        g_object_set_property(G_OBJECT(tag), "style-set", &gtrue);
        g_object_set_property(G_OBJECT(tag), "style", &gvalue);
        g_value_unset(&gvalue);
    }

    if (view->fstyle & ekFUNDERLINE)
    {
        GValue gvalue = G_VALUE_INIT;
        g_value_init(&gvalue, PANGO_TYPE_UNDERLINE);
        g_value_set_enum(&gvalue, PANGO_UNDERLINE_SINGLE);
        g_object_set_property(G_OBJECT(tag), "underline-set", &gtrue);
        g_object_set_property(G_OBJECT(tag), "underline", &gvalue);
        g_value_unset(&gvalue);
    }

    if (view->fstyle & ekFSTRIKEOUT)
    {
        GValue gvalue = G_VALUE_INIT;
        g_value_init(&gvalue, G_TYPE_BOOLEAN);
        g_value_set_boolean(&gvalue, TRUE);
        g_object_set_property(G_OBJECT(tag), "strikethrough-set", &gtrue);
        g_object_set_property(G_OBJECT(tag), "strikethrough", &gvalue);
        g_value_unset(&gvalue);
    }

    if (view->color != kCOLOR_DEFAULT)
    {
        GdkRGBA gdkcolor;
        GValue gvalue = G_VALUE_INIT;
        _oscontrol_to_gdkrgba(view->color, &gdkcolor);
        g_value_init(&gvalue, GDK_TYPE_RGBA);
        g_value_set_boxed(&gvalue, &gdkcolor);
        g_object_set_property(G_OBJECT(tag), "foreground-set", &gtrue);
        g_object_set_property(G_OBJECT(tag), "foreground-rgba", &gvalue);
        g_value_unset(&gvalue);
    }

    if (view->bgcolor != kCOLOR_DEFAULT)
    {
        GdkRGBA gdkcolor;
        GValue gvalue = G_VALUE_INIT;
        _oscontrol_to_gdkrgba(view->bgcolor, &gdkcolor);
        g_value_init(&gvalue, GDK_TYPE_RGBA);
        g_value_set_boxed(&gvalue, &gdkcolor);
        g_object_set_property(G_OBJECT(tag), "background-set", &gtrue);
        g_object_set_property(G_OBJECT(tag), "background-rgba", &gvalue);
        g_value_unset(&gvalue);
    }

    if (view->align != ekLEFT)
    {
        GtkJustification justif = _oscontrol_justification(view->align);
        GValue gvalue = G_VALUE_INIT;
        g_value_init(&gvalue, GTK_TYPE_JUSTIFICATION);
        g_value_set_enum(&gvalue, justif);
        g_object_set_property(G_OBJECT(tag), "justification-set", &gtrue);
        g_object_set_property(G_OBJECT(tag), "justification", &gvalue);
        g_value_unset(&gvalue);
    }

    if (view->lspacing_px != 0)
    {
        GValue gvalue = G_VALUE_INIT;
        g_value_init(&gvalue, G_TYPE_INT);
        g_value_set_int(&gvalue, view->lspacing_px);
        g_object_set_property(G_OBJECT(tag), "pixels-inside-wrap-set", &gtrue);
        g_object_set_property(G_OBJECT(tag), "pixels-inside-wrap", &gvalue);
        g_value_unset(&gvalue);
    }

    if (view->bfspace_px > 0)
    {
        GValue gvalue = G_VALUE_INIT;
        g_value_init(&gvalue, G_TYPE_INT);
        g_value_set_int(&gvalue, view->bfspace_px);
        g_object_set_property(G_OBJECT(tag), "pixels-above-lines-set", &gtrue);
        g_object_set_property(G_OBJECT(tag), "pixels-above-lines", &gvalue);
        g_value_unset(&gvalue);
    }

    if (view->afspace_px > 0)
    {
        GValue gvalue = G_VALUE_INIT;
        g_value_init(&gvalue, G_TYPE_INT);
        g_value_set_int(&gvalue, view->afspace_px);
        g_object_set_property(G_OBJECT(tag), "pixels-below-lines-set", &gtrue);
        g_object_set_property(G_OBJECT(tag), "pixels-below-lines", &gvalue);
        g_value_unset(&gvalue);
    }

    g_value_unset(&gtrue);
    return tag;
}

/*---------------------------------------------------------------------------*/

static void i_global_attribs(OSText *view)
{
    const char_t *cssobj = _osglobals_css_textview();
    _oscontrol_update_css_font_desc(view->tview, cssobj, view->ffamily, view->fsize, view->fstyle, &view->css_font);
    _oscontrol_update_css_color(view->tview, cssobj, view->color, &view->css_color);
    _oscontrol_update_css_bgcolor(view->tview, cssobj, view->bgcolor, &view->css_bgcolor);

    {
        GtkJustification justif = _oscontrol_justification(view->align);
        GValue gvalue = G_VALUE_INIT;
        g_value_init(&gvalue, GTK_TYPE_JUSTIFICATION);
        g_value_set_enum(&gvalue, justif);
        g_object_set_property(G_OBJECT(view->tview), "justification", &gvalue);
        g_value_unset(&gvalue);
    }

    {
        GValue gvalue = G_VALUE_INIT;
        g_value_init(&gvalue, G_TYPE_INT);
        g_value_set_int(&gvalue, view->lspacing_px);
        g_object_set_property(G_OBJECT(view->tview), "pixels-inside-wrap", &gvalue);
        g_value_unset(&gvalue);
    }

    {
        GValue gvalue = G_VALUE_INIT;
        g_value_init(&gvalue, G_TYPE_INT);
        g_value_set_int(&gvalue, view->bfspace_px);
        g_object_set_property(G_OBJECT(view->tview), "pixels-above-lines", &gvalue);
        g_value_unset(&gvalue);
    }

    {
        GValue gvalue = G_VALUE_INIT;
        g_value_init(&gvalue, G_TYPE_INT);
        g_value_set_int(&gvalue, view->afspace_px);
        g_object_set_property(G_OBJECT(view->tview), "pixels-below-lines", &gvalue);
        g_value_unset(&gvalue);
    }
}

/*---------------------------------------------------------------------------*/

void ostext_insert_text(OSText *view, const char_t *text)
{
    GtkTextIter end;
    cassert_no_null(view);
    cassert_no_null(text);
    view->launch_event = FALSE;

    if (view->tag_attribs == NULL)
        view->tag_attribs = i_tag_attribs(view);

    gtk_text_buffer_get_end_iter(view->buffer, &end);
    gtk_text_buffer_insert_with_tags(view->buffer, &end, cast_const(text, gchar), -1, view->tag_attribs, NULL);
    view->launch_event = TRUE;
}

/*---------------------------------------------------------------------------*/

static void i_apply_all(OSText *view)
{
    GtkTextIter start, end;
    cassert_no_null(view);

    if (view->tag_attribs == NULL)
        view->tag_attribs = i_tag_attribs(view);

    view->def_attribs = view->tag_attribs;
    gtk_text_buffer_get_start_iter(view->buffer, &start);
    gtk_text_buffer_get_end_iter(view->buffer, &end);
    gtk_text_buffer_remove_all_tags(view->buffer, &start, &end);
    gtk_text_buffer_apply_tag(view->buffer, view->tag_attribs, &start, &end);
}

/*---------------------------------------------------------------------------*/

static void i_iter(GtkTextBuffer *buffer, const int32_t pos, GtkTextIter *iter)
{
    if (pos >= 0)
    {
        gtk_text_buffer_get_start_iter(buffer, iter);
        gtk_text_iter_forward_chars(iter, (gint)pos);
    }
    else
    {
        gtk_text_buffer_get_end_iter(buffer, iter);
    }
}

/*---------------------------------------------------------------------------*/

static void i_apply_sel(OSText *view)
{
    GtkTextIter start, end;
    bool_t apply = FALSE;
    cassert_no_null(view);

    if (view->tag_attribs == NULL)
        view->tag_attribs = i_tag_attribs(view);

    if (gtk_widget_has_focus(GTK_WIDGET(view->control.widget)))
    {
        gtk_text_buffer_get_selection_bounds(view->buffer, &start, &end);
        apply = TRUE;
    }
    else
    {
        if (view->select_start != INT32_MAX)
        {
            i_iter(view->buffer, view->select_start, &start);
            i_iter(view->buffer, view->select_end, &end);
            apply = TRUE;
        }
    }

    if (apply == TRUE)
    {
        gtk_text_buffer_remove_all_tags(view->buffer, &start, &end);
        gtk_text_buffer_apply_tag(view->buffer, view->tag_attribs, &start, &end);
    }
}

/*---------------------------------------------------------------------------*/

void ostext_set_text(OSText *view, const char_t *text)
{
    GtkTextBuffer *buffer = NULL;
    cassert_no_null(view);
    cassert_no_null(text);
    view->launch_event = FALSE;

    if (view->global_attribs == FALSE)
    {
        i_global_attribs(view);
        view->global_attribs = TRUE;
    }

    buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view->tview));
    gtk_text_buffer_set_text(buffer, cast_const(text, gchar), -1);
    view->launch_event = TRUE;
}

/*---------------------------------------------------------------------------*/

void ostext_set_rtf(OSText *view, Stream *rtf_in)
{
    unref(view);
    unref(rtf_in);
}

/*---------------------------------------------------------------------------*/

static gboolean i_select(OSText *view)
{
    cassert_no_null(view);
    if (view->select_start != INT32_MAX)
    {
        GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view->tview));
        GtkTextIter st_iter;
        GtkTextIter ed_iter;
        cassert(view->select_start >= -1);
        cassert(view->select_end >= -1);
        i_iter(buffer, view->select_start, &st_iter);
        i_iter(buffer, view->select_end, &ed_iter);
        gtk_text_buffer_select_range(buffer, &st_iter, &ed_iter);
        view->select_start = INT32_MAX;
        view->select_end = INT32_MAX;
    }

    return FALSE;
}

/*---------------------------------------------------------------------------*/

static gboolean i_scroll_to_caret(OSText *view)
{
    GtkTextIter iter;
    GValue gval = G_VALUE_INIT;
    gint pos;
    cassert_no_null(view);
    g_value_init(&gval, G_TYPE_INT);
    g_object_get_property(G_OBJECT(view->buffer), "cursor-position", &gval);
    pos = g_value_get_int(&gval);
    g_value_unset(&gval);
    gtk_text_buffer_get_start_iter(view->buffer, &iter);
    gtk_text_iter_forward_chars(&iter, pos);
    gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(view->tview), &iter, 0, TRUE, 0.5, 0.5);
    return FALSE;
}

/*---------------------------------------------------------------------------*/

void ostext_property(OSText *view, const gui_text_t prop, const void *value)
{
    cassert_no_null(view);
    switch (prop)
    {
    case ekGUI_TEXT_FAMILY:
        if (str_cmp_c(view->ffamily, cast_const(value, char_t)) != 0)
        {
            str_copy_c(view->ffamily, sizeof(view->ffamily), cast_const(value, char_t));
            view->tag_attribs = NULL;
            view->global_attribs = FALSE;
        }
        break;

    case ekGUI_TEXT_SIZE:
        if (view->fsize != *cast(value, real32_t))
        {
            view->fsize = *cast(value, real32_t);
            view->tag_attribs = NULL;
            view->global_attribs = FALSE;
        }
        break;

    case ekGUI_TEXT_STYLE:
    {
        uint32_t fstyle = *cast(value, uint32_t);
        if (view->fstyle & ekFPOINTS)
            fstyle |= ekFPOINTS;

        if (view->fstyle != fstyle)
        {
            view->fstyle = fstyle;
            view->tag_attribs = NULL;
            view->global_attribs = FALSE;
        }
        break;
    }

    case ekGUI_TEXT_UNITS:
    {
        uint32_t fstyle = view->fstyle & (~ekFPOINTS);

        if (*cast_const(value, uint32_t) & ekFPOINTS)
            fstyle |= ekFPOINTS;

        if (view->fstyle != fstyle)
        {
            view->fstyle = fstyle;
            view->tag_attribs = NULL;
            view->global_attribs = FALSE;
        }
        break;
    }

    case ekGUI_TEXT_COLOR:
        if (view->color != *cast(value, color_t))
        {
            view->color = *cast(value, color_t);
            view->tag_attribs = NULL;
            view->global_attribs = FALSE;
        }
        break;

    case ekGUI_TEXT_BGCOLOR:
        if (view->bgcolor != *cast(value, color_t))
        {
            view->bgcolor = *cast(value, color_t);
            view->tag_attribs = NULL;
            view->global_attribs = FALSE;
        }
        break;

    case ekGUI_TEXT_PGCOLOR:
    {
        const char_t *cssobj = _osglobals_css_textview();
        _oscontrol_update_css_bgcolor(view->tview, cssobj, *cast(value, color_t), &view->css_pgcolor);
        break;
    }

    case ekGUI_TEXT_PARALIGN:
        if (view->align != *cast(value, align_t))
        {
            view->align = *cast(value, align_t);
            view->tag_attribs = NULL;
            view->global_attribs = FALSE;
        }
        break;

    case ekGUI_TEXT_LSPACING:
    {
        real32_t spacing = *cast(value, real32_t);
        gint lspacing = 0;
        if (spacing > 1)
        {
            Font *font = font_create(view->ffamily, view->fsize, view->fstyle);
            real32_t cell_size = font_height(font);
            font_destroy(&font);
            lspacing = (spacing - 1) * cell_size;
        }

        if (view->lspacing_px != lspacing)
        {
            view->lspacing_px = lspacing;
            view->tag_attribs = NULL;
            view->global_attribs = FALSE;
        }

        break;
    }

    case ekGUI_TEXT_AFPARSPACE:
    {
        gint lspace = i_size_pango(*cast(value, real32_t), view->fstyle) / PANGO_SCALE;
        if (lspace >= 0 && lspace != view->afspace_px)
        {
            view->afspace_px = lspace;
            view->tag_attribs = NULL;
            view->global_attribs = FALSE;
        }

        break;
    }

    case ekGUI_TEXT_BFPARSPACE:
    {
        gint lspace = i_size_pango(*cast(value, real32_t), view->fstyle) / PANGO_SCALE;
        if (lspace >= 0 && lspace != view->bfspace_px)
        {
            view->bfspace_px = lspace;
            view->tag_attribs = NULL;
            view->global_attribs = FALSE;
        }

        break;
    }

    case ekGUI_TEXT_APPLY_ALL:
        i_apply_all(view);
        break;

    case ekGUI_TEXT_APPLY_SEL:
        i_apply_sel(view);
        break;

    case ekGUI_TEXT_SELECT:
    {
        int32_t *range = cast(value, int32_t);
        view->select_start = range[0];
        view->select_end = range[1];
        g_idle_add((GSourceFunc)i_select, view);
        break;
    }

    case ekGUI_TEXT_SHOW_SELECT:
    {
        bool_t show = *cast(value, bool_t);
        view->show_select = show;
        break;
    }

    case ekGUI_TEXT_SCROLL:
        /* https://discourse.gnome.org/t/gtk-text-view-scroll-to-iter-does-not-scroll-to-the-desired-position/4257 */
        g_idle_add((GSourceFunc)i_scroll_to_caret, view);
        break;

    case ekGUI_TEXT_WRAP_MODE:
    {
        bool_t wrap = *cast(value, bool_t);
        i_set_wrap_mode(view->tview, wrap);
        break;
    }
        cassert_default();
    }
}

/*---------------------------------------------------------------------------*/

void ostext_editable(OSText *view, const bool_t is_editable)
{
    cassert_no_null(view);
    gtk_text_view_set_editable(GTK_TEXT_VIEW(view->tview), (gboolean)is_editable);
}

/*---------------------------------------------------------------------------*/

const char_t *ostext_get_text(const OSText *view)
{
    GtkTextIter start;
    GtkTextIter end;

    cassert_no_null(view);
    if (view->text_cache != NULL)
    {
        g_free(view->text_cache);
        cast(view, OSText)->text_cache = NULL;
    }

    gtk_text_buffer_get_start_iter(view->buffer, &start);
    gtk_text_buffer_get_end_iter(view->buffer, &end);
    cast(view, OSText)->text_cache = gtk_text_buffer_get_text(view->buffer, &start, &end, FALSE);
    return view->text_cache;
}

/*---------------------------------------------------------------------------*/

void ostext_scroller_visible(OSText *view, const bool_t horizontal, const bool_t vertical)
{
    GtkWidget *scrolled = i_scrolled_window(view);
    GtkWidget *hscroll = gtk_scrolled_window_get_hscrollbar(GTK_SCROLLED_WINDOW(scrolled));
    GtkWidget *vscroll = gtk_scrolled_window_get_vscrollbar(GTK_SCROLLED_WINDOW(scrolled));

    if (horizontal == TRUE)
        gtk_widget_show(hscroll);
    else
        gtk_widget_hide(hscroll);

    if (vertical == TRUE)
        gtk_widget_show(vscroll);
    else
        gtk_widget_hide(vscroll);
}

/*---------------------------------------------------------------------------*/

void ostext_set_need_display(OSText *view)
{
    unref(view);
    cassert(FALSE);
}

/*---------------------------------------------------------------------------*/

void ostext_clipboard(OSText *view, const clipboard_t clipboard)
{
    GtkClipboard *system_board = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
    cassert_no_null(view);
    switch (clipboard)
    {
    case ekCLIPBOARD_COPY:
        gtk_text_buffer_copy_clipboard(view->buffer, system_board);
        break;
    case ekCLIPBOARD_CUT:
        gtk_text_buffer_cut_clipboard(view->buffer, system_board, TRUE);
        break;
    case ekCLIPBOARD_PASTE:
        gtk_text_buffer_paste_clipboard(view->buffer, system_board, NULL, TRUE);
        break;
        cassert_default();
    }
}

/*---------------------------------------------------------------------------*/

void ostext_attach(OSText *view, OSPanel *panel)
{
    _ospanel_attach_control(panel, cast(view, OSControl));
}

/*---------------------------------------------------------------------------*/

void ostext_detach(OSText *view, OSPanel *panel)
{
    _ospanel_detach_control(panel, cast(view, OSControl));
}

/*---------------------------------------------------------------------------*/

void ostext_visible(OSText *view, const bool_t visible)
{
    cassert_no_null(view);
    _oscontrol_set_visible(cast(view, OSControl), visible);
}

/*---------------------------------------------------------------------------*/

void ostext_enabled(OSText *view, const bool_t enabled)
{
    cassert_no_null(view);
    _oscontrol_set_enabled(cast(view, OSControl), enabled);
}

/*---------------------------------------------------------------------------*/

void ostext_size(const OSText *view, real32_t *width, real32_t *height)
{
    _oscontrol_get_size(cast_const(view, OSControl), width, height);
}

/*---------------------------------------------------------------------------*/

void ostext_origin(const OSText *view, real32_t *x, real32_t *y)
{
    _oscontrol_get_origin(cast_const(view, OSControl), x, y);
}

/*---------------------------------------------------------------------------*/

void ostext_frame(OSText *view, const real32_t x, const real32_t y, const real32_t width, const real32_t height)
{
    cassert_no_null(view);
    _oscontrol_set_frame(cast(view, OSControl), x, y, width, height);
}

/*---------------------------------------------------------------------------*/

void _ostext_focus(OSText *view, const bool_t focus)
{
    cassert_no_null(view);
    if (view->OnFocus != NULL)
    {
        bool_t params = focus;
        listener_event(view->OnFocus, ekGUI_EVENT_FOCUS, view, &params, NULL, OSText, bool_t, void);
    }

    if (view->css_bdcolor != NULL)
    {
        cassert(GTK_IS_FRAME(view->control.widget));
        if (focus == TRUE)
            _oscontrol_add_css_provider(view->control.widget, view->css_bdcolor);
        else
            _oscontrol_remove_css_provider(view->control.widget, view->css_bdcolor);
    }

    if (focus == TRUE)
    {
        /* Select */
        g_idle_add((GSourceFunc)i_select, view);
    }
    else
    {
        /* Cache the current selection */
        GtkTextIter start, end;
        GtkTextBuffer *tbuf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view->tview));
        GtkTextIter iter;
        gtk_text_buffer_get_selection_bounds(tbuf, &start, &end);
        view->select_start = (int32_t)gtk_text_iter_get_offset(&start);
        view->select_end = (int32_t)gtk_text_iter_get_offset(&end);

        /* Deselect */
        if (view->show_select == FALSE)
        {
            gtk_text_buffer_get_start_iter(tbuf, &iter);
            gtk_text_buffer_select_range(tbuf, &iter, &iter);
        }
    }
}

/*---------------------------------------------------------------------------*/

bool_t _ostext_capture_return(const OSText *view)
{
    cassert_no_null(view);
    return (bool_t)gtk_text_view_get_editable(GTK_TEXT_VIEW(view->tview));
}

/*---------------------------------------------------------------------------*/

GtkWidget *_ostext_focus_widget(OSText *view)
{
    cassert_no_null(view);
    return view->tview;
}

/*---------------------------------------------------------------------------*/

void _ostext_set_capture(OSText *view, OSControl *control)
{
    cassert_no_null(view);
    view->capture = control;
}

/*---------------------------------------------------------------------------*/

void _ostext_release_capture(OSText *view)
{
    cassert_no_null(view);
    view->capture = NULL;
}

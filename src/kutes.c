#include <nappgui.h>

typedef struct _app_t App;

struct _app_t
{
    Window *window;
    Edit *cmdin;
    Button *run;
    TextView *cmdout;
};

/*---------------------------------------------------------------------------*/

static void i_OnButton(App *app, Event *e)
{
    byte_t buffer[512];
    uint32_t rsize;
    Proc *proc = bproc_exec(edit_get_text(app->cmdin), NULL);
    textview_clear(app->cmdout);
    while (bproc_read(proc, buffer, 512, &rsize, NULL) == TRUE)
    {
        if (rsize < 512)
        {
            buffer[rsize] = '\0';
        }
        else
        {
            buffer[511] = '\0';
        }
        textview_printf(app->cmdout, (char_t *)buffer);
    }
    bproc_close(&proc);
    unref(e);
}

/*---------------------------------------------------------------------------*/

static Panel *i_panel(App *app)
{
    Panel *panel = panel_create();
    Layout *layout = layout_create(1, 3);

    Edit *edit = edit_create();
    Button *button = button_push();
    TextView *text = textview_create();

    layout_edit(layout, edit, 0, 0);
    button_text(button, "Run");
    button_OnClick(button, listener(app, i_OnButton, App));
    layout_button(layout, button, 0, 1);
    layout_textview(layout, text, 0, 2);
    textview_wrap(text, FALSE);
    layout_hsize(layout, 0, 500);
    layout_vsize(layout, 0, 100);
    layout_vsize(layout, 1, 50);
    layout_vsize(layout, 2, 250);
    layout_margin(layout, 5);
    layout_vmargin(layout, 0, 5);
    layout_vmargin(layout, 1, 5);
    panel_layout(panel, layout);

    app->cmdin = edit;
    app->run = button;
    app->cmdout = text;

    return panel;
}

/*---------------------------------------------------------------------------*/

static void i_OnClose(App *app, Event *e)
{
    osapp_finish();
    unref(app);
    unref(e);
}

/*---------------------------------------------------------------------------*/

static App *i_create(void)
{
    App *app = heap_new0(App);
    Panel *panel = i_panel(app);
    app->window = window_create(ekWINDOW_STD);

    window_panel(app->window, panel);
    window_title(app->window, "kutes");
    window_origin(app->window, v2df(500, 200));
    window_OnClose(app->window, listener(app, i_OnClose, App));
    window_show(app->window);
    return app;
}

/*---------------------------------------------------------------------------*/

static void i_destroy(App **app)
{
    window_destroy(&(*app)->window);
    heap_delete(app, App);
}

/*---------------------------------------------------------------------------*/

#include "osmain.h"
osmain(i_create, i_destroy, "", App)

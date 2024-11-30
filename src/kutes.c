#include <yyjson.h>
#include <nappgui.h>

/*---------------------------------------------------------------------------*/

typedef struct _app_t App;

struct _app_t
{
    Window *window;
    Edit *cmdin;
    TextView *cmdout;

    Edit *search;
    const char_t *start_ptr;
    ArrSt(uint32_t) *pos_cache;
    uint32_t cur_idx;
    uint32_t end_len;
    uint32_t pat_len;
};

/*---------------------------------------------------------------------------*/

static void proc_cmd(App *app)
{
    byte_t buffer[512];
    uint32_t rsize;
    Proc *proc = bproc_exec(edit_get_text(app->cmdin), NULL);
    // stdout
    while (bproc_read(proc, buffer, 511, &rsize, NULL) == TRUE)
    {
        buffer[rsize] = '\0';
        textview_writef(app->cmdout, cast(buffer, char_t));
    }
    // stderr
    while (bproc_eread(proc, buffer, 511, &rsize, NULL) == TRUE)
    {
        buffer[rsize] = '\0';
        textview_color(app->cmdout, kCOLOR_RED);
        textview_writef(app->cmdout, cast(buffer, char_t));
    }
    bproc_close(&proc);
}

/*---------------------------------------------------------------------------*/

static void i_OnRun(App *app, Event *e)
{
    const char_t *cmdin = edit_get_text(app->cmdin);
    if (cmdin != NULL)
    {
        app->start_ptr = NULL;
        app->pat_len = 0;
        edit_text(app->search, "");

        textview_clear(app->cmdout);
        textview_color(app->cmdout, kCOLOR_DEFAULT);
    }
    if (str_equ_c(cmdin, "k"))
    {
        String *path = hfile_home_dir("oss/kutes/.cache/out-large.json");
        yyjson_doc *doc = yyjson_read_file(tcc(path), 0, NULL, NULL);
        if (doc)
        {
            yyjson_val *root = yyjson_doc_get_root(doc);
            yyjson_val *items = yyjson_obj_get(root, "items");
            size_t idx, max;
            yyjson_val *item;
            yyjson_arr_foreach(items, idx, max, item)
            {
                yyjson_val *name = yyjson_ptr_get(item, "/metadata/name");
                textview_writef(app->cmdout, yyjson_get_str(name));
                textview_writef(app->cmdout, "\n");
            }
        }
        yyjson_doc_free(doc);
        str_destroy(&path);
    }
    else
    {
        proc_cmd(app);
    }
    unref(e);
}

/*---------------------------------------------------------------------------*/

static void i_OnWrap(App *app, Event *e)
{
    const EvButton *p = event_params(e, EvButton);
    textview_wrap(app->cmdout, p->state == ekGUI_ON ? TRUE : FALSE);
}

/*---------------------------------------------------------------------------*/

static void i_OnSearchUpDown(App *app, Event *e)
{
    if (!app->start_ptr)
        return;

    const EvButton *p = event_params(e, EvButton);
    if (p->index == 0)
    {
        /* current index should be b/n 0 and max index if already found */
        if (app->cur_idx != 0)
            app->cur_idx -= 1;
        else if (app->end_len)
            app->cur_idx = app->end_len - 1;
    }
    else
    {
        app->cur_idx += 1;
        if (app->cur_idx == app->end_len)
            app->cur_idx = 0;
    }

    if (arrst_uint32_t_size(app->pos_cache) > app->cur_idx)
    {
        /* getting from cache */
        int32_t start = *arrst_uint32_t_get(app->pos_cache, app->cur_idx);
        textview_select(app->cmdout, start, start + app->pat_len);
        textview_scroll_caret(app->cmdout);
    }
    else
    {
        char *next = blib_strstr(app->start_ptr, edit_get_text(app->search));
        if (next)
        {
            uint32_t offset = next - app->start_ptr;

            int32_t prev = *arrst_uint32_t_get(app->pos_cache, app->cur_idx - 1);
            arrst_append(app->pos_cache, prev + app->pat_len + offset, uint32_t);

            int32_t start = prev + app->pat_len + offset;
            textview_select(app->cmdout, start, start + app->pat_len);
            textview_scroll_caret(app->cmdout);

            app->start_ptr = next + (sizeof(char_t) * app->pat_len);
        }
        else
        {
            app->end_len = app->cur_idx;
            app->cur_idx = 0;

            /* getting from cache */
            int32_t start = *arrst_uint32_t_get(app->pos_cache, app->cur_idx);
            textview_select(app->cmdout, start, start + app->pat_len);
            textview_scroll_caret(app->cmdout);
        }
    }
}

/*---------------------------------------------------------------------------*/

static void i_OnSearchFilter(App *app, Event *e)
{
    const EvText *p = event_params(e, EvText);
    app->pat_len += p->len;
    if (app->pat_len == 0)
    {
        /* reset starter and cache */
        app->start_ptr = NULL;
        arrst_uint32_t_clear(app->pos_cache, NULL);
        textview_select(app->cmdout, -1, -1);
    }
    else
    {
        /* TODO: possible to replace with Boyer–Moore or Efficient BNDM? */
        app->start_ptr = textview_get_text(app->cmdout);
        char *first = blib_strstr(app->start_ptr, p->text);
        if (first)
        {
            uint32_t offset = first - app->start_ptr;

            arrst_uint32_t_clear(app->pos_cache, NULL);
            arrst_append(app->pos_cache, offset, uint32_t);
            app->cur_idx = 0;

            textview_select(app->cmdout, offset, offset + app->pat_len);
            textview_scroll_caret(app->cmdout);

            app->start_ptr = first + (sizeof(char_t) * app->pat_len);
        }
        else
        {
            /* reset starter and cache */
            app->start_ptr = NULL;
            arrst_uint32_t_clear(app->pos_cache, NULL);
            textview_select(app->cmdout, -1, -1);
        }
    }
}

/*---------------------------------------------------------------------------*/

static Panel *i_panel(App *app)
{
    Panel *panel = panel_create();
    Layout *main = layout_create(1, 3);

    Edit *cmdin = edit_multiline();
    layout_edit(main, cmdin, 0, 0);
    layout_hsize(main, 0, 950);
    layout_vsize(main, 0, 50);

    Layout *ops = layout_create(4, 1);
    Button *run = button_push();
    button_text(run, "run");
    button_OnClick(run, listener(app, i_OnRun, App));
    layout_button(ops, run, 0, 0);

    Button *wrap = button_check();
    button_text(wrap, "wrap");
    button_OnClick(wrap, listener(app, i_OnWrap, App));
    button_state(wrap, ekGUI_OFF);
    layout_button(ops, wrap, 1, 0);

    Edit *search = edit_create();
    edit_phstyle(search, ekFITALIC);
    edit_phtext(search, "search...");
    edit_OnFilter(search, listener(app, i_OnSearchFilter, App));
    layout_edit(ops, search, 2, 0);
    layout_hexpand(ops, 2);

    UpDown *searchUpdown = updown_create();
    updown_OnClick(searchUpdown, listener(app, i_OnSearchUpDown, App));
    layout_updown(ops, searchUpdown, 3, 0);
    layout_layout(main, ops, 0, 1);

    TextView *cmdout = textview_create();
    layout_textview(main, cmdout, 0, 2);
    textview_size(cmdout, s2df(0, 950));
    textview_show_select(cmdout, TRUE);
    textview_wrap(cmdout, FALSE);

    layout_margin(main, 5);
    panel_layout(panel, main);

    app->cmdin = cmdin;
    app->cmdout = cmdout;

    app->search = search;
    app->pos_cache = arrst_create(uint32_t);

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
    window_OnClose(app->window, listener(app, i_OnClose, App));
    window_show(app->window);
    return app;
}

/*---------------------------------------------------------------------------*/

static void i_destroy(App **app)
{
    arrst_uint32_t_destroy(&(*app)->pos_cache, NULL);
    window_destroy(&(*app)->window);
    heap_delete(app, App);
}

/*---------------------------------------------------------------------------*/

#include "osmain.h"
osmain(i_create, i_destroy, "", App)

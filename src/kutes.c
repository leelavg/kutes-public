#include <yyjson.h>
#include <nappgui.h>
#define FOUR_KIB (4 * 1024)

/*---------------------------------------------------------------------------*/
typedef enum _run_t
{
    ekRUN_UNKNOWN,
    ekRUN_STARTED,
    ekRUN_ENDED
} run_t;

typedef struct _app_t App;
struct _app_t
{
    Window *window;
    Edit *cmdin;
    run_t run;
    Button *brun;
    byte_t buffer[FOUR_KIB];
    TextView *cmdout;
    Proc *proc;
    bool_t proc_end;
    Mutex *p_mutex;
    uint32_t rsize;
    bool_t eread;
    bool_t stop;

    Edit *search;
    const char_t *start_ptr;
    ArrSt(uint32_t) *pos_cache;
    uint32_t cur_idx;
    uint32_t end_len;
    uint32_t pat_len;
};

/*---------------------------------------------------------------------------*/

static uint32_t bg_proc_main(App *app)
{
    perror_t err;
    uint32_t sleep;
    sleep = 50;
    while (TRUE)
    {
        bmutex_lock(app->p_mutex);
        if (app->rsize != 0)
        {
            bmutex_unlock(app->p_mutex);
            if (err == ekPAGAIN)
                sleep = 150;
            bthread_sleep(sleep);
            continue;
        }
        else if (app->proc_end)
        {
            uint32_t rval = 0;
            if (!bproc_finish(app->proc, NULL))
            {
                bproc_cancel(app->proc);
                rval = 1;
            }
            bproc_close(&app->proc);
            bmutex_unlock(app->p_mutex);
            /* buffer is read by UI thread and process is closed */
            return rval;
        }
        else if (!(bproc_finish(app->proc, NULL) && app->eread) &&
                 ((bproc_read(app->proc, app->buffer, FOUR_KIB - 1, &app->rsize, &err) || err == ekPAGAIN)))
        {
            app->buffer[app->rsize] = '\0';
        }
        else if (bproc_eread(app->proc, app->buffer, FOUR_KIB - 1, &app->rsize, NULL))
        {
            /* TODO: require non blocking stderr? */
            app->buffer[app->rsize] = '\0';
            app->eread = TRUE;
        }
        else
        {
            app->proc_end = TRUE;
        }
        bmutex_unlock(app->p_mutex);
    }
}

/*---------------------------------------------------------------------------*/

static void i_run_update(App *app)
{
    bmutex_lock(app->p_mutex);
    if (app->rsize == 0)
    {
        bmutex_unlock(app->p_mutex);
        return;
    }
    else if (app->proc_end)
    {
        app->rsize = 0;
        bmutex_unlock(app->p_mutex);
        return;
    }
    else if (app->eread)
    {
        textview_color(app->cmdout, kCOLOR_RED);
    }
    textview_writef(app->cmdout, cast(app->buffer, char_t));
    app->rsize = 0;
    if (app->stop)
        app->proc_end = TRUE;
    bmutex_unlock(app->p_mutex);
}

/*---------------------------------------------------------------------------*/

static void i_run_end(App *app, const uint32_t rval)
{
    edit_editable(app->cmdin, TRUE);
    app->run = ekRUN_ENDED;
    button_text(app->brun, "run");
    app->stop = FALSE;
    if (rval == 0)
    {
        log_printf("ran till the end");
    }
    else
    {
        log_printf("stopped midway");
    }
}

/*---------------------------------------------------------------------------*/

static void i_OnRun(App *app, Event *e)
{
    if (app->run != ekRUN_STARTED)
    {
        const char_t *cmdin;
        edit_editable(app->cmdin, FALSE);
        cmdin = edit_get_text(app->cmdin);
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
            i_run_end(app, 0);
        }
        else
        {
            app->run = ekRUN_STARTED;
            app->proc_end = FALSE;
            app->eread = FALSE;
            app->proc = bproc_exec(edit_get_text(app->cmdin), NULL);
            bproc_write_close(app->proc);
            button_text(app->brun, "stop");
            app->stop = FALSE;
            osapp_task(app, .05, bg_proc_main, i_run_update, i_run_end, App);
        }
    }
    else
    {
        app->stop = TRUE;
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
    const EvButton *p;
    if (!app->start_ptr)
        return;

    p = event_params(e, EvButton);
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
            int32_t start;
            uint32_t offset = next - app->start_ptr;

            int32_t prev = *arrst_uint32_t_get(app->pos_cache, app->cur_idx - 1);
            arrst_append(app->pos_cache, prev + app->pat_len + offset, uint32_t);

            start = prev + app->pat_len + offset;
            textview_select(app->cmdout, start, start + app->pat_len);
            textview_scroll_caret(app->cmdout);

            app->start_ptr = next + (sizeof(char_t) * app->pat_len);
        }
        else
        {
            int32_t start;
            app->end_len = app->cur_idx;
            app->cur_idx = 0;

            /* getting from cache */
            start = *arrst_uint32_t_get(app->pos_cache, app->cur_idx);
            textview_select(app->cmdout, start, start + app->pat_len);
            textview_scroll_caret(app->cmdout);
        }
    }
}

/*---------------------------------------------------------------------------*/

static void i_OnSearchFilter(App *app, Event *e)
{
    const EvText *p = event_params(e, EvText);
    if (app->run != ekRUN_ENDED)
        return;

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
        char *first;
        /* TODO: possible to replace with Boyer–Moore or Efficient BNDM? */
        app->start_ptr = textview_get_text(app->cmdout);
        first = blib_strstr(app->start_ptr, p->text);
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
    Layout *ops = layout_create(4, 1);
    Button *run = button_push();
    Button *wrap = button_check();
    Edit *search = edit_create();
    UpDown *searchUpdown = updown_create();
    TextView *cmdout = textview_create();

    layout_edit(main, cmdin, 0, 0);
    layout_hsize(main, 0, 950);
    layout_vsize(main, 0, 50);

    button_text(run, "run");
    button_OnClick(run, listener(app, i_OnRun, App));
    layout_button(ops, run, 0, 0);

    button_text(wrap, "wrap");
    button_OnClick(wrap, listener(app, i_OnWrap, App));
    button_state(wrap, ekGUI_OFF);
    layout_button(ops, wrap, 1, 0);

    edit_phstyle(search, ekFITALIC);
    edit_phtext(search, "search...");
    edit_OnFilter(search, listener(app, i_OnSearchFilter, App));
    layout_edit(ops, search, 2, 0);
    layout_hexpand(ops, 2);

    updown_OnClick(searchUpdown, listener(app, i_OnSearchUpDown, App));
    layout_updown(ops, searchUpdown, 3, 0);
    layout_layout(main, ops, 0, 1);

    layout_textview(main, cmdout, 0, 2);
    textview_size(cmdout, s2df(0, 950));
    textview_show_select(cmdout, TRUE);
    textview_wrap(cmdout, FALSE);

    layout_margin(main, 5);
    panel_layout(panel, main);

    app->cmdin = cmdin;
    app->brun = run;
    app->cmdout = cmdout;

    app->search = search;

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

    app->pos_cache = arrst_create(uint32_t);
    app->p_mutex = bmutex_create();

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
    bmutex_close(&(*app)->p_mutex);

    window_destroy(&(*app)->window);
    heap_delete(app, App);
}

/*---------------------------------------------------------------------------*/

#include "osmain.h"
osmain(i_create, i_destroy, "", App)

#include <yyjson.h>
#include <nappgui.h>
/*
Reading a 2.5Mi indented JSON took
with 4Ki buffer - 35s, peak 4% CPU
with 16Ki buffer - 10s, peak 5% CPU
with 32Ki buffer - 4s, peak 6% CPU
with 64Ki buffer - 2s, peak 7% CPU
more than 64Ki is increasing overall time with current way of doing I/O
on my machine (fedora 40)
 */
#define SIXTY_FOUR_KB (64 * 1024)

/*---------------------------------------------------------------------------*/

const char_t *st_ready = "⌘ ready";         /* U+2318*/
const char_t *st_running = "⏳running";     /* U+23f3 */
const char_t *st_stopping = "⌛stopping";   /* U+231b */
const char_t *st_stopped = "✕ stopped";     /* U+2715 */
const char_t *st_completed = "✓ completed"; /* U+2713 */

const char_t *bt_run = "&run";
const char_t *bt_stop = "&stop";
const char_t *bt_wrap = "&wrap";
const char_t *bt_tail = "&tail";

/*---------------------------------------------------------------------------*/

typedef enum _run_t
{
    ktRUN_UNKNOWN,
    ktRUN_STARTED,
    ktRUN_ENDED
} run_t;

typedef enum _proc_state_t
{
    ktPROC_UNKNOWN,
    ktPROC_CANCEL,
    ktPROC_COMPLETE
} proc_state_t;

typedef struct _app_t App;
struct _app_t
{
    byte_t buffer[SIXTY_FOUR_KB];
    Window *window;
    Edit *cmdin;
    run_t run;
    Button *brun;
    Button *tail;
    TextView *cmdout;
    Proc *proc;
    proc_state_t proc_state;
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

    Label *status;
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
        if (app->proc_state == ktPROC_CANCEL)
        {
            bproc_cancel(app->proc);
            bproc_close(&app->proc);
            bmutex_unlock(app->p_mutex);
            return 1;
        }
        else if (app->rsize)
        {
            bmutex_unlock(app->p_mutex);
            if (err == ekPAGAIN)
                sleep = 500;
            bthread_sleep(sleep);
            continue;
        }
        else if (app->proc_state == ktPROC_COMPLETE)
        {
            bproc_close(&app->proc);
            bmutex_unlock(app->p_mutex);
            return 0;
        }
        else if (!(bproc_finish(app->proc, NULL) && app->eread) &&
                 ((bproc_read(app->proc, app->buffer, SIXTY_FOUR_KB - 1, &app->rsize, &err) || err == ekPAGAIN)))
        {
            app->buffer[app->rsize] = '\0';
        }
        else if (bproc_eread(app->proc, app->buffer, SIXTY_FOUR_KB - 1, &app->rsize, NULL))
        {
            /* TODO: require non blocking stderr? */
            app->buffer[app->rsize] = '\0';
            app->eread = TRUE;
        }
        else
        {
            app->proc_state = ktPROC_COMPLETE;
        }
        bmutex_unlock(app->p_mutex);
    }
}

/*---------------------------------------------------------------------------*/

static void i_run_update(App *app)
{
    bmutex_lock(app->p_mutex);
    if (app->stop)
        app->proc_state = ktPROC_CANCEL;
    if (app->rsize == 0)
    {
        bmutex_unlock(app->p_mutex);
        return;
    }
    else if (app->proc_state == ktPROC_COMPLETE)
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
    if (button_get_state(app->tail) == ekGUI_ON)
        textview_scroll_caret(app->cmdout);
    app->rsize = 0;
    bmutex_unlock(app->p_mutex);
}

/*---------------------------------------------------------------------------*/

static void i_run_end(App *app, const uint32_t rval)
{
    edit_editable(app->cmdin, TRUE);
    app->run = ktRUN_ENDED;
    button_text(app->brun, bt_run);
    app->stop = FALSE;
    if (rval == 0)
    {
        label_text(app->status, st_completed);
    }
    else
    {
        label_text(app->status, st_stopped);
    }
}

/*---------------------------------------------------------------------------*/

static void i_OnRun(App *app, Event *e)
{
    if (app->run != ktRUN_STARTED)
    {
        const char_t *cmdin;
        cmdin = edit_get_text(app->cmdin);

        label_text(app->status, st_ready);
        app->start_ptr = NULL;
        app->pat_len = 0;
        edit_text(app->search, "");
        textview_clear(app->cmdout);
        textview_color(app->cmdout, kCOLOR_DEFAULT);

        if (cmdin && !cmdin[0])
        {
            return;
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
            app->run = ktRUN_STARTED;
            edit_editable(app->cmdin, FALSE);
            app->proc_state = ktPROC_UNKNOWN;
            app->eread = FALSE;
            app->proc = bproc_exec(cmdin, NULL);
            label_text(app->status, st_running);
            bproc_write_close(app->proc);
            button_text(app->brun, bt_stop);
            app->stop = FALSE;
            osapp_task(app, 0., bg_proc_main, i_run_update, i_run_end, App);
        }
    }
    else
    {
        app->stop = TRUE;
        label_text(app->status, st_stopping);
    }
    unref(e);
}

/*---------------------------------------------------------------------------*/

static void i_OnWrap(App *app, Event *e)
{
    const EvButton *p;
    if (app->proc_state == ktPROC_UNKNOWN)
    {
        unref(e);
        return;
    }
    p = event_params(e, EvButton);
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

static void i_focus_search(App *app, Event *e)
{
    window_focus(app->window, guicontrol(app->search));
    unref(e);
}

/*---------------------------------------------------------------------------*/

static void i_OnSearchFilter(App *app, Event *e)
{
    const EvText *p = event_params(e, EvText);
    if (app->run != ktRUN_ENDED)
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
    Layout *main = layout_create(1, 4);
    Edit *cmdin = edit_multiline();
    Layout *ops = layout_create(5, 1);
    Button *run = button_push();
    Button *wrap = button_check();
    Button *tail = button_check();
    Edit *search = edit_create();
    UpDown *searchUpdown = updown_create();
    TextView *cmdout = textview_create();
    Label *status = label_multiline();

    layout_edit(main, cmdin, 0, 0);
    layout_hsize(main, 0, 950);
    layout_vsize(main, 0, 50);

    button_text(run, bt_run);
    button_OnClick(run, listener(app, i_OnRun, App));
    layout_button(ops, run, 0, 0);

    button_text(wrap, bt_wrap);
    button_OnClick(wrap, listener(app, i_OnWrap, App));
    button_state(wrap, ekGUI_OFF);
    layout_button(ops, wrap, 1, 0);

    button_text(tail, bt_tail);
    button_state(tail, ekGUI_OFF);
    layout_button(ops, tail, 2, 0);

    edit_phstyle(search, ekFITALIC);
    edit_phtext(search, "search...");
    edit_OnFilter(search, listener(app, i_OnSearchFilter, App));
    layout_edit(ops, search, 3, 0);
    layout_hexpand(ops, 3);

    updown_OnClick(searchUpdown, listener(app, i_OnSearchUpDown, App));
    layout_updown(ops, searchUpdown, 4, 0);
    layout_layout(main, ops, 0, 1);

    textview_size(cmdout, s2df(0, 750));
    textview_show_select(cmdout, TRUE);
    textview_wrap(cmdout, FALSE);
    layout_textview(main, cmdout, 0, 2);

    label_text(status, st_ready);
    layout_halign(main, 0, 3, ekJUSTIFY);
    layout_label(main, status, 0, 3);

    layout_margin(main, 5);
    panel_layout(panel, main);

    app->cmdin = cmdin;
    app->brun = run;
    app->tail = tail;
    app->search = search;
    app->cmdout = cmdout;
    app->status = status;

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
    Panel *panel;
    draw2d_preferred_monospace("Noto Sans Mono");
    panel = i_panel(app);
    app->window = window_create(ekWINDOW_STD);

    app->pos_cache = arrst_create(uint32_t);
    app->p_mutex = bmutex_create();

    window_panel(app->window, panel);
    window_title(app->window, "kutes");
    window_OnClose(app->window, listener(app, i_OnClose, App));
    window_show(app->window);
    window_hotkey(app->window, ekKEY_F, ekMKEY_CONTROL, listener(app, i_focus_search, App));
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

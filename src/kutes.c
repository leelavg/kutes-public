#include "kt.h"

#include <boron.h>
#include <yyjson.h>
#include <nappgui.h>

byte_t bmatch[kTEXTFILTER_SIZE];

/*---------------------------------------------------------------------------*/

struct _inops_t
{
    Cell *c1;
    Cell *c2;
};

/*---------------------------------------------------------------------------*/

static const char_t *bt_wrap = "&wrap";
static const char_t *bt_tail = "&tail";
static const char_t *bt_err_search = "std&err";

static const char_t *info = "\
only last 512 KiB of stdout data is displayed here.\n\n\
checking 'stderr' searches stderr text or 'stdout' by default.\n\n\
'noshow' suppresses stdout display but still stored for parsing.\n\n\
if 'nolimit' is checked full stdout is stored for parsing or limited to 4 MiB.\
";

/*---------------------------------------------------------------------------*/

static uint32_t bg_proc_main(App *app)
{
    bproc_wait_exit(&app->proc);
    return 0;
}

/*---------------------------------------------------------------------------*/

static void i_OnEditFilter(App *app, Event *e)
{
    const EvText *p = event_params(e, EvText);
    EvTextFilter *r = event_result(e, EvTextFilter);
    const char_t *cmdin = p->text;
    uint32_t pos = p->cpos;
    uint32_t bmatch_len = 0;

    if (!app->complete)
        return;

    /* don't suggest anything if whole line is deleted */
    if (!pos && p->len < 0)
    {
        r->text[0] = '\0';
        r->apply = TRUE;
        return;
    }

    bmatch_len = history_search(app->hist, cast(cmdin, byte_t), pos, bmatch, kTEXTFILTER_SIZE);
    if (bmatch_len)
    {
        bmem_copy(cast(r->text, byte_t), bmatch, bmatch_len);
        r->text[bmatch_len] = '\0';
    }
    else
    {
        bmem_copy(cast(r->text, byte_t), cast(p->text, byte_t), p->cpos);
        r->text[p->cpos] = '\0';
    }
    r->cpos = p->cpos;
    r->apply = TRUE;
}

/*---------------------------------------------------------------------------*/

static void i_OnEditChange(App *app, Event *e)
{
    const EvText *p = event_params(e, EvText);
    app->cpos = p->cpos;
}

/*---------------------------------------------------------------------------*/

static void i_run_update(App *app)
{
    perror_t out, err;
    bool_t read0 = TRUE;
    if (app->run_state != ktRUN_INPROGRESS)
    {
        return;
    }
    else if (app->stop)
    {
        bproc_cancel(app->proc);
        bproc_close(&app->proc);
        app->run_state = ktRUN_CANCEL;
        return;
    }

    if (bproc_read(app->proc, app->read_buf, READ_BUFFER - 1, &app->rsize, &out))
    {
        if (app->nolimit && app->out_len + app->rsize > app->parse_size)
        {
            /* TODO: what if this fails and old pointer is lost?, in assert mode this throws a fatal error */
            app->parse_buf = heap_realloc_n(app->parse_buf, app->parse_size, app->parse_size + INCR_PARSE_SIZE, byte_t);
            app->parse_size += INCR_PARSE_SIZE;
        }

        if (app->nolimit || app->out_len < INITIAL_PARSE_SIZE)
            bmem_copy(app->parse_buf + (app->out_len * sizeof(char_t)), app->read_buf, app->rsize);

        app->read_buf[app->rsize] = '\0';
        app->out_len += app->rsize;
        if (button_get_state(app->noshow) == ekGUI_OFF)
        {
            if (app->out_len > MAX_OUT_SIZE)
            {
                textview_select(app->cmdout, 0, app->rsize);
                textview_del_sel(app->cmdout);
                textview_select(app->cmdout, -1, -1);
            }
            textview_writef(app->cmdout, cast(app->read_buf, char_t));
        }
        read0 = FALSE;
    }

    if (bproc_eread(app->proc, app->read_buf, READ_BUFFER - 1, &app->rsize, &err))
    {
        app->read_buf[app->rsize] = '\0';
        app->err_len += app->rsize;
        if (app->err_len > MAX_ERR_SIZE)
        {
            textview_select(app->cmderr, 0, app->rsize);
            textview_del_sel(app->cmderr);
            textview_select(app->cmderr, -1, -1);
        }
        textview_writef(app->cmderr, cast(app->read_buf, char_t));
        read0 = FALSE;
    }

    if (button_get_state(app->tail) == ekGUI_ON)
    {
        textview_scroll_caret(app->cmdout);
        textview_scroll_caret(app->cmderr);
    }

    if (read0 && out != ekPAGAIN && err != ekPAGAIN)
    {
        bproc_close(&app->proc);
        app->run_state = ktRUN_COMPLETE;
    }
}

/*---------------------------------------------------------------------------*/

static void i_run_end(App *app, const uint32_t rval)
{
    /* reset state */
    edit_editable(app->cmdin, TRUE);
    cell_enabled(app->nolimitb, TRUE);
    button_text(app->run, bt_run);
    app->stop = FALSE;
    if (app->run_state == ktRUN_COMPLETE)
    {
        label_text(app->status, st_completed);
        app->run_state = ktRUN_ENDED;
        /* 119 is empty resource list */
        if (app->out_len > 119 && (app->nolimit || app->out_len < INITIAL_PARSE_SIZE))
        {
            populate_views(app);
        }
    }

    else if (app->run_state == ktRUN_CANCEL)
    {
        label_text(app->status, st_stopped);
        app->run_state = ktRUN_ENDED;
    }
    else
    {
        label_text(app->status, st_unknown);
    }
    unref(rval);
}

/*---------------------------------------------------------------------------*/

static void viewdata_destroy(Destroyer **destr)
{
    if ((*destr)->func_closure)
        (*destr)->func_closure(*destr);
    heap_delete(destr, Destroyer);
}

/*---------------------------------------------------------------------------*/

static void i_OnRun(App *app, Event *e)
{
    if (app->run_state == ktRUN_ENDED)
    {
        const char_t *cmdin;
        uint32_t total = popup_count(app->vselect);
        cmdin = edit_get_text(app->cmdin);

        label_text(app->status, st_ready);
        app->start_ptr = NULL;
        app->pat_len = 0;
        app->out_len = 0;
        app->err_len = 0;
        edit_text(app->search, "");
        textview_clear(app->cmdout);
        textview_clear(app->cmderr);

        popup_selected(app->vselect, 0);
        layout_show_row(app->vscroll, 0, TRUE);
        layout_update(app->vscroll);
        while ((total--) > 1)
        {
            /* TODO: we can reuse the layout rather than adding & removing continuosuly
            as it allocates and deallocates memory considerably */
            popup_del_elem(app->vselect, 1);
            if (arrpt_size(app->views, Destroyer))
                arrpt_pop(app->views, viewdata_destroy, Destroyer);
            layout_remove_row(app->vscroll, 1);
        }
        layout_update(app->vscroll);
        if (app->doc)
        {
            yyjson_mut_doc_free(app->doc);
            app->doc = NULL;
        }

        if (cmdin && cmdin[0])
        {
            uint32_t len = app->cpos;
            if (cmdin[0] != ' ')
                history_append(app->hist, cast(cmdin, byte_t), len);
            bmem_copy(bmatch, cast(cmdin, byte_t), len);
            bmatch[len] = '\0';

            app->proc = bproc_exec(cast(bmatch, char_t), NULL);
            app->run_state = ktRUN_INPROGRESS;
            edit_editable(app->cmdin, FALSE);
            cell_enabled(app->nolimitb, FALSE);
            label_text(app->status, st_running);
            bproc_write_close(app->proc);
            button_text(app->run, bt_stop);
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
    if (app->run_state != ktRUN_ENDED)
    {
        return;
    }
    p = event_params(e, EvButton);
    textview_wrap(app->cmdout, p->state == ekGUI_ON ? TRUE : FALSE);
    textview_wrap(app->cmderr, p->state == ekGUI_ON ? TRUE : FALSE);
}

/*---------------------------------------------------------------------------*/

static void i_ResetSearch(App *app, Event *e)
{
    app->start_ptr = NULL;
    arrst_clear(app->pos_cache, NULL, uint32_t);
    textview_select(app->cmdout, -1, -1);
    textview_select(app->cmderr, -1, -1);
    unref(e);
}

/*---------------------------------------------------------------------------*/

static void i_NoSizeLimit(App *app, Event *e)
{
    const EvButton *p = event_params(e, EvButton);
    app->nolimit = p->state == ekGUI_ON ? TRUE : FALSE;
}

/*---------------------------------------------------------------------------*/

static void i_OnSearchUpDown(App *app, Event *e)
{
    const EvButton *p;
    TextView *tview;
    if (!app->start_ptr)
        return;

    tview = button_get_state(app->err_search) == ekGUI_ON
                ? app->cmderr
                : app->cmdout;

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

    if (arrst_size(app->pos_cache, uint32_t) > app->cur_idx)
    {
        /* getting from cache */
        int32_t start = *arrst_get_const(app->pos_cache, app->cur_idx, uint32_t);
        textview_select(tview, start, start + app->pat_len);
        textview_scroll_caret(tview);
    }
    else
    {
        char *next = blib_strstr(app->start_ptr, edit_get_text(app->search));
        if (next)
        {
            int32_t start;
            uint32_t offset = next - app->start_ptr;

            int32_t prev = *arrst_get_const(app->pos_cache, app->cur_idx - 1, uint32_t);
            arrst_append(app->pos_cache, prev + app->pat_len + offset, uint32_t);

            start = prev + app->pat_len + offset;
            textview_select(tview, start, start + app->pat_len);
            textview_scroll_caret(tview);

            app->start_ptr = next + (sizeof(char_t) * app->pat_len);
        }
        else
        {
            int32_t start;
            app->end_len = app->cur_idx;
            app->cur_idx = 0;

            /* getting from cache */
            start = *arrst_get_const(app->pos_cache, app->cur_idx, uint32_t);
            textview_select(tview, start, start + app->pat_len);
            textview_scroll_caret(tview);
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
    TextView *tview;
    if (app->run_state != ktRUN_ENDED)
        return;

    tview = button_get_state(app->err_search) == ekGUI_ON
                ? app->cmderr
                : app->cmdout;

    app->pat_len += p->len;
    if (app->pat_len == 0)
    {
        /* reset starter and cache */
        app->start_ptr = NULL;
        arrst_clear(app->pos_cache, NULL, uint32_t);
        textview_select(tview, -1, -1);
    }
    else
    {
        char *first;
        /* TODO: possible to replace with Boyer–Moore or Efficient BNDM? */
        app->start_ptr = textview_get_text(tview);
        first = blib_strstr(app->start_ptr, p->text);
        if (first)
        {
            uint32_t offset = first - app->start_ptr;

            arrst_clear(app->pos_cache, NULL, uint32_t);
            arrst_append(app->pos_cache, offset, uint32_t);
            app->cur_idx = 0;

            textview_select(tview, offset, offset + app->pat_len);
            textview_scroll_caret(tview);

            app->start_ptr = first + (sizeof(char_t) * app->pat_len);
        }
        else
        {
            /* reset starter and cache */
            app->start_ptr = NULL;
            arrst_clear(app->pos_cache, NULL, uint32_t);
            textview_select(tview, -1, -1);
        }
    }
}

/*---------------------------------------------------------------------------*/

static void i_OnSearchFocus(App *app, Event *e)
{
    const bool_t *p = event_params(e, bool_t);
    if (*p)
    {
        EvText params;
        params.text = edit_get_text(app->search);
        params.cpos = blib_strlen(params.text);
        params.len = 0;
        if (params.cpos)
        {
            Listener *func = listener(app, i_OnSearchFilter, App);
            listener_event(func, ekGUI_TYPE_EDITBOX, NULL, &params, NULL, void, EvText, void);
            listener_destroy(&func);
        }
    }
    unref(app);
}

/*---------------------------------------------------------------------------*/

void lock_view(opsv *locker, bool_t lock)
{
    cell_enabled(locker->c1, !lock);
    cell_enabled(locker->c2, !lock);
}

/*---------------------------------------------------------------------------*/

static void i_OnPopupSelect(App *app, Event *e)
{
    const EvButton *p = event_params(e, EvButton);
    if (p->index != UINT32_MAX)
    {
        adjust_vscroll(app->vscroll, p->index, popup_count(app->vselect));
    }
}

/*---------------------------------------------------------------------------*/

static void i_OnComplete(App *app, Event *e)
{
    const EvButton *p = event_params(e, EvButton);
    app->complete = p->state == ekGUI_ON ? TRUE : FALSE;
}

/*---------------------------------------------------------------------------*/

static void i_OnInvert(App *app, Event *e)
{
#if defined(__LINUX__)
    const EvButton *p = event_params(e, EvButton);
    osapp_theme_invert(p->state == ekGUI_ON ? true : false);
#else
    log_printf("inverting theme not supported");
    unref(e);
#endif
    unref(app);
}

/*---------------------------------------------------------------------------*/

static Panel *i_panel(App *app)
{
    Panel *panel = panel_scroll(TRUE, TRUE);

    Layout *pane = layout_create(1, 1);
    Layout *main = layout_create(1, 3);
    Layout *input = layout_create(1, 2);
    Layout *inops = layout_create(3, 1);
    Layout *vscroll = layout_create(1, 1);
    Layout *state = layout_create(2, 1);

    Font *font = font_monospace(font_regular_size(), ekFNORMAL);
    Edit *cmdin = edit_multiline();
    Button *run = button_push();
    PopUp *vselect = popup_create();
    Button *complete = button_check();

    Layout *result = layout_create(1, 2);
    Layout *ops = layout_create(7, 1);
    Button *wrap = button_check();
    Button *tail = button_check();
    Edit *search = edit_create();
    Button *err_search = button_check();
    Button *noshow = button_check();
    Button *nolimitb = button_check();
    UpDown *searchUpdown = updown_create();
    SplitView *hsplit = splitview_horizontal();
    TextView *cmdout = textview_create();
    TextView *cmderr = textview_create();

    Label *status = label_create();
    Button *theme = button_check();

    real32_t width = .7f * app->dsize.width;
    real32_t height = .8f * app->dsize.height;

    /* on my laptop these sizes are looking good */
    panel_size(panel, s2df(min_r32(940, width), min_r32(900, height)));
    layout_vsize(input, 0, 50);
    layout_vsize(input, 1, 25);
    layout_margin4(main, 5, 5, 0, 5);

    /* edit_phstyle(cmdin, ekFITALIC);
    edit_phtext(cmdin, "... on your command ..."); */
    edit_font(cmdin, font);
    edit_OnFilter(cmdin, listener(app, i_OnEditFilter, App));
    edit_OnChange(cmdin, listener(app, i_OnEditChange, App));
    font_destroy(&font);
    layout_edit(input, cmdin, 0, 0);

    button_text(run, bt_run);
    button_OnClick(run, listener(app, i_OnRun, App));

    layout_button(inops, run, 0, 0);

    popup_add_elem(vselect, "result", NULL);
    popup_OnSelect(vselect, listener(app, i_OnPopupSelect, App));

    layout_popup(inops, vselect, 1, 0);
    layout_hexpand(inops, 1);

    button_text(complete, "complete");
    button_state(complete, ekGUI_ON);
    button_OnClick(complete, listener(app, i_OnComplete, App));

    layout_button(inops, complete, 2, 0);

    layout_layout(input, inops, 0, 1);

    layout_layout(main, input, 0, 0);

    button_text(wrap, bt_wrap);
    button_OnClick(wrap, listener(app, i_OnWrap, App));
    button_state(wrap, ekGUI_OFF);

    layout_button(ops, wrap, 0, 0);

    button_text(tail, bt_tail);
    button_state(tail, ekGUI_OFF);

    layout_button(ops, tail, 1, 0);

    edit_phstyle(search, ekFITALIC);
    edit_phtext(search, "search...");
    edit_OnFilter(search, listener(app, i_OnSearchFilter, App));
    edit_OnFocus(search, listener(app, i_OnSearchFocus, App));

    layout_edit(ops, search, 2, 0);
    layout_hexpand(ops, 2);

    updown_OnClick(searchUpdown, listener(app, i_OnSearchUpDown, App));

    layout_updown(ops, searchUpdown, 3, 0);

    button_text(err_search, bt_err_search);
    button_OnClick(err_search, listener(app, i_ResetSearch, App));
    button_state(err_search, ekGUI_OFF);

    layout_button(ops, err_search, 4, 0);

    button_text(noshow, "noshow");
    button_state(noshow, ekGUI_OFF);

    layout_button(ops, noshow, 5, 0);

    button_text(nolimitb, "nolimit");
    button_OnClick(nolimitb, listener(app, i_NoSizeLimit, App));
    button_state(nolimitb, ekGUI_OFF);

    layout_button(ops, nolimitb, 6, 0);

    layout_layout(result, ops, 0, 0);

    textview_show_select(cmdout, TRUE);
    textview_wrap(cmdout, FALSE);
    textview_fstyle(cmdout, ekFITALIC);
    textview_writef(cmdout, info);
    textview_fstyle(cmdout, ekFNORMAL);
    splitview_text(hsplit, cmdout, FALSE);

    textview_color(cmderr, kCOLOR_RED);
    textview_show_select(cmderr, TRUE);
    textview_wrap(cmderr, FALSE);
    textview_fstyle(cmderr, ekFITALIC);
    textview_writef(cmderr, "only last 256 KiB of stderr data is displayed here.");
    textview_fstyle(cmderr, ekFNORMAL);
    splitview_text(hsplit, cmderr, FALSE);

    splitview_pos(hsplit, .80f);

    layout_splitview(result, hsplit, 0, 1);

    layout_vexpand(result, 1);
    layout_layout(vscroll, result, 0, 0);
    layout_layout(main, vscroll, 0, 1);
    layout_vexpand(main, 1);

    label_text(status, st_ready);
    layout_halign(state, 0, 0, ekJUSTIFY);

    layout_label(state, status, 0, 0);

    button_text(theme, "invert");
    button_OnClick(theme, listener(app, i_OnInvert, App));

    layout_halign(state, 1, 0, ekRIGHT);
    layout_button(state, theme, 1, 0);

    layout_layout(main, state, 0, 2);

    layout_layout(pane, main, 0, 0);
    panel_layout(panel, pane);

    app->cmdin = cmdin;
    app->run = run;
    app->vselect = vselect;
    app->vscroll = vscroll;
    app->tail = tail;
    app->search = search;
    app->err_search = err_search;
    app->noshow = noshow;
    app->nolimitb = layout_cell(ops, 6, 0);
    app->cmdout = cmdout;
    app->cmderr = cmderr;
    app->status = status;
    app->locker = heap_new(opsv);
    app->locker->c1 = layout_cell(inops, 0, 0);
    app->locker->c2 = layout_cell(inops, 1, 0);

    return panel;
}

/*---------------------------------------------------------------------------*/

static void i_OnClose(App *app, Event *e)
{
    if (app->proc)
    {
        bproc_cancel(app->proc);
        bproc_close(&app->proc);
    }
    osapp_finish();
    unref(e);
}

/*---------------------------------------------------------------------------*/

static void i_OnResize(App *app, Event *e)
{
    adjust_vscroll(app->vscroll, popup_get_selected(app->vselect), popup_count(app->vselect));
    unref(e);
}

/*---------------------------------------------------------------------------*/

static App *i_create(void)
{
    App *app = heap_new0(App);
    Panel *panel;
    draw2d_preferred_monospace("Noto Sans Mono");
    app->dsize = gui_resolution();
    panel = i_panel(app);
    app->window = window_create(ekWINDOW_STDRES);
    app->hist = history_load();
    app->complete = TRUE;
    app->pos_cache = arrst_create(uint32_t);
    app->run_state = ktRUN_ENDED;
    app->alc = alc_init("yyjson");
    app->parse_size = INITIAL_PARSE_SIZE + READ_BUFFER + YYJSON_PADDING_SIZE;
    app->parse_buf = heap_new_n(app->parse_size, byte_t);
    app->nolimit = FALSE;
    app->doc = NULL;
    app->views = arrpt_create(Destroyer);
    cols_bind();

    window_panel(app->window, panel);
    window_title(app->window, "kutes");
    window_OnClose(app->window, listener(app, i_OnClose, App));
    window_OnResize(app->window, listener(app, i_OnResize, App));
    window_show(app->window);
    window_hotkey(app->window, ekKEY_F, ekMKEY_CONTROL, listener(app, i_focus_search, App));
    app->uthread = uthread_create();
    cassert_no_null(app->uthread);
    osapp_theme_invert(TRUE);
    return app;
}

/*---------------------------------------------------------------------------*/

static void i_destroy(App **app)
{
    if ((*app)->doc)
        yyjson_mut_doc_free((*app)->doc);
    if ((*app)->uthread)
        uthread_destroy(&(*app)->uthread);
    history_flush(&(*app)->hist);
    arrpt_destroy(&(*app)->views, viewdata_destroy, Destroyer);
    arrst_destroy(&(*app)->pos_cache, NULL, uint32_t);
    heap_delete_n(&(*app)->parse_buf, (*app)->parse_size, byte_t);
    window_destroy(&(*app)->window);
    alc_dest(&(*app)->alc);
    heap_delete(&(*app)->locker, opsv);
    heap_delete(app, App);
}

/*---------------------------------------------------------------------------*/

#include <osapp/osmain.h>
osmain(i_create, i_destroy, "", App)

/* trying to implement sample readline for history */
#include <nappgui.h>
#include <rax.h>

typedef struct _history_t History;
struct _history_t
{
    Stream *stm;
    rax *rt;
    raxIterator rit;
};

typedef struct _app_t App;
struct _app_t
{
    Window *window;
    Edit *edit;
    TextView *text;
    History *hist;
    uint32_t cpos;
    bool_t complete;
};

History *history_load(void);
bool_t history_append(History *hist, byte_t *data, uint32_t len);
uint32_t history_search(History *hist, byte_t *prefix, uint32_t prefix_len, byte_t *match, uint32_t max_len);
void history_flush(History **hist);

static const char_t *words[] = {
    "tlp-stat -s",
    "cmake --build build && ./build/Debug/bin/kutes",
    "k delete cm temp --wait=false",
    "lite",
    "hx main.go",
    "echo ${TERM_PROGRAM}",
    "rg command -i src/raft/",
    "make -C support/ linux64-b && ./build-linux64/Debug/bin/kutes",
    "workon lsp",
    "ls",
    "cmake -S . -B build; cmake --build build -j 4;",
    "k describe no/example.com",
    "z kutes",
    "l",
    "go mod init gio-learn",
    "hx",
    "oc debug no -ndefault example.com",
    "k get cm --watch -ojson --output-watch-events --show-managed-fields=false",
    "sed 's,server: .*$,server: https://127.0.0.1:3001,' $KUBECONFIG",
    "cd support/",
    "ET_NO_TELEMETRY=y et root@jph",
    "hugo new post",
    "cmake -S . -B build",
    "echo '#include <WINDOws.h>' > /tmp/123/file",
    "cd todo-app/",
    "k edit proxy cluster",
    "gzip -d rebol3-bulk-linux-musl-x64.gz",
    "man 2 sync",
    "zola serve -i 192.168.143.10 -u 192.168.143.10 -h",
    "cmake --build build",
    "install zls ~/.local/bin/",
    "pip install ipykernel",
    "echo build > .gitignore",
    "ls",
    "ls sun/",
    "rlwrap -a -A -f . boron -s server-2.b",
    "dnf search pcap-savefile",
    "wish pad.tcl",
    "cfg push --set-upstream origin HEAD:main -f",
    "hx",
    "rg bstd_sprintf src/",
    "declare -i lvl=$((SHLVL-2));",
    "sudo dnf update celluloid",
    "diff --help",
    "gc -b e01071a",
    "ls ~/.local/lib/python3.12/site-packages/toga/__pycache__/",
    "g show --pretty=fuller 921a224",
    "man past",
    "./build/Debug/bin/HelloWorld",
    "install red-toolchain-25jun24-6e9e6a5f5 ~/.local/bin/redt",
    NULL,
};

History *history_load(void)
{
    History *hist = heap_new(History);
    String *hifile = hfile_appdata("history");
    Stream *stm;

    hist->rt = raxNew();
    raxStart(&hist->rit, hist->rt);
    if (!hfile_exists(tc(hifile), NULL))
    {
        /* TODO: default permissions are too open (0777) */
        Stream *stm = stm_to_file(tc(hifile), NULL);
        uint32_t i, len;
        cassert_no_null(stm);
        stm_write_u8(stm, 0);
        for (i = 0; words[i]; ++i)
        {
            /* load some dummy data */
            len = blib_strlen(words[i]);
            stm_write_u32(stm, len);
            stm_write(stm, cast(words[i], byte_t), len);
        }
        stm_close(&stm);
    }

    stm = hfile_stream(tc(hifile), NULL);
    if (stm)
    {
        uint8_t ver = stm_read_u8(stm);
        byte_t data[kTEXTFILTER_SIZE];
        cassert(ver == 0);
        while (stm_state(stm) != ekSTEND)
        {
            uint32_t len = stm_read_u32(stm);
            uint32_t read = stm_read(stm, data, len);
            cassert(len == read);
            /* TODO: implement load/store the tree directly from/to disk */
            raxInsert(hist->rt, data, len, NULL, NULL);
        }
        stm_close(&stm);
        hist->stm = stm_append_file(tc(hifile), NULL);
        cassert_no_null(hist->stm);
    }
    str_destroy(&hifile);
    return hist;
}

bool_t history_append(History *hist, byte_t *data, uint32_t len)
{
    if (raxInsert(hist->rt, data, len, NULL, NULL))
    {
        stm_write_u32(hist->stm, len);
        stm_write(hist->stm, data, len);
        return TRUE;
    }
    return FALSE;
}

uint32_t history_search(History *hist, byte_t *prefix, uint32_t prefix_len, byte_t *match, uint32_t max_len)
{
    raxIterator it = hist->rit;
    uint32_t mlen = 0;
    raxSeek(&it, ">=", prefix, prefix_len);
    if (raxNext(&it) && !bmem_cmp(it.key, prefix, prefix_len))
    {
        mlen = min_u32(it.key_len, max_len);
        /* TODO: avoid copy and directly return key? */
        bmem_copy(match, it.key, mlen);
    }
    return mlen;
}

void history_flush(History **hist)
{
    if ((*hist)->stm)
        stm_close(&(*hist)->stm);
    raxStop(&(*hist)->rit);
    raxFree((*hist)->rt);
    heap_delete(hist, History);
}

byte_t bmatch[kTEXTFILTER_SIZE];

static void onComplete(App *app, Event *e)
{
    const EvButton *p = event_params(e, EvButton);
    app->complete = p->state == ekGUI_ON ? TRUE : FALSE;
}

static void onFilter(App *app, Event *e)
{
    const EvText *p = event_params(e, EvText);
    EvTextFilter *r = event_result(e, EvTextFilter);
    const char_t *edit = p->text;
    uint32_t pos = p->cpos;
    uint32_t bmatch_len = 0;

    if (!app->complete)
        return;

    textview_clear(app->text);
    if (!pos && p->len < 0)
    {
        r->text[0] = '\0';
        r->apply = TRUE;
        return;
    }

    bmatch_len = history_search(app->hist, cast(edit, byte_t), pos, bmatch, kTEXTFILTER_SIZE);
    if (bmatch_len)
    {
        textview_printf(app->text, "%.*s\n", bmatch_len, bmatch);
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

static void onChange(App *app, Event *e)
{
    const EvText *p = event_params(e, EvText);
    app->cpos = p->cpos;
}

static void onClick(App *app, Event *e)
{
    const char_t *edit = edit_get_text(app->edit);
    uint32_t len = app->cpos;
    textview_clear(app->text);
    if (edit && edit[0] != ' ' && history_append(app->hist, cast(edit, byte_t), len))
        textview_printf(app->text, "new: [%.*s]\n", len, edit);
    else
        textview_printf(app->text, "old: [%.*s]\n", len, edit);
    unref(e);
}

static void onClose(App *app, Event *e)
{
    osapp_finish();
    unref(app);
    unref(e);
}

static App *onCreate(void)
{
    App *app = heap_new0(App);
    Panel *panel = panel_scroll(TRUE, TRUE);
    Layout *pane = layout_create(1, 1);
    Layout *input = layout_create(1, 4);
    Edit *edit = edit_multiline();
    Button *add = button_push();
    Button *comp = button_check();
    TextView *text;
    Font *font = font_monospace(font_regular_size(), ekFNORMAL);

    draw2d_preferred_monospace("Noto Sans Mono");
    edit_font(edit, font);
    edit_OnFilter(edit, listener(app, onFilter, App));
    edit_OnChange(edit, listener(app, onChange, App));
    font_destroy(&font);
    layout_edit(input, edit, 0, 0);

    button_text(add, "add");
    button_OnClick(add, listener(app, onClick, App));
    layout_button(input, add, 0, 1);

    text = textview_create();
    textview_editable(text, FALSE);
    layout_textview(input, text, 0, 2);

    button_text(comp, "complete");
    button_state(comp, ekGUI_ON);
    button_OnClick(comp, listener(app, onComplete, App));
    layout_button(input, comp, 0, 3);

    layout_vexpand2(input, 0, 2, .5f);
    layout_layout(pane, input, 0, 0);
    layout_margin(pane, 5);
    panel_size(panel, s2df(1080, 720));
    panel_layout(panel, pane);

    app->edit = edit;
    app->text = text;
    app->hist = history_load();
    app->complete = TRUE;

    app->window = window_create(ekWINDOW_STDRES);
    window_panel(app->window, panel);
    window_title(app->window, "kutes");
    window_OnClose(app->window, listener(app, onClose, App));
    window_show(app->window);

    return app;
}

static void onDestroy(App **app)
{
    history_flush(&(*app)->hist);
    window_destroy(&(*app)->window);
    heap_delete(app, App);
}

#include <osapp/osmain.h>
osmain(onCreate, onDestroy, "", App)

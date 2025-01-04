#pragma once

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
#define READ_BUFFER (64 * 1024)
#define MAX_OUT_SIZE (512 * 1024)
#define MAX_ERR_SIZE (256 * 1024)
#define INITIAL_PARSE_SIZE (4 * 1024 * 1024)
#define INCR_PARSE_SIZE (2 * 1024 * 1024)

typedef enum _run_t
{
    ktRUN_UNKNOWN,
    ktRUN_INPROGRESS,
    ktRUN_COMPLETE,
    ktRUN_CANCEL,
    ktRUN_ENDED
} run_t;

typedef struct _closure_t Closure;
typedef void (*FPtr_closure)(const Closure *context);
#define FUNC_CHECK_CLOSURE(func, type) \
    (void)((void (*)(const type *))func == func)
struct _closure_t
{
    void *data;
    FPtr_destroy func_destroy;
    FPtr_closure func_closure;
};

typedef struct _app_t App;
struct _app_t
{
    /* data */
    byte_t read_buf[READ_BUFFER];
    byte_t *parse_buf;
    uint32_t parse_size;

    /* control */
    Window *window;
    Edit *cmdin;
    Button *run;
    PopUp *vselect;
    Layout *vscroll;
    ListBox *listb;
    Button *tail;
    Edit *search;
    Button *err_search;
    Button *noshow;
    Cell *nolimitb;
    TextView *cmdout;
    TextView *cmderr;
    Label *status;

    /* state */
    const char_t *start_ptr;
    ArrSt(uint32_t) *pos_cache;
    Proc *proc;
    uint32_t rsize;
    uint32_t cur_idx;
    uint32_t end_len;
    uint32_t pat_len;
    uint32_t out_len;
    uint32_t err_len;
    run_t run_state;
    bool_t stop;
    bool_t nolimit;

    /* child data */
    Closure *cls;
    yyjson_alc *alc;
};

/*---------------------------------------------------------------------------*/

void populate_listbox(App *, Layout *, const char_t *, uint32_t);
void adjust_vscroll(Layout *, const uint32_t, const uint32_t);
void cols_bind(void);

yyjson_alc *alc_init(const char_t *name);
void alc_dest(yyjson_alc **alc);

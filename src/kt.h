#pragma once

#include <boron.h>
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

typedef struct _destroyer_t Destroyer;
typedef void (*FPtr_closure)(const Destroyer *context);
#define FUNC_CHECK_CLOSURE(func, type) \
    (void)((void (*)(const type *))func == func)
struct _destroyer_t
{
    void *data;
    FPtr_destroy func_destroy;
    FPtr_closure func_closure;
};
DeclPt(Destroyer);

typedef struct _inops_t opsv;
typedef struct _app_t App;
struct _app_t
{
    /* data */
    byte_t read_buf[READ_BUFFER];
    byte_t *parse_buf;
    uint32_t parse_size;
    S2Df dsize;
    UThread *uthread;

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
    opsv *locker;

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
    yyjson_mut_doc *doc;
    yyjson_alc *alc;

    ArrPt(Destroyer) *views;
};

/*
    TODO: need an explicit null?
*/
typedef enum _type_t
{
    ktUNK = 0,
    ktSTR = 1,
    ktINT = 2,
    ktBOOL = 3,
    ktNUM = 4,

    /* derived types */
    ktTIM = 5,
    ktJVAL = 6
} KDataType;
DeclSt(KDataType);

extern char_t const *st_ready;
extern char_t const *st_running;
extern char_t const *st_stopping;
extern char_t const *st_stopped;
extern char_t const *st_completed;
extern char_t const *st_unknown;
extern char_t const *bt_run;
extern char_t const *bt_stop;

/*---------------------------------------------------------------------------*/
/* TODO: probably the worst api, relook */
void lock_view(opsv *, bool_t);
/* TODO: end */

/* TODO: populate_views should eventually accept a shared struct for all the views */
void populate_views(App *);
void adjust_vscroll(Layout *, const uint32_t selected, const uint32_t total);
void cols_bind(void);

yyjson_alc *alc_init(const char_t *name);
void alc_dest(yyjson_alc **alc);

/* related to boron evaluator */
UThread *uthread_create(void);
void uthread_destroy(UThread **);
void update_jroot(UThread *, yyjson_mut_val *);
KDataType boron_eval(UThread *, const char *, UCell **);
const char *bn_str(UThread *, UCell *);
int64_t bn_int(UCell *);
bool_t bn_bool(UCell *);
double bn_num(UCell *);
yyjson_mut_val *bn_jval(UThread *, UCell *);

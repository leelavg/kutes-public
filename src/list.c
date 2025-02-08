#include "kt.h"

#include <yyjson.h>
#include <nappgui.h>
#include <inet/json.h>

#include <time.h>

/* https://stackoverflow.com/a/17996915 */
#define QUOTE(...) #__VA_ARGS__
/* due to usage of 32 bitmask for headers and having limits doesn't hurt */
#define MAX_COLS 32
/* 65 is based on pod name length 64 + NULL byte */
#define TEMP_STR_LEN 65

/*---------------------------------------------------------------------------*/

typedef struct _column_t Column;
typedef struct _columns_t Columns;
typedef struct _tb_data_t Tbdata;
typedef struct _ft_data_t Ftdata;

struct _column_t
{
    String *display;
    String *expr;
    bool_t freeze;
};
DeclSt(Column);

struct _columns_t
{
    String *kind;
    ArrSt(Column) *cols;
};

DeclPt(yyjson_mut_val);
struct _tb_data_t
{
    char_t tempstr[TEMP_STR_LEN];
    Layout *ops;
    TableView *tbview;
    Edit *line;
    Label *status;
    yyjson_mut_val *items;
    yyjson_mut_doc *mdoc;
    ArrSt(uint32_t) *widths;
    ArrPt(String) *expr;
    ArrPt(String) *display;
    ArrSt(KDataType) *kttype;
    ArrPt(yyjson_mut_val) *ele;
    yyjson_mut_doc *wdoc;
    yyjson_mut_val *wroot;
    byte_t *rowbuf;
    UThread *uthread;
    RegEx *iso8601;
    uint32_t ncols;
    uint32_t nrows;
    uint32_t hmask;
    uint32_t freeze;
    real32_t font_width;
    bool_t invalid;
};

struct _ft_data_t
{
    byte_t read_buf[READ_BUFFER];
    Edit *cmdin;
    Button *jptr;
    Button *run;
    TextView *tview;
    Label *status;
    Proc *proc;
    uint32_t rsize;
    run_t run_state;
    bool_t stop;
    uint32_t tot_len;
    UThread *uthread;
    opsv *locker;
    yyjson_mut_doc *mdoc;
    yyjson_alc *alc;
};

/*---------------------------------------------------------------------------*/

static const char_t cjson[] = QUOTE(

    {
        "kind" : "Pod",
        "cols" :
            [
                {
                    "display" : "name",
                    "expr" : "/metadata/name",
                    "freeze" : true
                },
                {
                    "display" : "age",
                    "expr" : "/metadata/creationTimestamp"
                },
                {
                    "display" : "count",
                    "expr" : "/spec/containers"
                },
                {
                    "display" : "ready",
                    "expr" : "ait: jait {/status/containerStatuses} nxt: janv ait r: 0 t: 0 while [value? 'nxt][if jval jptr/root {/ready} nxt [++ r] ++ t nxt: janv ait] join r ['/' t]"
                },
                {
                    "display" : "status",
                    "expr" : "/status/phase"
                },
                {
                    "display" : "ns",
                    "expr" : "/metadata/namespace"
                },
                {
                    "display" : "now",
                    "expr" : "now"
                }

            ]
    }

);

static const char_t *info = "\
command is stopped if stdout/stderr exceeds 64 KiB.\n\n\
if jsonpointer is checked, inprocess jsonpointer path search or expression evaluation is performed (faster).\n\n\
suggested external json filtering programs are jq (C), dasel (Go), jsonquerylang (Javascript/Python).\n\n\
UI will freeze if your first command (like 'sleep') doesn't drain it's stdin.\
";

/*---------------------------------------------------------------------------*/

void cols_bind(void)
{
    dbind(Column, String *, display);
    dbind(Column, String *, expr);
    dbind(Column, bool_t, freeze);
    dbind(Columns, String *, kind);
    dbind(Columns, ArrSt(Column) *, cols);
}

/*---------------------------------------------------------------------------*/

/* from https://stackoverflow.com/a/58037981 */
__PURE ___INLINE int days_from_epoch(int y, int m, int d)
{
    int era, yoe, doy, doe;
    y -= m <= 2;
    era = y / 400;
    yoe = y - era * 400;                                  /* [0, 399] */
    doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1; /* [0, 365] */
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;          /* [0, 146096] */
    return era * 146097 + doe - 719468;
}

static ___INLINE time_t ktimegm(struct tm const *t)
{
    int days_since_1970;
    int year = t->tm_year + 1900;
    int month = t->tm_mon; /* 0-11 */
    if (month > 11)
    {
        year += month / 12;
        month %= 12;
    }
    else if (month < 0)
    {
        int years_diff = (11 - month) / 12;
        year -= years_diff;
        month += 12 * years_diff;
    }
    days_since_1970 = days_from_epoch(year, month + 1, t->tm_mday);

    return 60 * (60 * (24L * days_since_1970 + t->tm_hour) + t->tm_min) + t->tm_sec;
}

/*---------------------------------------------------------------------------*/

static uint32_t human_duration(const char_t *then, time_t now_s, char_t *buf, uint8_t size)
{
    struct tm then_br;
    time_t then_s;
    sscanf(then, "%d-%d-%dT%d:%d:%dZ",
           &then_br.tm_year, &then_br.tm_mon, &then_br.tm_mday,
           &then_br.tm_hour, &then_br.tm_min, &then_br.tm_sec);
    then_br.tm_year -= 1900;
    then_br.tm_mon -= 1;
    then_br.tm_isdst = -1;

    then_s = ktimegm(&then_br);
    {
        /* from k8s.io/apimachinery/pkg/util/duration */
        /* TODO: refactor later */
        uint64_t seconds = (uint64_t)difftime(now_s, then_s);
        uint32_t minutes, hours;

        if (seconds == 0)
            return bstd_sprintf(buf, size, "0s");
        else if (seconds < 60 * 2)
            return bstd_sprintf(buf, size, "%lds", seconds);

        minutes = seconds / 60;
        if (minutes < 10)
        {
            uint32_t s = seconds % 60;
            if (s == 0)
                return bstd_sprintf(buf, size, "%dm", minutes);
            return bstd_sprintf(buf, size, "%dm%ds", minutes, s);
        }
        else if (minutes < 60 * 3)
            return bstd_sprintf(buf, size, "%dm", minutes);

        hours = seconds / (60 * 60);
        if (hours < 8)
        {
            uint32_t m = (seconds / 60) % 60;
            if (m == 0)
                return bstd_sprintf(buf, size, "%dh", hours);
            return bstd_sprintf(buf, size, "%dh%dm", hours, m);
        }
        else if (hours < 48)
            return bstd_sprintf(buf, size, "%dh", hours);
        else if (hours < 24 * 8)
        {
            uint32_t h = hours % 24;
            if (h == 0)
                return bstd_sprintf(buf, size, "%dd", hours / 24);
            return bstd_sprintf(buf, size, "%dd%dh", hours / 24, h);
        }
        else if (hours < 24 * 365 * 2)
            return bstd_sprintf(buf, size, "%dd", hours / 24);
        else if (hours < 24 * 365 * 8)
        {
            uint32_t dy = (hours / 24) % 365;
            if (dy == 0)
                return bstd_sprintf(buf, size, "%dy", hours / 24 / 365);
            return bstd_sprintf(buf, size, "%dy%dd", hours / 24 / 365, dy);
        }

        return bstd_sprintf(buf, size, "%dy", hours / 24 / 365);
    }
}

/*---------------------------------------------------------------------------*/

static void tb_destroy(Tbdata **data)
{
    arrst_destroy(&(*data)->widths, NULL, uint32_t);
    arrpt_destroy(&(*data)->ele, NULL, yyjson_mut_val);
    arrpt_destroy(&(*data)->expr, str_destroy, String);
    arrpt_destroy(&(*data)->display, str_destroy, String);
    arrst_destroy(&(*data)->kttype, NULL, KDataType);
    heap_delete_n(&(*data)->rowbuf, ((TEMP_STR_LEN + 1) * MAX_COLS), byte_t);
    regex_destroy(&(*data)->iso8601);
    if ((*data)->wdoc)
        yyjson_mut_doc_free((*data)->wdoc);
    heap_delete(data, Tbdata);
}

/*---------------------------------------------------------------------------*/

static void tb_destroy_clr(const Destroyer *context)
{
    Tbdata *data = cast(context->data, Tbdata);
    context->func_destroy(dcast(&data, void));
}

/*---------------------------------------------------------------------------*/

static Tbdata *tb_create_destroy(Destroyer **destr, yyjson_alc *alc)
{
    Tbdata *data = heap_new0(Tbdata);
    data->widths = arrst_create(uint32_t);
    data->ele = arrpt_create(yyjson_mut_val);
    data->expr = arrpt_create(String);
    data->display = arrpt_create(String);
    data->kttype = arrst_create(KDataType);
    data->rowbuf = heap_new_n((TEMP_STR_LEN + 1) * MAX_COLS, byte_t);
    data->wdoc = yyjson_mut_doc_new(alc);
    data->wroot = yyjson_mut_obj(data->wdoc);
    /* no validation only for matching */
    data->iso8601 = regex_create("20[0-9][0-9]\\-[0-1][0-9]\\-[0-3][0-9]T[0-2][0-9]:[0-5][0-9]:[0-5][0-9]Z");

    *destr = heap_new(Destroyer);
    FUNC_CHECK_DESTROY(tb_destroy, Tbdata);
    FUNC_CHECK_CLOSURE(tb_destroy_clr, Destroyer);
    (*destr)->data = data;
    (*destr)->func_destroy = (FPtr_destroy)tb_destroy;
    (*destr)->func_closure = (FPtr_closure)tb_destroy_clr;

    return data;
}

/*---------------------------------------------------------------------------*/

static void nodata_destroy(Destroyer **destr)
{
    *destr = heap_new0(Destroyer);
}

/*---------------------------------------------------------------------------*/

void adjust_vscroll(Layout *vscroll, const uint32_t selected, const uint32_t total)
{
    uint32_t current;
    for (current = 0; current < total; current++)
    {
        if (current == selected)
        {
            layout_show_row(vscroll, current, TRUE);
            layout_vexpand(vscroll, current);
        }
        else
            layout_show_row(vscroll, current, FALSE);
        /* observed that without update the edit box component is loosing its shape*/
        layout_update(vscroll);
    }
}

/*---------------------------------------------------------------------------*/

static void onCol_add(Tbdata *data, Event *e)
{
    /* TODO: validations */
    if (data->ncols == MAX_COLS)
    {
        bstd_sprintf(data->tempstr, TEMP_STR_LEN, "+ max columns (%d) exceeded", MAX_COLS);
        label_text(data->status, data->tempstr);
        log_printf("columns more than %d are dropped", MAX_COLS);
        return;
    }
    else
    {
        Layout *add_col = layout_get_layout(data->ops, 0, 0);
        Edit *disp_name = layout_get_edit(add_col, 0, 0);
        Edit *json_expr = layout_get_edit(add_col, 1, 0);
        const char_t *name = edit_get_text(disp_name);
        const char_t *expr = edit_get_text(json_expr);
        const uint32_t col_id = data->ncols;
        uint32_t hlen = 0;
        if (!(name && name[0] && expr && expr[0]))
            return;
        hlen = bstd_sprintf(data->tempstr, TEMP_STR_LEN, "%s", name);
        arrpt_append(data->display, str_c(data->tempstr), String);
        arrpt_append(data->expr, str_c(expr), String);
        tableview_header_title(data->tbview,
                               tableview_new_column_text(data->tbview),
                               data->tempstr);
        arrst_append(data->widths, min_u32(hlen, TEMP_STR_LEN), uint32_t);
        arrst_append(data->widths, 0, uint32_t);
        tableview_column_limits(data->tbview, data->ncols,
                                data->font_width * *arrst_get(data->widths, data->ncols * 2, uint32_t),
                                data->font_width * (TEMP_STR_LEN + 1));
        data->ncols++;
        data->invalid = TRUE;

        {
            Layout *rem_col = layout_get_layout(data->ops, 0, 1);
            PopUp *col_name = layout_get_popup(rem_col, 0, 0);
            bstd_sprintf(data->tempstr, TEMP_STR_LEN, "[%d] %s", col_id, name);
            popup_add_elem(col_name, data->tempstr, NULL);
        }

        edit_text(disp_name, "");
        edit_text(json_expr, "");
    }
    unref(e);
}

/*---------------------------------------------------------------------------*/

static void onCol_rem(Tbdata *data, Event *e)
{
    Layout *rem_col = layout_get_layout(data->ops, 0, 1);
    PopUp *col_name = layout_get_popup(rem_col, 0, 0);
    uint32_t selected = popup_get_selected(col_name);
    /* TODO: not enforce atleast a single column? */
    if (selected && popup_count(col_name) > 2)
    {
        arrpt_delete(data->display, selected - 1, str_destroy, String);
        arrpt_delete(data->expr, selected - 1, str_destroy, String);
        arrst_delete(data->kttype, selected - 1, NULL, KDataType);
        arrst_delete(data->widths, 2 * (selected - 1), NULL, uint32_t); /* header */
        arrst_delete(data->widths, 2 * (selected - 1), NULL, uint32_t); /* row */
        data->ncols--;
        data->invalid = TRUE;
        tableview_remove_column(data->tbview, selected - 1);

        {
            uint32_t col_id = 0;
            popup_clear(col_name);
            popup_add_elem(col_name, "column", NULL);
            arrpt_foreach_const(name, data->display, String)
                bstd_sprintf(data->tempstr, TEMP_STR_LEN, "[%d] %s", col_id, tc(name));
                popup_add_elem(col_name, data->tempstr, NULL);
                col_id++;
            arrpt_end();
        }
    }
    unref(e);
}

/*---------------------------------------------------------------------------*/

static void onQuery_run(Tbdata *data, Event *e)
{
    Layout *query_col = layout_get_layout(data->ops, 0, 2);
    Edit *query_ppth = layout_get_edit(query_col, 0, 0);
    Edit *query_result = layout_get_edit(query_col, 2, 0);
    const char_t *jptr = edit_get_text(query_ppth);

    if (jptr && jptr[0])
    {
        uint32_t len = 0;
        if (jptr[0] == '/')
        {
            yyjson_mut_val *val = yyjson_mut_doc_ptr_get(data->mdoc, jptr);
            /* switch statement copied from yyjson_mut_get_type_desc function */
            switch (yyjson_mut_get_tag(val))
            {
            case YYJSON_TYPE_STR | YYJSON_SUBTYPE_NONE:
            case YYJSON_TYPE_STR | YYJSON_SUBTYPE_NOESC:
                len = bstd_sprintf(data->tempstr, TEMP_STR_LEN, "%s", yyjson_mut_get_str(val));
                break;
            case YYJSON_TYPE_NUM | YYJSON_SUBTYPE_UINT:
            case YYJSON_TYPE_NUM | YYJSON_SUBTYPE_SINT:
                len = bstd_sprintf(data->tempstr, TEMP_STR_LEN, "%s%ld", "int - ", yyjson_mut_get_sint(val));
                break;
            case YYJSON_TYPE_BOOL | YYJSON_SUBTYPE_TRUE:
                len = bstd_sprintf(data->tempstr, TEMP_STR_LEN, "%s", "bool - true");
                break;
            case YYJSON_TYPE_BOOL | YYJSON_SUBTYPE_FALSE:
                len = bstd_sprintf(data->tempstr, TEMP_STR_LEN, "%s", "bool - false");
                break;
            case YYJSON_TYPE_NUM | YYJSON_SUBTYPE_REAL:
                len = bstd_sprintf(data->tempstr, TEMP_STR_LEN, "%s%.2f", "real -", yyjson_mut_get_real(val));
                break;
            case YYJSON_TYPE_ARR | YYJSON_SUBTYPE_NONE:
                len = bstd_sprintf(data->tempstr, TEMP_STR_LEN, "%s%ld%s", "array - [", yyjson_mut_arr_size(val), "] item(s)");
                break;
            case YYJSON_TYPE_OBJ | YYJSON_SUBTYPE_NONE:
                len = bstd_sprintf(data->tempstr, TEMP_STR_LEN, "%s%ld%s", "object - [", yyjson_mut_obj_size(val), "] key-value pair(s)");
                break;
            case YYJSON_TYPE_RAW | YYJSON_SUBTYPE_NONE:
            case YYJSON_TYPE_NULL | YYJSON_SUBTYPE_NONE:
            default:
                len = bstd_sprintf(data->tempstr, TEMP_STR_LEN, "%s", "<unset>");
                break;
            }
        }
        else
        {
            UCell *vcell;
            update_jroot(data->uthread, yyjson_mut_doc_get_root(data->mdoc));
            switch (boron_eval(data->uthread, jptr, &vcell))
            {
            case ktTIM:
            case ktSTR:
                len = bstd_sprintf(data->tempstr, TEMP_STR_LEN, "%s", bn_str(data->uthread, vcell));
                break;
            case ktINT:
                len = bstd_sprintf(data->tempstr, TEMP_STR_LEN, "%ld", bn_int(vcell));
                break;
            case ktBOOL:
                len = bstd_sprintf(data->tempstr, TEMP_STR_LEN, "%s", bn_bool(vcell) ? "true" : "false");
                break;
            case ktNUM:
                len = bstd_sprintf(data->tempstr, TEMP_STR_LEN, "%.2f", bn_num(vcell));
                break;
            case ktJVAL:
                len = bstd_sprintf(data->tempstr, TEMP_STR_LEN, "%ld", yyjson_mut_get_len(bn_jval(data->uthread, vcell)));
                break;
            case ktUNK:
                len = bstd_sprintf(data->tempstr, TEMP_STR_LEN, "%s", "<unset>");
                break;
            }
        }
        if (len)
            edit_text(query_result, data->tempstr);
    }
    unref(e);
}

/*---------------------------------------------------------------------------*/

static void onRow_open(Tbdata *data, Event *e)
{
    log_printf("open row in new pane");
    unref(data);
    unref(e);
}

/*---------------------------------------------------------------------------*/

static ___INLINE yyjson_mut_val *get_tb_value(Tbdata *data, uint32_t row, uint32_t col)
{
    /*
    1D to 2D: pos = x + width*y
    2D to 1D: x = pos % width; y = pos / width;
    */
    return arrpt_get(data->ele, col + data->ncols * row, yyjson_mut_val);
}

/*---------------------------------------------------------------------------*/

static void tb_cache(Tbdata *data)
{
    size_t idx, max, wkey = 0;
    yyjson_mut_val *val;
    if (data->invalid)
    {
        /* TODO: optimize if there is a latency */
        arrpt_clear(data->ele, NULL, yyjson_mut_val);
        arrst_clear(data->kttype, NULL, KDataType);
        yyjson_mut_arr_foreach(data->items, idx, max, val)
        {
            KDataType val_type = ktUNK;
            arrpt_foreach_const(path, data->expr, String)
                const char_t *expr = tc(path);
                yyjson_mut_val *key, *res = NULL;
                if (expr && expr[0] == '/')
                {
                    res = yyjson_mut_ptr_get(val, tc(path));
                    switch (yyjson_mut_get_tag(res))
                    {
                    case YYJSON_TYPE_STR | YYJSON_SUBTYPE_NONE:
                    case YYJSON_TYPE_STR | YYJSON_SUBTYPE_NOESC:
                        val_type = ktSTR;
                        break;
                    case YYJSON_TYPE_NUM | YYJSON_SUBTYPE_UINT:
                    case YYJSON_TYPE_NUM | YYJSON_SUBTYPE_SINT:
                        val_type = ktINT;
                        break;
                    case YYJSON_TYPE_BOOL | YYJSON_SUBTYPE_TRUE:
                    case YYJSON_TYPE_BOOL | YYJSON_SUBTYPE_FALSE:
                        val_type = ktBOOL;
                        break;
                    case YYJSON_TYPE_NUM | YYJSON_SUBTYPE_REAL:
                        val_type = ktNUM;
                        break;
                    case YYJSON_TYPE_ARR | YYJSON_SUBTYPE_NONE:
                    case YYJSON_TYPE_OBJ | YYJSON_SUBTYPE_NONE:
                        val_type = ktJVAL;
                        break;
                    case YYJSON_TYPE_RAW | YYJSON_SUBTYPE_NONE:
                    case YYJSON_TYPE_NULL | YYJSON_SUBTYPE_NONE:
                    default:
                        val_type = ktUNK;
                        break;
                    }
                    arrpt_append(data->ele, res, yyjson_mut_val);
                }
                else
                {
                    UCell *vcell;
                    /* wkey is stringified element index */
                    bstd_sprintf(data->tempstr, TEMP_STR_LEN, "%ld", wkey);
                    key = yyjson_mut_strcpy(data->wdoc, data->tempstr);
                    update_jroot(data->uthread, val);
                    switch (boron_eval(data->uthread, expr, &vcell))
                    {
                    case ktTIM:
                    case ktSTR:
                        bstd_sprintf(data->tempstr, TEMP_STR_LEN, "%s", bn_str(data->uthread, vcell));
                        res = yyjson_mut_strcpy(data->wdoc, data->tempstr);
                        yyjson_mut_obj_add(data->wroot, key, res);
                        val_type = ktSTR;
                        break;
                    case ktINT:
                        res = yyjson_mut_int(data->wdoc, bn_int(vcell));
                        yyjson_mut_obj_add(data->wroot, key, res);
                        val_type = ktINT;
                        break;
                    case ktBOOL:
                        res = yyjson_mut_bool(data->wdoc, bn_bool(vcell));
                        yyjson_mut_obj_add(data->wroot, key, res);
                        val_type = ktBOOL;
                        break;
                    case ktNUM:
                        res = yyjson_mut_real(data->wdoc, bn_num(vcell));
                        yyjson_mut_obj_add(data->wroot, key, res);
                        val_type = ktNUM;
                        break;
                    case ktJVAL:
                        res = bn_jval(data->uthread, vcell);
                        yyjson_mut_obj_add(data->wroot, key, res);
                        val_type = ktJVAL;
                        break;
                    case ktUNK:
                        res = yyjson_mut_null(data->wdoc);
                        yyjson_mut_obj_add(data->wroot, key, res);
                        val_type = ktUNK;
                        break;
                    }
                    arrpt_append(data->ele, res, yyjson_mut_val);
                }

                if (res && val_type == ktSTR)
                {
                    uint32_t len = yyjson_mut_get_len(res);
                    if (len == blib_strlen("2001-02-13T14:15:16Z"))
                        if (regex_match(data->iso8601, yyjson_mut_get_str(res)))
                            val_type = ktTIM;
                }

                /* TODO: a value may not be available for first item, needs a fix */
                if (arrst_size(data->kttype, KDataType) < data->ncols)
                    arrst_append(data->kttype, val_type, KDataType);

                wkey++;
            arrpt_end()
        }
        data->invalid = FALSE;
    }
}

/*---------------------------------------------------------------------------*/

static void tb_OnHeader(Tbdata *data, Event *e)
{
    const EvButton *p = event_params(e, EvButton);
    /* TODO: this limits number of cols to 32 */
    data->hmask ^= 1 << p->index;
    unref(data);
}

static ___INLINE uint32_t fill_tempstr(Tbdata *data, uint32_t col, uint32_t row)
{
    const KDataType *kttype = arrst_get_const(data->kttype, col, KDataType);
    uint32_t len = 0;
    switch (*kttype)
    {
    case ktBOOL:
        len = bstd_sprintf(data->tempstr, TEMP_STR_LEN, "%s", yyjson_mut_get_bool(get_tb_value(data, row, col)) ? "true" : "false");
        break;
    case ktINT:
        len = bstd_sprintf(data->tempstr, TEMP_STR_LEN, "%ld", yyjson_mut_get_sint(get_tb_value(data, row, col)));
        break;
    case ktNUM:
        len = bstd_sprintf(data->tempstr, TEMP_STR_LEN, "%.2f", yyjson_mut_get_num(get_tb_value(data, row, col)));
        break;
    case ktSTR:
        len = bstd_sprintf(data->tempstr, TEMP_STR_LEN, "%s", yyjson_mut_get_str(get_tb_value(data, row, col)));
        break;
    case ktTIM:
    {
        const char_t *str = yyjson_mut_get_str(get_tb_value(data, row, col));
        if (data->hmask & (1 << col))
        {
            str_copy_c(data->tempstr, TEMP_STR_LEN, str);
            len = strlen(str);
        }
        else
            /* more likely */
            len = human_duration(str, time(NULL), data->tempstr, TEMP_STR_LEN);
        break;
    }
    case ktJVAL:
        len = bstd_sprintf(data->tempstr, TEMP_STR_LEN, "%ld", yyjson_mut_get_len(get_tb_value(data, row, col)));
        break;
    case ktUNK:
        len = bstd_sprintf(data->tempstr, TEMP_STR_LEN, "%s", "<unset>");
        break;
    }
    return len > TEMP_STR_LEN ? TEMP_STR_LEN : len;
}

/*---------------------------------------------------------------------------*/

static void tb_OnData(Tbdata *data, Event *e)
{
    /*
        TODO: this is the tightest function as data is called for a refresh after
        every possible view update and lookout for any optimizations.
    */
    uint32_t etype = event_type(e);
    switch (etype)
    {
    case ekGUI_EVENT_TBL_NROWS:
    {
        uint32_t *n = event_result(e, uint32_t);
        *n = data->nrows;
        break;
    }
    case ekGUI_EVENT_TBL_BEGIN:
    {
        const EvTbRect *rect = event_params(e, EvTbRect);
        /* TODO: revisit if processing all the json takes time and only process visible table */
        tb_cache(data);
        unref(rect);
        break;
    }
    case ekGUI_EVENT_TBL_CELL:
    {
        const EvTbPos *pos = event_params(e, EvTbPos);
        EvTbCell *cell = event_result(e, EvTbCell);
        uint32_t len = fill_tempstr(data, pos->col, pos->row);
        uint32_t *cell_len = arrst_get(data->widths, 2 * pos->col + 1, uint32_t);
        if (*cell_len < len)
            *cell_len = len;
        cell->text = data->tempstr;
        break;
    }
    case ekGUI_EVENT_TBL_END:
    {
        const uint32_t row = tableview_get_focus_row(data->tbview);
        if (row != UINT32_MAX)
        {
            uint32_t col;
            uint32_t len = 0;
            uint32_t next = 0;
            for (col = 0; col < data->ncols; col++)
            {
                len = fill_tempstr(data, col, row);
                bmem_copy(data->rowbuf + (next * sizeof(byte_t)), cast(data->tempstr, byte_t), len);
                next += len;
                bmem_set1(data->rowbuf + (next * sizeof(byte_t)), 1, ' ');
                next++;
            }
            if (next)
            {
                bmem_set_zero(data->rowbuf + ((next - 1) * sizeof(char_t)), 1);
                edit_text(data->line, cast(data->rowbuf, char_t));
            }
        }
        {
            /* gives table a compact look, slightly collapses larger cells and relaxes shorter cells */
            uint32_t *width = arrst_all(data->widths, uint32_t);
            uint32_t i = 0, extra = 0, len = 0;
            real32_t fwidth = 0;
            for (i = 0; i < data->ncols; i++)
            {
                extra = 0;
                fwidth = data->font_width;
                len = max_u32(width[2 * i], width[2 * i + 1]);
                /* these calculations are being done here as there is no callback for overlay,
                drawing after headers would've reduced most of these magic numbers. */
                if (len < TEMP_STR_LEN && width[2 * i] + 4 > width[2 * i + 1])
                    /* preserve shorter cells, ensure row content is higher than header */
                    extra = 3;
                else if (width[2 * i + 1] > TEMP_STR_LEN * 2 / 3)
                    /* collapses larger cells, on my system regular font size is ~12% higher, reducing by
                    that amount until a legit fix if found. */
                    fwidth *= .89f;
                tableview_column_width(data->tbview, i, fwidth * (len + extra));
                width[2 * i + 1] = 0;
            }
        }
        break;
    }
        cassert_default();
    }
}

/*---------------------------------------------------------------------------*/

static Destroyer *add_list_to_layout(UThread *ut, PopUp *pop, Layout *vscroll, yyjson_mut_doc *doc, Label *status, yyjson_alc *alc)
{
    yyjson_mut_val *items = yyjson_mut_doc_ptr_get(doc, "/items");
    yyjson_mut_val *first = yyjson_mut_ptr_get(items, "/0/kind");
    const char_t *kind;
    Destroyer *destr = NULL;
    if (first)
        kind = yyjson_mut_get_str(first);
    else
        return destr;

    {
        Stream *stm = stm_from_block(cast(cjson, byte_t), sizeof(cjson));
        if (stm != NULL)
        {
            Columns *json = json_read(stm, NULL, Columns);
            if (json != NULL && !blib_strcmp(kind, tc(json->kind)))
            {
                Tbdata *data;
                uint32_t hlen;
                uint32_t vscroll_ridx = popup_count(pop);
                Layout *table = layout_create(1, 2);
                Layout *ops = layout_create(1, 4);
                Layout *add_col = layout_create(3, 1);
                Layout *rem_col = layout_create(2, 1);
                Layout *query_col = layout_create(3, 1);
                Layout *status_row = layout_create(2, 1);

                Edit *disp_name = edit_create();
                Edit *json_ppth = edit_create();
                Button *col_add = button_push();

                PopUp *col_name = popup_create();
                Button *col_rem = button_push();

                Edit *query_ppth = edit_create();
                Button *query_run = button_push();
                Edit *query_result = edit_create();

                Edit *line = edit_create();
                Button *open = button_push();

                Font *font = font_system(font_regular_size(), 0);
                TableView *tbview = tableview_create();

                edit_phstyle(disp_name, ekFITALIC);
                edit_phtext(disp_name, "display name");
                layout_edit(add_col, disp_name, 0, 0);

                edit_phstyle(json_ppth, ekFITALIC);
                edit_phtext(json_ppth, "json pointer path or expression");
                layout_edit(add_col, json_ppth, 1, 0);
                layout_hexpand(add_col, 1);

                button_text(col_add, "col add");
                layout_button(add_col, col_add, 2, 0);

                layout_layout(ops, add_col, 0, 0);

                popup_add_elem(col_name, "column", NULL);
                layout_popup(rem_col, col_name, 0, 0);
                layout_hexpand(rem_col, 0);

                button_text(col_rem, "col rem");
                layout_button(rem_col, col_rem, 1, 0);

                layout_layout(ops, rem_col, 0, 1);

                edit_phstyle(query_ppth, ekFITALIC);
                edit_phtext(query_ppth, "json pointer path from root (ex: /items/0/kind) or expression");
                layout_edit(query_col, query_ppth, 0, 0);

                button_text(query_run, "query");
                layout_button(query_col, query_run, 1, 0);

                edit_editable(query_result, FALSE);
                layout_edit(query_col, query_result, 2, 0);

                layout_hexpand2(query_col, 0, 2, .5f);
                layout_layout(ops, query_col, 0, 2);

                edit_editable(line, FALSE);
                edit_vpadding(line, 0);
                layout_edit(status_row, line, 0, 0);

                button_text(open, "open");
                layout_button(status_row, open, 1, 0);

                layout_hexpand(status_row, 0);
                layout_layout(ops, status_row, 0, 3);
                layout_vsize(ops, 3, 25);

                layout_layout(table, ops, 0, 0);

                layout_tableview(table, tbview, 0, 1);
                layout_vexpand(table, 1);

                layout_insert_row(vscroll, vscroll_ridx);
                layout_layout(vscroll, table, 0, vscroll_ridx);

                data = tb_create_destroy(&destr, alc);
                popup_add_elem(pop, "table", NULL);
                data->mdoc = doc;
                data->items = items;
                data->nrows = yyjson_mut_arr_size(items);
                data->freeze = 0;
                data->status = status;
                data->font_width = font_width(font);
                font_destroy(&font);
                arrst_foreach_const(col, json->cols, Column)
                    if (data->ncols == MAX_COLS)
                    {
                        bstd_sprintf(data->tempstr, TEMP_STR_LEN, "+ max columns (%d) exceeded", MAX_COLS);
                        label_text(data->status, data->tempstr);
                        log_printf("columns more than %d are dropped", MAX_COLS);
                        break;
                    }

                    hlen = max_u32(bstd_sprintf(data->tempstr, TEMP_STR_LEN, "%s", tc(col->display)), 1);
                    tableview_header_title(tbview,
                                           tableview_new_column_text(tbview),
                                           data->tempstr);
                    arrpt_append(data->expr, str_copy(col->expr), String);
                    arrpt_append(data->display, str_copy(col->display), String);

                    bstd_sprintf(data->tempstr, TEMP_STR_LEN, "[%d] %s", data->ncols, tc(col->display));
                    popup_add_elem(col_name, data->tempstr, NULL);

                    arrst_append(data->widths, min_u32(hlen, TEMP_STR_LEN), uint32_t);
                    arrst_append(data->widths, 0, uint32_t);
                    tableview_column_limits(tbview, data->ncols,
                                            data->font_width * *arrst_get(data->widths, data->ncols * 2, uint32_t),
                                            data->font_width * (TEMP_STR_LEN + 1));

                    data->freeze = col->freeze ? data->ncols : data->freeze;

                    data->ncols++;
                arrst_end()

                tableview_OnData(tbview, listener(data, tb_OnData, Tbdata));
                tableview_OnHeaderClick(tbview, listener(data, tb_OnHeader, Tbdata));
                tableview_header_resizable(tbview, FALSE);
                tableview_column_freeze(tbview, data->freeze);
                tableview_header_clickable(tbview, TRUE);

                button_OnClick(col_add, listener(data, onCol_add, Tbdata));
                button_OnClick(col_rem, listener(data, onCol_rem, Tbdata));
                button_OnClick(query_run, listener(data, onQuery_run, Tbdata));
                button_OnClick(open, listener(data, onRow_open, Tbdata));

                data->ops = ops;
                data->tbview = tbview;
                data->invalid = TRUE;
                data->line = line;
                data->uthread = ut;

                tableview_update(tbview);
            }
            json_destroy(&json, Columns);
            stm_close(&stm);
        }
    }
    return destr;
}

/*---------------------------------------------------------------------------*/

static Destroyer *add_kind_to_layout(PopUp *pop, Layout *vscroll, yyjson_mut_doc *doc)
{
    TextView *tview;
    uint32_t vscroll_ridx = popup_count(pop);
    /* TODO: should check for full GVK */
    yyjson_mut_val *first = yyjson_mut_doc_ptr_get(doc, "/kind");
    const char_t *kind;
    Destroyer *destr;
    nodata_destroy(&destr);
    if (first)
        kind = yyjson_mut_get_str(first);
    else
        return destr;

    tview = textview_create();
    popup_add_elem(pop, "owner", NULL);
    textview_writef(tview, kind);
    layout_insert_row(vscroll, vscroll_ridx);
    layout_textview(vscroll, tview, 0, vscroll_ridx);
    return destr;
}

/*---------------------------------------------------------------------------*/

static void ft_destroy(Ftdata **data)
{
    /* just a guard */
    if ((*data)->proc)
    {
        bproc_cancel((*data)->proc);
        bproc_close(&(*data)->proc);
        cassert_msg(FALSE, "did not expect a process to be running");
    }
    heap_delete(data, Ftdata);
}

/*---------------------------------------------------------------------------*/

static void ft_destroy_clr(const Destroyer *context)
{
    Ftdata *data = cast(context->data, Ftdata);
    context->func_destroy(dcast(&data, void));
}

/*---------------------------------------------------------------------------*/

static Ftdata *ft_create_destroy(Destroyer **destr)
{
    Ftdata *data = heap_new0(Ftdata);
    *destr = heap_new(Destroyer);
    FUNC_CHECK_DESTROY(ft_destroy, Ftdata);
    FUNC_CHECK_CLOSURE(ft_destroy_clr, Destroyer);
    (*destr)->data = data;
    (*destr)->func_destroy = (FPtr_destroy)ft_destroy;
    (*destr)->func_closure = (FPtr_closure)ft_destroy_clr;
    return data;
}

/*---------------------------------------------------------------------------*/
static uint32_t bg_proc_main(Ftdata *data)
{
    bproc_wait_exit(&data->proc);
    return 0;
}

/*---------------------------------------------------------------------------*/

static void i_run_update(Ftdata *data)
{
    perror_t out, err;
    bool_t read0 = TRUE;
    if (data->run_state != ktRUN_INPROGRESS)
    {
        return;
    }
    else if (data->stop)
    {
        bproc_cancel(data->proc);
        bproc_close(&data->proc);
        data->run_state = ktRUN_CANCEL;
        return;
    }

    if (bproc_read(data->proc, data->read_buf, READ_BUFFER - 1, &data->rsize, &out))
    {
        data->read_buf[data->rsize] = '\0';
        data->tot_len += data->rsize;
        textview_color(data->tview, kCOLOR_DEFAULT);
        textview_writef(data->tview, cast(data->read_buf, char_t));
        read0 = FALSE;
    }

    if (bproc_eread(data->proc, data->read_buf, READ_BUFFER - 1, &data->rsize, &err))
    {
        data->read_buf[data->rsize] = '\0';
        data->tot_len += data->rsize;
        textview_color(data->tview, kCOLOR_RED);
        textview_writef(data->tview, cast(data->read_buf, char_t));
        read0 = FALSE;
    }

    if (read0 && out != ekPAGAIN && err != ekPAGAIN)
    {
        bproc_close(&data->proc);
        data->run_state = ktRUN_COMPLETE;
    }
    else if (data->tot_len > READ_BUFFER)
    {
        data->stop = TRUE;
    }
}

/*---------------------------------------------------------------------------*/

static void i_run_end(Ftdata *data, const uint32_t rval)
{
    /* reset state */
    edit_editable(data->cmdin, TRUE);
    button_text(data->run, bt_run);
    data->stop = FALSE;
    if (data->run_state == ktRUN_COMPLETE)
    {
        label_text(data->status, st_completed);
    }
    else if (data->run_state == ktRUN_CANCEL)
    {
        label_text(data->status, st_stopped);
    }
    else
    {
        label_text(data->status, st_unknown);
    }
    data->run_state = ktRUN_ENDED;
    lock_view(data->locker, FALSE);
    unref(rval);
}

/*---------------------------------------------------------------------------*/

static void i_OnRun(Ftdata *data, Event *e)
{
    const char_t *cmdin;
    char_t *json = NULL;
    size_t len = 0;
    yyjson_ptr_err perr;
    yyjson_write_err werr;

    unref(e);
    if (data->run_state != ktRUN_ENDED)
    {
        data->stop = TRUE;
        label_text(data->status, st_stopping);
        return;
    }

    cmdin = edit_get_text(data->cmdin);
    data->tot_len = 0;
    textview_color(data->tview, kCOLOR_DEFAULT);
    textview_clear(data->tview);
    label_text(data->status, st_ready);

    if (!(cmdin && cmdin[0]))
    {
        return;
    }

    label_text(data->status, st_running);
    if (button_get_state(data->jptr) == ekGUI_ON)
    {
        /* in process evaluation */
        yyjson_mut_val *jptr = NULL;
        if (cmdin[0] != '/')
        {
/* U64 has 20 chars */
#define U64_LEN 32
            /* TODO: refactor, seems a bit ugly due to multiple exit points */
            UCell *vcell;
            char_t tempstr[U64_LEN];
            update_jroot(data->uthread, yyjson_mut_doc_get_root(data->mdoc));
            switch (boron_eval(data->uthread, cmdin, &vcell))
            {
            case ktTIM:
            case ktSTR:
                textview_writef(data->tview, bn_str(data->uthread, vcell));
                label_text(data->status, st_completed);
                return;
            case ktINT:
                len = bstd_sprintf(tempstr, U64_LEN, "%ld", bn_int(vcell));
                break;
            case ktBOOL:
                len = bstd_sprintf(tempstr, U64_LEN, "%s", bn_bool(vcell) ? "true" : "false");
                break;
            case ktNUM:
                len = bstd_sprintf(tempstr, U64_LEN, "%.2f", bn_num(vcell));
                break;
            case ktJVAL:
                jptr = bn_jval(data->uthread, vcell);
                perr.code = YYJSON_PTR_ERR_NONE;
                break;
            case ktUNK:
                len = bstd_sprintf(tempstr, U64_LEN, "%s", "<unset>");
                break;
            }
#undef U64_LEN
            if (len)
            {
                textview_writef(data->tview, tempstr);
                label_text(data->status, st_completed);
                return;
            }
        }

        if (!jptr)
            jptr = yyjson_mut_doc_ptr_getx(data->mdoc, cmdin, blib_strlen(cmdin), NULL, &perr);

        /* TODO: recheck if we need to guard interface elements as below might not probably be slow vs running a new process */
        if (perr.code == YYJSON_PTR_ERR_NONE)
        {
            json = yyjson_mut_val_write_opts(jptr, YYJSON_WRITE_PRETTY_TWO_SPACES, data->alc, &len, &werr);
            if (werr.code == YYJSON_WRITE_SUCCESS)
            {
                if (len > READ_BUFFER)
                {
                    json[READ_BUFFER] = '\0';
                    label_text(data->status, st_stopped);
                }
                else
                {
                    label_text(data->status, st_completed);
                }
                textview_writef(data->tview, json);
            }
        }
        else
        {
            textview_color(data->tview, kCOLOR_RED);
            textview_writef(data->tview, perr.msg);
            label_text(data->status, st_stopped);
        }
    }
    else
    {
        /* external command */
        lock_view(data->locker, TRUE);
        /* TODO: track if the buffer is changed or not */
        json = yyjson_mut_write_opts(data->mdoc, YYJSON_WRITE_NOFLAG, data->alc, &len, &werr);
        cassert_msg(werr.code == 0, "this is a parsed buf and shouldn't fail as size should be within limits");

        data->proc = bproc_exec(cmdin, NULL);
        {
            uint32_t times = len / (READ_BUFFER - 1);
            uint32_t remain = len % (READ_BUFFER - 1);
            bool_t stop_write = FALSE;
            while (times--)
            {
                if (bproc_write(data->proc, cast(json, byte_t), READ_BUFFER - 1, NULL, NULL) == FALSE)
                {
                    stop_write = TRUE;
                    break;
                }
                json = json + (sizeof(char_t) * (READ_BUFFER - 1));
            }
            if (!stop_write && remain)
                bproc_write(data->proc, cast(json, byte_t), remain, NULL, NULL);
        }
        data->run_state = ktRUN_INPROGRESS;
        bproc_write_close(data->proc);
        edit_editable(data->cmdin, FALSE);
        button_text(data->run, bt_stop);
        osapp_task(data, 0., bg_proc_main, i_run_update, i_run_end, Ftdata);
    }
}

/*---------------------------------------------------------------------------*/

static Destroyer *add_filter_to_layout(UThread *ut, PopUp *pop, Layout *vscroll, opsv *locker, yyjson_mut_doc *mdoc, byte_t *buf, uint32_t bsize, Label *status)
{
    Ftdata *data;
    Destroyer *destr;
    uint32_t vscroll_ridx = popup_count(pop);

    Layout *filter = layout_create(1, 2);
    Layout *cmd = layout_create(2, 1);
    Layout *ops = layout_create(1, 2);

    Edit *cmdin = edit_multiline();

    Button *run = button_push();
    Button *jptr = button_check();

    TextView *tview = textview_create();

    edit_phstyle(cmdin, ekFITALIC);
    edit_phtext(cmdin, "compacted json will be supplied to stdin of this command.");
    layout_edit(cmd, cmdin, 0, 0);

    button_text(run, bt_run);
    layout_button(ops, run, 0, 0);

    button_text(jptr, "jsonpointer");
    layout_button(ops, jptr, 0, 1);

    layout_hexpand(cmd, 0);
    layout_layout(cmd, ops, 1, 0);
    layout_layout(filter, cmd, 0, 0);

    layout_vsize(filter, 0, 50);

    textview_show_select(tview, TRUE);
    textview_wrap(tview, TRUE);
    textview_fstyle(tview, ekFITALIC);
    textview_writef(tview, info);
    textview_fstyle(tview, ekFNORMAL);
    layout_textview(filter, tview, 0, 1);
    layout_vexpand(filter, 1);

    layout_insert_row(vscroll, vscroll_ridx);
    layout_layout(vscroll, filter, 0, vscroll_ridx);
    popup_add_elem(pop, "filter", NULL);

    data = ft_create_destroy(&destr);
    data->cmdin = cmdin;
    data->jptr = jptr;
    data->run = run;
    data->tview = tview;
    data->run_state = ktRUN_ENDED;
    data->locker = locker;
    data->mdoc = mdoc;
    data->status = status;
    data->uthread = ut;
    /* TODO: no need to free explicitly? */
    yyjson_alc_pool_init(data->alc, buf, bsize);

    button_OnClick(data->run, listener(data, i_OnRun, Ftdata));

    return destr;
}

/*---------------------------------------------------------------------------*/

void populate_views(App *app)
{
    yyjson_doc *doc = yyjson_read_opts(cast(app->parse_buf, char_t), app->out_len, YYJSON_READ_INSITU, app->alc, NULL);
    if (doc)
    {
        yyjson_val *kind = yyjson_doc_ptr_get(doc, "/kind");
        Destroyer *destr;
        if (kind)
        {
            yyjson_mut_doc *mdoc = yyjson_doc_mut_copy(doc, app->alc);
            /* TODO: move the params to a shared struct after some point, let's say after 7 params? */
            if (!blib_strcmp(yyjson_get_str(kind), "List"))
            {
                destr = add_list_to_layout(app->uthread, app->vselect, app->vscroll, mdoc, app->status, app->alc);
                cassert_no_null(destr);
                arrpt_append(app->views, destr, Destroyer);
            }
            else
            {
                destr = add_kind_to_layout(app->vselect, app->vscroll, mdoc);
                cassert_no_null(destr);
                arrpt_append(app->views, destr, Destroyer);
            }
            destr = add_filter_to_layout(app->uthread, app->vselect, app->vscroll, app->locker, mdoc, app->parse_buf, app->parse_size, app->status);
            cassert_no_null(destr);
            arrpt_append(app->views, destr, Destroyer);
            app->doc = mdoc;
        }
        yyjson_doc_free(doc);
    }
}

/*---------------------------------------------------------------------------*/

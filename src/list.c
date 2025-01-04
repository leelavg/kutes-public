#include "gui.hxx"
#include "kt.h"
#include "yyjson.h"
#include <nappgui.h>
#include <time.h>
#include <inet/json.h>
/* https://stackoverflow.com/a/17996915 */
#define QUOTE(...) #__VA_ARGS__
/* 65 is based on pod name length */
#define TEMP_STR_LEN 65

/*---------------------------------------------------------------------------*/

typedef struct _column_t Column;
typedef struct _columns_t Columns;
typedef struct _tb_data_t Tbdata;

/* TODO: enum isn't good in user facing API and rearrange based on usage */
typedef enum _type_t
{
    ktBOOL, /* 0 */
    ktUINT, /* 1 */
    ktSINT, /* 2 */
    ktINT,  /* 3, can overflow */
    ktNUM,  /* 4 */
    ktSTR,  /* 5 */
    ktTIM   /* 6*/
} type_t;
DeclSt(type_t);

struct _column_t
{
    String *display;
    String *path;
    type_t type;
    bool_t primary;
    bool_t freeze;
};
DeclSt(Column);

struct _columns_t
{
    String *kind;
    ArrSt(Column) *cols;
};

DeclPt(yyjson_val);
struct _tb_data_t
{
    char_t tempstr[TEMP_STR_LEN];
    Layout *ops;
    TableView *tbview;
    Edit *status;
    yyjson_val *jval;
    yyjson_doc *doc;
    ArrPt(String) *path;
    ArrPt(String) *display;
    ArrSt(type_t) *type;
    ArrPt(yyjson_val) *ele;
    uint32_t ncols;
    uint32_t nrows;
    uint32_t hmask;
    uint32_t primary;
    uint32_t freeze;
    bool_t invalid;
};

/*---------------------------------------------------------------------------*/

static char_t cjson[] = QUOTE(

    {
        "kind" : "Pod",
        "cols" :
            [
                {
                    "display" : "name",
                    "path" : "/metadata/name",
                    "type" : 5,
                    "primary" : true,
                    "freeze" : true
                },
                {
                    "display" : "age",
                    "path" : "/metadata/creationTimestamp",
                    "type" : 6
                },
                {
                    "display" : "priority",
                    "path" : "/spec/priority",
                    "type" : 1
                },
                {
                    "display" : "status",
                    "path" : "/status/phase",
                    "type" : 5
                }
            ]
    }

);

/*---------------------------------------------------------------------------*/

void cols_bind(void)
{
    dbind_enum(type_t, ktBOOL, "bool");
    dbind_enum(type_t, ktUINT, "unsigned int");
    dbind_enum(type_t, ktSINT, "signed int");
    dbind_enum(type_t, ktINT, "signed int (can overflow)");
    dbind_enum(type_t, ktNUM, "number");
    dbind_enum(type_t, ktSTR, "string");
    dbind_enum(type_t, ktTIM, "timestamp");
    dbind(Column, String *, display);
    dbind(Column, String *, path);
    dbind(Column, type_t, type);
    dbind(Column, bool_t, primary);
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

static void destroy(Tbdata **data)
{
    arrpt_destroy(&(*data)->ele, NULL, yyjson_val);
    yyjson_doc_free((*data)->doc);
    arrpt_destroy(&(*data)->path, str_destroy, String);
    arrpt_destroy(&(*data)->display, str_destroy, String);
    arrst_destroy(&(*data)->type, NULL, type_t);
    heap_delete(data, Tbdata);
}

/*---------------------------------------------------------------------------*/

static void destroy_closure(const Closure *context)
{
    Tbdata *data = cast(context->data, Tbdata);
    context->func_destroy(dcast(&data, void));
}

/*---------------------------------------------------------------------------*/

static Closure *create_destory(Tbdata **data)
{
    Closure *cls = heap_new(Closure);
    *data = heap_new0(Tbdata);
    FUNC_CHECK_DESTROY(destroy, Tbdata);
    FUNC_CHECK_CLOSURE(destroy_closure, Closure);
    cls->data = *data;
    cls->func_destroy = (FPtr_destroy)destroy;
    cls->func_closure = (FPtr_closure)destroy_closure;
    return cls;
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
    Layout *add_col = layout_get_layout(data->ops, 0, 0);
    Edit *disp_name = layout_get_edit(add_col, 0, 0);
    Edit *json_ppth = layout_get_edit(add_col, 1, 0);
    PopUp *json_type = layout_get_popup(add_col, 2, 0);
    uint32_t selected = popup_get_selected(json_type);
    /* TODO: validations */
    if (selected)
    {
        const char_t *name = edit_get_text(disp_name);
        const char_t *path = edit_get_text(json_ppth);
        const uint32_t col_id = data->ncols;
        if (!(name && name[0] && path && path[0]))
            return;
        bstd_sprintf(data->tempstr, sizeof(data->tempstr), "%s", name);
        arrpt_append(data->display, str_c(data->tempstr), String);
        arrpt_append(data->path, str_c(path), String);
        arrst_append(data->type, selected - 1, type_t);
        data->ncols++;
        data->invalid = TRUE;
        tableview_header_title(data->tbview,
                               tableview_new_column_text(data->tbview),
                               data->tempstr);

        {
            Layout *rem_col = layout_get_layout(data->ops, 0, 1);
            PopUp *col_name = layout_get_popup(rem_col, 0, 0);
            bstd_sprintf(data->tempstr, sizeof(data->tempstr), "[%d] %s", col_id, name);
            popup_add_elem(col_name, data->tempstr, NULL);
        }

        edit_text(disp_name, "");
        edit_text(json_ppth, "");
        popup_selected(json_type, 0);
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
        arrpt_delete(data->path, selected - 1, str_destroy, String);
        arrst_delete(data->type, selected - 1, NULL, type_t);
        data->ncols--;
        data->invalid = TRUE;
        tableview_remove_column(data->tbview, selected - 1);

        {
            uint32_t col_id = 0;
            popup_clear(col_name);
            popup_add_elem(col_name, "column", NULL);
            arrpt_foreach_const(name, data->display, String)
                bstd_sprintf(data->tempstr, sizeof(data->tempstr), "[%d] %s", col_id, tc(name));
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
        uint32_t len;
        yyjson_val *root = yyjson_doc_get_root(data->doc);
        yyjson_val *val = yyjson_ptr_get(root, jptr);
        /* switch statement copied from yyjson_get_type_desc function */
        switch (yyjson_get_tag(val))
        {
        case YYJSON_TYPE_RAW | YYJSON_SUBTYPE_NONE:
            len = bstd_sprintf(data->tempstr, sizeof(data->tempstr), "%s", "raw - not printed");
            break;
        case YYJSON_TYPE_NULL | YYJSON_SUBTYPE_NONE:
            len = bstd_sprintf(data->tempstr, sizeof(data->tempstr), "%s", "null - literal");
            break;
        case YYJSON_TYPE_STR | YYJSON_SUBTYPE_NONE | YYJSON_SUBTYPE_NOESC:
            len = bstd_sprintf(data->tempstr, sizeof(data->tempstr), "%s", yyjson_get_str(val));
            break;
        case YYJSON_TYPE_ARR | YYJSON_SUBTYPE_NONE:
            len = bstd_sprintf(data->tempstr, sizeof(data->tempstr), "%s%ld%s", "array - [", yyjson_arr_size(val), "] item(s)");
            break;
        case YYJSON_TYPE_OBJ | YYJSON_SUBTYPE_NONE:
            len = bstd_sprintf(data->tempstr, sizeof(data->tempstr), "%s%ld%s", "object - [", yyjson_obj_size(val), "] key-value pair(s)");
            break;
        case YYJSON_TYPE_BOOL:
            len = bstd_sprintf(data->tempstr, sizeof(data->tempstr), "%s%s", "bool - ", yyjson_get_bool(val) ? "true" : "false");
            break;
        case YYJSON_TYPE_NUM | YYJSON_SUBTYPE_UINT:
            len = bstd_sprintf(data->tempstr, sizeof(data->tempstr), "%s%lu", "uint - ", yyjson_get_uint(val));
            break;
        case YYJSON_TYPE_NUM | YYJSON_SUBTYPE_SINT:
            len = bstd_sprintf(data->tempstr, sizeof(data->tempstr), "%s%ld", "int - ", yyjson_get_sint(val));
            break;
        case YYJSON_TYPE_NUM | YYJSON_SUBTYPE_REAL:
            len = bstd_sprintf(data->tempstr, sizeof(data->tempstr), "%s%.2f", "real -", yyjson_get_real(val));
            break;
        default:
            len = bstd_sprintf(data->tempstr, sizeof(data->tempstr), "%s", "unknown");
            break;
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

static ___INLINE yyjson_val *get_tb_value(Tbdata *data, uint32_t row, uint32_t col)
{
    /*
    1D to 2D: pos = x + width*y
    2D to 1D: x = pos % width; y = pos / width;
    */
    return arrpt_get(data->ele, col + data->ncols * row, yyjson_val);
}

/*---------------------------------------------------------------------------*/

static void tb_cache(Tbdata *data)
{
    size_t idx, max;
    yyjson_val *val;
    if (data->invalid)
    {
        /* TODO: optimize if there is a latency */
        arrpt_clear(data->ele, NULL, yyjson_val);
        yyjson_arr_foreach(data->jval, idx, max, val)
        {
            arrpt_foreach_const(path, data->path, String)
                arrpt_append(data->ele, yyjson_ptr_get(val, tc(path)), yyjson_val);
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
    const type_t *jtype = arrst_get_const(data->type, col, type_t);
    uint32_t len = 0;
    switch (*jtype)
    {
    case ktBOOL:
        len = bstd_sprintf(data->tempstr, sizeof(data->tempstr), "%s", yyjson_get_bool(get_tb_value(data, row, col)) ? "true" : "false");
        break;
    case ktUINT:
    {
        len = bstd_sprintf(data->tempstr, sizeof(data->tempstr), "%lu", yyjson_get_uint(get_tb_value(data, row, col)));
        break;
    }
    case ktSINT:
    {
        len = bstd_sprintf(data->tempstr, sizeof(data->tempstr), "%ld", yyjson_get_sint(get_tb_value(data, row, col)));
        break;
    }
    case ktINT:
    {
        len = bstd_sprintf(data->tempstr, sizeof(data->tempstr), "%d", yyjson_get_int(get_tb_value(data, row, col)));
        break;
    }
    case ktNUM:
    {
        len = bstd_sprintf(data->tempstr, sizeof(data->tempstr), "%.2f", yyjson_get_num(get_tb_value(data, row, col)));
        break;
    }
    case ktSTR:
        len = bstd_sprintf(data->tempstr, sizeof(data->tempstr), "%s", yyjson_get_str(get_tb_value(data, row, col)));
        break;
    case ktTIM:
    {
        const char_t *str = yyjson_get_str(get_tb_value(data, row, col));
        if (data->hmask & (1 << col))
            len = human_duration(str, time(NULL), data->tempstr, sizeof(data->tempstr));
        else
        {
            str_copy_c(data->tempstr, sizeof(data->tempstr), str);
            len = strlen(str);
        }
        break;
    }
        cassert_default();
    }
    return len;
}

/*---------------------------------------------------------------------------*/

static void tb_OnData(Tbdata *data, Event *e)
{
    /*
        TODO: too bad that the sdk invokes this func for every mouse movement
        even when all the data was fed due to table widget kinda behaving like
        immediate mode.

        this is the tightest function and lookout for any optimizations.
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
        fill_tempstr(data, pos->col, pos->row);
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
            /* TODO: allocate on stack rather than this mumbo jumbo? */
            byte_t *buffer = heap_new_n((TEMP_STR_LEN + 1) * data->ncols, byte_t);
            for (col = 0; col < data->ncols; col++)
            {
                len = fill_tempstr(data, col, row);
                bmem_copy(buffer + (next * sizeof(byte_t)), cast(data->tempstr, byte_t), len);
                next += len;
                bmem_set1(buffer + (next * sizeof(byte_t)), 1, ' ');
                next++;
            }
            if (next)
            {
                bmem_set_zero(buffer + ((next - 1) * sizeof(char_t)), 1);
                edit_text(data->status, cast(buffer, char_t));
            }
            heap_delete_n(&buffer, ((TEMP_STR_LEN + 1) * data->ncols), byte_t);
        }
        break;
    }
        cassert_default();
    }
}

/*---------------------------------------------------------------------------*/

static void add_list_to_layout(App *app, yyjson_doc *doc, PopUp *pop, Layout *vscroll)
{
    yyjson_val *root = yyjson_doc_get_root(doc);
    yyjson_val *items = yyjson_ptr_get(root, "/items");
    yyjson_val *first = yyjson_ptr_get(items, "/0/kind");
    const char_t *kind;
    if (first)
        kind = yyjson_get_str(first);
    else
        return;

    popup_add_elem(pop, "table", NULL);
    {
        Stream *stm = stm_from_block(cast(cjson, byte_t), sizeof(cjson));
        if (stm != NULL)
        {
            Columns *json = json_read(stm, NULL, Columns);
            if (json != NULL && !blib_strcmp(kind, tc(json->kind)))
            {
                Tbdata *data;
                Layout *table = layout_create(1, 2);
                Layout *ops = layout_create(1, 4);
                Layout *add_col = layout_create(4, 1);
                Layout *rem_col = layout_create(2, 1);
                Layout *query_col = layout_create(3, 1);
                Layout *status_row = layout_create(2, 1);

                Edit *disp_name = edit_create();
                Edit *json_ppth = edit_create();
                PopUp *json_type = popup_create();
                Button *col_add = button_push();

                PopUp *col_name = popup_create();
                Button *col_rem = button_push();

                Edit *query_ppth = edit_create();
                Button *query_run = button_push();
                Edit *query_result = edit_create();

                Edit *status = edit_create();
                Button *open = button_push();

                TableView *tbview = tableview_create();

                edit_phstyle(disp_name, ekFITALIC);
                edit_phtext(disp_name, "display name");
                layout_edit(add_col, disp_name, 0, 0);

                edit_phstyle(json_ppth, ekFITALIC);
                edit_phtext(json_ppth, "json pointer path");
                layout_edit(add_col, json_ppth, 1, 0);
                layout_hexpand(add_col, 1);

                popup_add_elem(json_type, "type", NULL);
                popup_add_elem(json_type, "bool", NULL);
                popup_add_elem(json_type, "unsigned int", NULL);
                popup_add_elem(json_type, "signed int", NULL);
                popup_add_elem(json_type, "signed int (can overflow)", NULL);
                popup_add_elem(json_type, "number", NULL);
                popup_add_elem(json_type, "string", NULL);
                popup_add_elem(json_type, "timestamp", NULL);
                layout_popup(add_col, json_type, 2, 0);

                button_text(col_add, "col add");
                layout_button(add_col, col_add, 3, 0);

                layout_layout(ops, add_col, 0, 0);

                popup_add_elem(col_name, "column", NULL);
                layout_popup(rem_col, col_name, 0, 0);
                layout_hexpand(rem_col, 0);

                button_text(col_rem, "col rem");
                layout_button(rem_col, col_rem, 1, 0);

                layout_layout(ops, rem_col, 0, 1);

                edit_phstyle(query_ppth, ekFITALIC);
                edit_phtext(query_ppth, "json pointer query from document root (ex: /items/0/kind)");
                layout_edit(query_col, query_ppth, 0, 0);

                button_text(query_run, "query");
                layout_button(query_col, query_run, 1, 0);

                edit_editable(query_result, FALSE);
                layout_edit(query_col, query_result, 2, 0);

                layout_hexpand2(query_col, 0, 2, .5f);
                layout_layout(ops, query_col, 0, 2);

                edit_editable(status, FALSE);
                edit_vpadding(status, 0);
                layout_edit(status_row, status, 0, 0);

                button_text(open, "open");
                layout_button(status_row, open, 1, 0);

                layout_hexpand(status_row, 0);
                layout_layout(ops, status_row, 0, 3);
                layout_vsize(ops, 3, 25);

                layout_layout(table, ops, 0, 0);

                layout_tableview(table, tbview, 0, 1);
                layout_vexpand(table, 1);

                layout_insert_row(vscroll, 1);
                layout_layout(vscroll, table, 0, 1);

                app->cls = create_destory(&data);
                data->doc = doc;
                data->jval = items;
                data->path = arrpt_create(String);
                data->display = arrpt_create(String);
                data->type = arrst_create(type_t);
                data->nrows = yyjson_arr_size(items);
                data->primary = 0;
                data->freeze = 0;
                arrst_foreach_const(col, json->cols, Column)
                    bstd_sprintf(data->tempstr, sizeof(data->tempstr), "%s", tc(col->display));
                    tableview_header_title(tbview,
                                           tableview_new_column_text(tbview),
                                           data->tempstr);
                    tableview_header_align(tbview, data->ncols, ekCENTER);
                    data->primary = col->primary ? data->ncols : data->primary;
                    data->freeze = col->freeze ? data->ncols : data->freeze;
                    arrpt_append(data->path, str_copy(col->path), String);
                    arrpt_append(data->display, str_copy(col->display), String);
                    bstd_sprintf(data->tempstr, sizeof(data->tempstr), "[%d] %s", data->ncols, tc(col->display));
                    popup_add_elem(col_name, data->tempstr, NULL);
                    arrst_append(data->type, col->type, type_t);
                    /* REAL32_MAX-1 isn't working as max width and 1000 is greater than panel size */
                    tableview_column_limits(tbview, data->ncols, 20, 1000);
                    if (col->type == ktTIM)
                        /* can view time without cutoff with 160 */
                        tableview_column_width(tbview, data->ncols, 160);
                    data->ncols++;
                arrst_end()

                tableview_OnData(tbview, listener(data, tb_OnData, Tbdata));
                tableview_OnHeaderClick(tbview, listener(data, tb_OnHeader, Tbdata));
                tableview_header_resizable(tbview, TRUE);
                /* width can't be updated as a response to an event as that'll result in multiple events and
                 expanding primary, 450 is based on longest pod name length that I see */
                tableview_column_width(tbview, data->primary, 450);
                tableview_column_freeze(tbview, data->freeze);
                tableview_header_clickable(tbview, TRUE);

                button_OnClick(col_add, listener(data, onCol_add, Tbdata));
                button_OnClick(col_rem, listener(data, onCol_rem, Tbdata));
                button_OnClick(query_run, listener(data, onQuery_run, Tbdata));
                button_OnClick(open, listener(data, onRow_open, Tbdata));

                data->ops = ops;
                data->tbview = tbview;
                data->ele = arrpt_create(yyjson_val);
                data->invalid = TRUE;
                data->status = status;

                tableview_update(tbview);
            }
            json_destroy(&json, Columns);
            stm_close(&stm);
        }
    }
}

/*---------------------------------------------------------------------------*/

static void add_kind_to_layout(App *app, yyjson_doc *doc, PopUp *pop, Layout *vscroll)
{
    TextView *tview;
    yyjson_val *root = yyjson_doc_get_root(doc);
    /* TODO: should check for full GVK */
    yyjson_val *first = yyjson_ptr_get(root, "/kind");
    const char_t *kind;
    if (first)
        kind = yyjson_get_str(first);
    else
        return;

    tview = textview_create();
    popup_add_elem(pop, "owner", NULL);
    layout_insert_row(vscroll, 1);
    textview_writef(tview, kind);
    layout_textview(vscroll, tview, 0, 1);
    unref(app);
}

/*---------------------------------------------------------------------------*/

void populate_listbox(App *app, Layout *vscroll, const char_t *cmdout, uint32_t len)
{
    yyjson_doc *doc = yyjson_read_opts(cast(cmdout, char_t), len, 0, app->alc, NULL);
    if (doc)
    {
        yyjson_val *root = yyjson_doc_get_root(doc);
        yyjson_val *kind = yyjson_ptr_get(root, "/kind");
        PopUp *pop = app->vselect;
        if (kind)
        {
            if (!blib_strcmp(yyjson_get_str(kind), "List"))
                add_list_to_layout(app, doc, pop, vscroll);
            else
                add_kind_to_layout(app, doc, pop, vscroll);
        }
        /* no row is added */
        if (layout_nrows(vscroll) < 2)
            yyjson_doc_free(doc);
    }
}

/*---------------------------------------------------------------------------*/

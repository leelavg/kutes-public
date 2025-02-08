#include <boron.h>
#include <unset.h>
#include <yyjson.h>

#include <time.h>
#include <stdio.h>

#define MIN(x, y) ((x < y) ? x : y)
#define SLEN 65

static char str[SLEN];
static UBuffer bstr;
static UAtom jrootW;

enum YYDataType
{
    UT_VAL_PTR = UT_BORON_COUNT,
    UT_ARR_ITER,
    UT_OBJ_ITER,
    UT_YY_COUNT
};
#define yy_count (UT_YY_COUNT - UT_BORON_COUNT)

static void yy_mark(UThread *ut, UCell *cell)
{
    UIndex n = cell->series.buf;
    if (n > UR_INVALID_BUF)
        ur_markBuffer(ut, n);
}

static void yy_destroy(UBuffer *buf)
{
    switch (buf->type)
    {
    case UT_ARR_ITER:
        /* fall through */
    case UT_OBJ_ITER:
        if (buf->ptr.v)
            free(buf->ptr.v);
        /* fall through */
    case UT_VAL_PTR:
        if (buf->ptr.v)
            buf->ptr.v = NULL;
    }
}

const UDatatype yy_types[] = {
    /* clang-format off */
    {
        "jval!",
        unset_make,        unset_make,       unset_copy,
        unset_compare,     unset_operate,    unset_select,
        unset_toString,    unset_toText,
        unset_recycle,     yy_mark,          yy_destroy,
        unset_markBuf,     unset_toShared,   unset_bind
    },
    {
        "jait!",
        unset_make,        unset_make,       unset_copy,
        unset_compare,     unset_operate,    unset_select,
        unset_toString,    unset_toText,
        unset_recycle,     yy_mark,          yy_destroy,
        unset_markBuf,     unset_toShared,   unset_bind
    },
    {
        "joit!",
        unset_make,        unset_make,       unset_copy,
        unset_compare,     unset_operate,    unset_select,
        unset_toString,    unset_toText,
        unset_recycle,     yy_mark,          yy_destroy,
        unset_markBuf,     unset_toShared,   unset_bind
    },
    /* clang-format on */
};

static UBuffer *makeYYBuf(UThread *ut, int type, void *ptr, UCell *cell)
{
    UIndex bufN;
    UBuffer *buf = ur_genBuffers(ut, 1, &bufN);
    buf->type = type;
    buf->ptr.v = ptr;
    ur_initSeries(cell, type, bufN);
    return buf;
}

static yyjson_val *rootLookup(UThread *ut, UAtom name)
{
    UBuffer *ctx = ur_threadContext(ut);
    int n = ur_ctxLookup(ctx, name);
    if (n < 0)
        return NULL;
    return (yyjson_val *)ur_bufferSer(ur_ctxCell(ctx, n))->ptr.v;
}

CFUNC(jait)
{
    const char *cp = boron_cstr(ut, a1, 0);
    if (cp && cp[0])
    {
        yyjson_arr_iter iter;
        yyjson_val *jroot = rootLookup(ut, jrootW);
        yyjson_val *val = yyjson_ptr_get(jroot, cp);
        if (yyjson_arr_iter_init(val, &iter))
        {
            yyjson_arr_iter *hiter = malloc(sizeof(yyjson_arr_iter));
            if (hiter)
            {
                memcpy(hiter, &iter, sizeof(yyjson_arr_iter));
                makeYYBuf(ut, UT_ARR_ITER, hiter, res);
                return UR_OK;
            }
        }
    }
    ur_setId(res, UT_UNSET);
    return UR_OK;
}

CFUNC(janv)
{
    const UBuffer *ser = ur_bufferSer(a1);
    if (ser)
    {
        yyjson_arr_iter *iter = ser->ptr.v;
        yyjson_val *val = yyjson_arr_iter_next(iter);
        if (val)
        {
            makeYYBuf(ut, UT_VAL_PTR, val, res);
            return UR_OK;
        }
    }
    ur_setId(res, UT_UNSET);
    return UR_OK;
}

CFUNC(joit)
{
    const char *cp = boron_cstr(ut, a1, 0);
    if (cp && cp[0])
    {
        yyjson_obj_iter iter;
        yyjson_val *jroot = rootLookup(ut, jrootW);
        yyjson_val *val = yyjson_ptr_get(jroot, cp);
        if (yyjson_obj_iter_init(val, &iter))
        {
            yyjson_obj_iter *hiter = malloc(sizeof(yyjson_obj_iter));
            if (hiter)
            {
                memcpy(hiter, &iter, sizeof(yyjson_obj_iter));
                makeYYBuf(ut, UT_OBJ_ITER, hiter, res);
                return UR_OK;
            }
        }
    }
    ur_setId(res, UT_UNSET);
    return UR_OK;
}

CFUNC(jonk)
{
    const UBuffer *ser = ur_bufferSer(a1);
    if (ser)
    {
        yyjson_obj_iter *iter = ser->ptr.v;
        yyjson_val *key = yyjson_obj_iter_next(iter);
        if (key)
        {
            makeYYBuf(ut, UT_VAL_PTR, key, res);
            return UR_OK;
        }
    }
    ur_setId(res, UT_UNSET);
    return UR_OK;
}

CFUNC(jonv)
{
    const UBuffer *ser = ur_bufferSer(a1);
    if (ser)
    {
        yyjson_val *key = ser->ptr.v;
        yyjson_val *val = yyjson_obj_iter_get_val(key);
        if (val)
        {
            makeYYBuf(ut, UT_VAL_PTR, val, res);
            return UR_OK;
        }
    }
    ur_setId(res, UT_UNSET);
    return UR_OK;
}

CFUNC(jval)
{
    const UBuffer *ser = ur_bufferSer(a1);
    yyjson_val *val = NULL;
    if (!ser)
    {
        ur_setId(res, UT_UNSET);
        return UR_OK;
    }
    val = ser->ptr.v;
    switch (yyjson_get_tag(val))
    {
    case YYJSON_TYPE_STR | YYJSON_SUBTYPE_NONE:
    case YYJSON_TYPE_STR | YYJSON_SUBTYPE_NOESC:
    {
        int len = yyjson_get_len(val);
        if (len)
        {
            int plen = MIN(len, SLEN);
            UBuffer *buf = ur_makeStringCell(ut, UR_ENC_UTF8, plen, res);
            snprintf(buf->ptr.c, plen + 1, "%s", yyjson_get_str(val));
            buf->used = plen;
        }
        else
        {
            ur_setId(res, UT_UNSET);
        }
        break;
    }
    case YYJSON_TYPE_BOOL | YYJSON_SUBTYPE_TRUE:
        ur_setId(res, UT_LOGIC);
        ur_logic(res) = 1;
        break;
    case YYJSON_TYPE_BOOL | YYJSON_SUBTYPE_FALSE:
        ur_setId(res, UT_LOGIC);
        ur_logic(res) = 0;
        break;
    case YYJSON_TYPE_NUM | YYJSON_SUBTYPE_UINT:
        ur_setId(res, UT_INT);
        ur_int(res) = yyjson_get_uint(val);
        break;
    case YYJSON_TYPE_NUM | YYJSON_SUBTYPE_SINT:
        ur_setId(res, UT_INT);
        ur_int(res) = yyjson_get_sint(val);
        break;
    case YYJSON_TYPE_NUM | YYJSON_SUBTYPE_REAL:
        ur_setId(res, UT_DOUBLE);
        ur_double(res) = yyjson_get_num(val);
        break;
    default:
        ur_setId(res, UT_UNSET);
        break;
    }
    return UR_OK;
}

CFUNC(jlen)
{
    yyjson_val *jroot = NULL, *val = NULL;
    /* option /ptr = 0x01 */
    if (CFUNC_OPTIONS & 0x01)
    {
        const UBuffer *ser = ur_is(a1, UT_VAL_PTR) ? ur_bufferSer(a1) : NULL;
        val = ser ? ser->ptr.v : NULL;
    }
    else
    {
        const char *cp = ur_is(a1, UT_STRING) ? boron_cstr(ut, a1, 0) : NULL;
        jroot = rootLookup(ut, jrootW);
        val = yyjson_ptr_get(jroot, cp);
    }

    if (val)
    {
        ur_setId(res, UT_INT);
        ur_int(res) = yyjson_get_len(val);
    }
    else
    {
        ur_setId(res, UT_UNSET);
    }
    return UR_OK;
}

CFUNC(jptr)
{
    const char *cp = boron_cstr(ut, a1, 0);
    yyjson_val *jroot = NULL, *val = NULL;

    if (!(cp && cp[0]))
    {
        ur_setId(res, UT_UNSET);
        return UR_OK;
    }

    /* option /root = 0x01 */
    if (CFUNC_OPTIONS & 0x01)
    {
        const UBuffer *ser = ur_is(CFUNC_OPT_ARG(1), UT_VAL_PTR) ? ur_bufferSer(CFUNC_OPT_ARG(1)) : NULL;
        jroot = ser ? ser->ptr.v : NULL;
    }
    else
    {
        jroot = rootLookup(ut, jrootW);
    }

    val = yyjson_ptr_get(jroot, cp);
    if (val)
    {
        UIndex bufN;
        UBuffer *buf = ur_genBuffers(ut, 1, &bufN);
        buf->type = UT_VAL_PTR;
        buf->ptr.v = (void *)val;
        ur_initSeries(res, buf->type, bufN);
        return UR_OK;
    }

    ur_setId(res, UT_UNSET);
    return UR_OK;
}

void eval(UThread *ut, const char *line)
{
    const UCell *val = boron_evalUtf8(ut, line, -1);
    if (val)
    {
        switch (ur_type(val))
        {
        case UT_LOGIC:
            snprintf(str, SLEN, "%s", ur_logic(val) ? "true" : "false");
            break;
        case UT_INT:
            snprintf(str, SLEN, "%ld", ur_int(val));
            break;
        case UT_DOUBLE:
            snprintf(str, SLEN, "%.2f", ur_double(val));
            break;
        case UT_STRING:
            snprintf(str, SLEN, "%s", boron_cstr(ut, val, 0));
            break;
        default:
            snprintf(str, SLEN, "type is %d", ur_type(val));
            break;
        }
    }
    else if (ur_is(ur_exception(ut), UT_ERROR))
    {
        bstr.used = 0;
        ur_toText(ut, ur_exception(ut), &bstr);
        ur_strTermNull(&bstr);
        snprintf(str, SLEN, "%s", bstr.ptr.c);
        ur_recycle(ut);
    }
    else
        snprintf(str, SLEN, "unhandled");

    printf("%s\n", str);
}

static const BoronCFunc funcs[] = {

    jait, janv, joit, jonk,
    jonv, jval, jlen, jptr

};

static const char funcSpecs[] = {

    /* returns jait! */
    "jait pth string!\n"
    /* returns jval! */
    "janv ptr jait!\n"

    /* returns joit! */
    "joit pth string!\n"
    /* returns jval! */
    "jonk ptr joit!\n"
    /* returns jval! */
    "jonv ptr jval!\n"

    /* returns string!/logic!/int!/double! */
    "jval ptr jval!\n"
    /* returns int! */
    "jlen inp string!/jval! /ptr\n"
    /* returns jval! */
    "jptr pth string! /root ptr jval!\n"

};

void run(void)
{
    char *line = NULL;
    size_t size;
    int out = 0;
    yyjson_doc *doc;

    UEnvParameters params;
    UThread *ut;
    {
        unsigned int i;
        const UDatatype *table[yy_count];
        boron_envParam(&params);
        for (i = 0; i < (sizeof(yy_types) / sizeof(UDatatype)); ++i)
            table[i] = yy_types + i;
        params.dtTable = table;
        params.dtCount = yy_count;
    }
    ut = boron_makeEnv(&params);
    boron_defineCFunc(ut, UR_MAIN_CONTEXT, funcs, funcSpecs, sizeof(funcSpecs) - 1);
    ur_freezeEnv(ut);
    ur_strInit(&bstr, UR_ENC_UTF8, 0);

    /*
        Examples:
        ait: jait {/items} nxt: janv ait r: 0 t:0 while [value? 'nxt][if eq? jval jptr/root {/status/phase} nxt {Running} [++ r] ++ t nxt: janv ait] join r ['/' t]
    */
    doc = yyjson_read_file(".cache/out-largest.json", 0, NULL, NULL);
    jrootW = ur_intern(ut, "jroot", 5);
    if (doc)
    {
        yyjson_val *jroot = yyjson_doc_get_root(doc);
        UCell *cell = ur_ctxAddWord(ur_threadContext(ut), jrootW);
        makeYYBuf(ut, UT_VAL_PTR, jroot, cell);
        ur_ctxSort(ur_threadContext(ut));

        while ((out = getline(&line, &size, stdin)) != -1)
        {
            unsigned int times = 1;
            struct timespec begin, end;

            {
                clock_gettime(CLOCK_REALTIME, &begin);

                unsigned int i;
                for (i = 0; i < times; ++i)
                    eval(ut, line);

                clock_gettime(CLOCK_REALTIME, &end);
                long seconds = end.tv_sec - begin.tv_sec;
                long nanoseconds = end.tv_nsec - begin.tv_nsec;
                double elapsed = seconds + nanoseconds * 1e-9;
                /* printf("eval for %d times took %.3f seconds.\n", times, elapsed); */
            }
            {
                clock_gettime(CLOCK_REALTIME, &begin);

                unsigned int i;
                for (i = 0; i < times; ++i)
                {
                    int r = 0, t = 0;
                    const char *running;
                    yyjson_val *val;
                    yyjson_arr_iter iter = yyjson_arr_iter_with(yyjson_ptr_get(jroot, "/items"));
                    while ((val = yyjson_arr_iter_next(&iter)))
                    {
                        running = yyjson_get_str(yyjson_ptr_get(val, "/status/phase"));
                        if (strcmp(running, "Running") == 0)
                            r++;
                        t++;
                    }
                }

                clock_gettime(CLOCK_REALTIME, &end);
                long seconds = end.tv_sec - begin.tv_sec;
                long nanoseconds = end.tv_nsec - begin.tv_nsec;
                double elapsed = seconds + nanoseconds * 1e-9;
                /* printf("yyjson for %d times took %.3f seconds.\n", times, elapsed); */
            }

            if (!strcmp(line, "exit\n"))
            {
                break;
            }
        }

        ur_strFree(&bstr);
        free(line);
        yyjson_doc_free(doc);
    }
    ur_strFree(&bstr);
    boron_freeEnv(ut);
}

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    run();
    return 0;
}

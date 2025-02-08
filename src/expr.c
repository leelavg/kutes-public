#include "kt.h"
#include <urlan.h>
#include <yyjson.h>

#include <boron.h>
#include <stdint.h>
#include <unset.h>

#include <time.h>

static UAtom jrootW;

enum YYDataType
{
    UT_MUT_VAL_PTR = UT_BORON_COUNT,
    UT_MUT_ARR_ITER,
    UT_MUT_OBJ_ITER,
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
    case UT_MUT_ARR_ITER:
        if (buf->ptr.v)
            heap_delete(dcast(&buf->ptr.v, yyjson_mut_arr_iter), yyjson_mut_arr_iter);
        /* fall through */
    case UT_MUT_OBJ_ITER:
        if (buf->ptr.v)
            heap_delete(dcast(&buf->ptr.v, yyjson_mut_obj_iter), yyjson_mut_obj_iter);
        /* fall through */
    case UT_MUT_VAL_PTR:
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

static yyjson_mut_val *rootLookup(UThread *ut, UAtom name)
{
    UBuffer *ctx = ur_threadContext(ut);
    int32_t n = ur_ctxLookup(ctx, name);
    if (n < 0)
        return NULL;
    return (yyjson_mut_val *)ur_bufferSer(ur_ctxCell(ctx, n))->ptr.v;
}

CFUNC(jait)
{
    yyjson_mut_val *val = NULL;
    /* option /ptr = 0x01 */
    if (CFUNC_OPTIONS & 0x01)
    {
        const UBuffer *ser = ur_is(a1, UT_MUT_VAL_PTR) ? ur_bufferSer(a1) : NULL;
        val = ser ? ser->ptr.v : NULL;
    }
    else
    {
        const char *cp = ur_is(a1, UT_STRING) ? boron_cstr(ut, a1, 0) : NULL;
        yyjson_mut_val *jroot = rootLookup(ut, jrootW);
        val = yyjson_mut_ptr_get(jroot, cp);
    }
    if (val)
    {
        yyjson_mut_arr_iter iter;
        if (yyjson_mut_arr_iter_init(val, &iter))
        {
            yyjson_mut_arr_iter *hiter = heap_new(yyjson_mut_arr_iter);
            bmem_copy_n(hiter, &iter, 1, yyjson_mut_arr_iter);
            makeYYBuf(ut, UT_MUT_ARR_ITER, hiter, res);
            return UR_OK;
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
        yyjson_mut_arr_iter *iter = ser->ptr.v;
        yyjson_mut_val *val = yyjson_mut_arr_iter_next(iter);
        if (val)
        {
            makeYYBuf(ut, UT_MUT_VAL_PTR, val, res);
            return UR_OK;
        }
    }
    ur_setId(res, UT_UNSET);
    return UR_OK;
}

CFUNC(joit)
{
    yyjson_mut_val *val = NULL;
    /* option /ptr = 0x01 */
    if (CFUNC_OPTIONS & 0x01)
    {
        const UBuffer *ser = ur_is(a1, UT_MUT_VAL_PTR) ? ur_bufferSer(a1) : NULL;
        val = ser ? ser->ptr.v : NULL;
    }
    else
    {
        const char *cp = ur_is(a1, UT_STRING) ? boron_cstr(ut, a1, 0) : NULL;
        yyjson_mut_val *jroot = rootLookup(ut, jrootW);
        val = yyjson_mut_ptr_get(jroot, cp);
    }
    if (val)
    {
        yyjson_mut_obj_iter iter;
        if (yyjson_mut_obj_iter_init(val, &iter))
        {
            yyjson_mut_obj_iter *hiter = heap_new(yyjson_mut_obj_iter);
            bmem_copy_n(hiter, &iter, 1, yyjson_mut_obj_iter);
            makeYYBuf(ut, UT_MUT_OBJ_ITER, hiter, res);
            return UR_OK;
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
        yyjson_mut_obj_iter *iter = ser->ptr.v;
        yyjson_mut_val *key = yyjson_mut_obj_iter_next(iter);
        if (key)
        {
            makeYYBuf(ut, UT_MUT_VAL_PTR, key, res);
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
        yyjson_mut_val *key = ser->ptr.v;
        yyjson_mut_val *val = yyjson_mut_obj_iter_get_val(key);
        if (val)
        {
            makeYYBuf(ut, UT_MUT_VAL_PTR, val, res);
            return UR_OK;
        }
    }
    ur_setId(res, UT_UNSET);
    return UR_OK;
}

CFUNC(jval)
{
    const UBuffer *ser = ur_bufferSer(a1);
    yyjson_mut_val *val = NULL;
    if (!ser)
    {
        ur_setId(res, UT_UNSET);
        return UR_OK;
    }
    val = ser->ptr.v;
    switch (yyjson_mut_get_tag(val))
    {
    case YYJSON_TYPE_STR | YYJSON_SUBTYPE_NONE:
    case YYJSON_TYPE_STR | YYJSON_SUBTYPE_NOESC:
    {
        uint32_t len = yyjson_mut_get_len(val);
        if (len)
        {
            /* assuming 255 chars is good enough for an image length */
            uint32_t plen = min_u32(len, 255);
            UBuffer *buf = ur_makeStringCell(ut, UR_ENC_UTF8, plen, res);
            bmem_copy_n(buf->ptr.c, yyjson_mut_get_str(val), plen, char_t);
            buf->used = plen;
        }
        else
        {
            ur_setId(res, UT_UNSET);
        }
        break;
    }
    case YYJSON_TYPE_NUM | YYJSON_SUBTYPE_UINT:
    case YYJSON_TYPE_NUM | YYJSON_SUBTYPE_SINT:
        ur_setId(res, UT_INT);
        ur_int(res) = yyjson_mut_get_sint(val);
        break;
    case YYJSON_TYPE_BOOL | YYJSON_SUBTYPE_TRUE:
        ur_setId(res, UT_LOGIC);
        ur_logic(res) = 1;
        break;
    case YYJSON_TYPE_BOOL | YYJSON_SUBTYPE_FALSE:
        ur_setId(res, UT_LOGIC);
        ur_logic(res) = 0;
        break;
    case YYJSON_TYPE_NUM | YYJSON_SUBTYPE_REAL:
        ur_setId(res, UT_DOUBLE);
        ur_double(res) = yyjson_mut_get_num(val);
        break;
    case YYJSON_TYPE_ARR | YYJSON_SUBTYPE_NONE:
    case YYJSON_TYPE_OBJ | YYJSON_SUBTYPE_NONE:
        makeYYBuf(ut, UT_MUT_VAL_PTR, val, res);
        break;
    case YYJSON_TYPE_RAW | YYJSON_SUBTYPE_NONE:
    case YYJSON_TYPE_NULL | YYJSON_SUBTYPE_NONE:
    default:
        ur_setId(res, UT_UNSET);
        break;
    }
    return UR_OK;
}

CFUNC(jlen)
{
    yyjson_mut_val *val;
    /* option /ptr = 0x01 */
    if (CFUNC_OPTIONS & 0x01)
    {
        const UBuffer *ser = ur_is(a1, UT_MUT_VAL_PTR) ? ur_bufferSer(a1) : NULL;
        val = ser ? ser->ptr.v : NULL;
    }
    else
    {
        const char *cp = ur_is(a1, UT_STRING) ? boron_cstr(ut, a1, 0) : NULL;
        yyjson_mut_val *jroot = rootLookup(ut, jrootW);
        val = yyjson_mut_ptr_get(jroot, cp);
    }
    if (val)
    {
        ur_setId(res, UT_INT);
        ur_int(res) = yyjson_mut_get_len(val);
        return UR_OK;
    }
    ur_setId(res, UT_UNSET);
    return UR_OK;
}

CFUNC(jptr)
{
    const char *cp = boron_cstr(ut, a1, 0);
    const UBuffer *ser = NULL;
    yyjson_mut_val *jroot, *val;

    if (!(cp && cp[0]))
    {
        ur_setId(res, UT_UNSET);
        return UR_OK;
    }
    /* option /root = 0x01 */
    if (CFUNC_OPTIONS & 0x01)
    {
        ser = ur_bufferSer(CFUNC_OPT_ARG(1));
    }
    jroot = ser ? ser->ptr.v : rootLookup(ut, jrootW);
    val = yyjson_mut_ptr_get(jroot, cp);
    if (val)
    {
        UIndex bufN;
        UBuffer *buf = ur_genBuffers(ut, 1, &bufN);
        buf->type = UT_MUT_VAL_PTR;
        buf->ptr.v = (void *)val;
        ur_initSeries(res, buf->type, bufN);
        return UR_OK;
    }
    ur_setId(res, UT_UNSET);
    return UR_OK;
}

CFUNC(now)
{
    time_t n = time(NULL);
    struct tm *t = gmtime(&n);
    /* example: 2001-02-13T14:15:16Z */
    UBuffer *buf = ur_makeStringCell(ut, UR_ENC_UTF8, 30, res);
    buf->used = bstd_sprintf(buf->ptr.c, 30, "%04d-%02d-%02dT%02d:%02d:%02dZ",
                             t->tm_year + 1900, t->tm_mon + 1, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec);
    return UR_OK;
}

static const BoronCFunc funcs[] = {

    joit, jait, jlen, janv,
    jonk, jonv, jptr, jval,
    now

};

static const char funcSpecs[] = {

    /* returns joit! */
    "joit inp string!/jval! /ptr\n"
    /* returns jait! */
    "jait inp string!/jval! /ptr\n"
    /* returns int! */
    "jlen inp string!/jval! /ptr\n"

    /* returns jval! */
    "janv ptr jait!\n"
    /* returns jval! */
    "jonk ptr joit!\n"
    /* returns jval! */
    "jonv ptr jval!\n"
    /* returns jval! */
    "jptr pth string! /root ptr jval!\n"

    /* returns string!/logic!/int!/double!/jval! */
    "jval ptr jval!\n"

    /* returns iso8601 UTC time as string! ex: 2001-02-13T14:15:16Z */
    "now\n"

};

UThread *uthread_create(void)
{
    const UDatatype *table[yy_count];
    UEnvParameters params;
    uint32_t i;
    UCell *cell;
    UThread *ut;

    boron_envParam(&params);
    for (i = 0; i < (sizeof(yy_types) / sizeof(UDatatype)); ++i)
        table[i] = yy_types + i;
    params.dtTable = table;
    params.dtCount = yy_count;
    ut = boron_makeEnv(&params);
    if (!ut)
    {
        return NULL;
    }
    boron_defineCFunc(ut, UR_MAIN_CONTEXT, funcs, funcSpecs, sizeof(funcSpecs) - 1);
    ur_freezeEnv(ut);
    jrootW = ur_intern(ut, "jroot", 5);
    cell = ur_ctxAddWord(ur_threadContext(ut), jrootW);
    makeYYBuf(ut, UT_MUT_VAL_PTR, NULL, cell);
    ur_ctxSort(ur_threadContext(ut));
    return ut;
}

void uthread_destroy(UThread **ut)
{
    boron_freeEnv(*ut);
    *ut = NULL;
}

void update_jroot(UThread *ut, yyjson_mut_val *jroot)
{
    UBuffer *ctx = ur_threadContext(ut);
    int n = ur_ctxLookup(ctx, jrootW);
    cassert(n >= 0);
    ur_bufferSerM(ur_ctxCell(ctx, n))->ptr.v = jroot;
}

KDataType boron_eval(UThread *ut, const char *script, UCell **val)
{
    *val = boron_evalUtf8(ut, script, -1);
    if (*val)
    {
        switch (ur_type(*val))
        {
        case UT_STRING:
            return ktSTR;
        case UT_INT:
            return ktINT;
        case UT_LOGIC:
            return ktBOOL;
        case UT_DOUBLE:
            return ktNUM;
        case UT_MUT_VAL_PTR:
            return ktJVAL;
        }
    }
    else if (ur_is(ur_exception(ut), UT_ERROR))
    {
        boron_reset(ut);
        ur_recycle(ut);
    }
    return ktUNK;
}

const char *bn_str(UThread *ut, UCell *val)
{
    return boron_cstr(ut, val, 0);
}

int64_t bn_int(UCell *val)
{
    return ur_int(val);
}

bool_t bn_bool(UCell *val)
{
    return ur_logic(val);
}

double bn_num(UCell *val)
{
    return ur_double(val);
}

yyjson_mut_val *bn_jval(UThread *ut, UCell *val)
{
    const UBuffer *ser = ur_bufferSer(val);
    if (ser)
        return ser->ptr.v;
    return NULL;
}

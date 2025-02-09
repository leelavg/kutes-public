/*
 * NAppGUI Cross-platform C SDK
 * 2015-2025 Francisco Garcia Collado
 * MIT Licence
 * https://nappgui.com/en/legal/license.html
 *
 * File: blib.c
 *
 */

/* C library funcions */

#include "blib.h"
#include "cassert.h"
#include "ptr.h"
#include "qsort.inl"
#include "sewer.inl"

#include "nowarn.hxx"
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include "warn.hxx"

/*---------------------------------------------------------------------------*/

uint32_t blib_strlen(const char_t *str)
{
    cassert_no_null(str);
    return (uint32_t)strlen(cast_const(str, char));
}

/*---------------------------------------------------------------------------*/

char_t *blib_strstr(const char_t *str, const char_t *substr)
{
    cassert_no_null(str);
    cassert_no_null(substr);
    return cast(strstr(cast_const(str, char), cast_const(substr, char)), char_t);
}

/*---------------------------------------------------------------------------*/

void blib_strcpy(char_t *dest, const uint32_t size, const char_t *src)
{
    size_t len = 0;
    cassert_no_null(dest);
    cassert_no_null(src);
    len = strlen(src);
    if (len >= size)
        len = size - 1;
    memcpy(cast(dest, char), cast_const(src, char), (size_t)len);
    dest[len] = '\0';
}

/*---------------------------------------------------------------------------*/

void blib_strncpy(char_t *dest, const uint32_t size, const char_t *src, const uint32_t n)
{
    size_t len = 0;
    cassert_no_null(dest);
    cassert_no_null(src);
    len = strlen(src);
    if (n < len)
        len = n;
    if (len >= size)
        len = size - 1;
    memcpy(cast(dest, char), cast_const(src, char), (size_t)len);
    dest[len] = '\0';
}

/*---------------------------------------------------------------------------*/

void blib_strcat(char_t *dest, const uint32_t size, const char_t *src)
{
    size_t len = 0;
    cassert_no_null(dest);
    cassert_no_null(src);
    len = strlen(dest);
    if (size > len + 1)
    {
        size_t len2 = strlen(src);
        if (len2 >= size - len)
            len2 = size - len - 1;
        memcpy(cast(dest + len, char), cast_const(src, char), (size_t)len2);
        dest[len + len2] = '\0';
    }
}

/*---------------------------------------------------------------------------*/

int blib_strcmp(const char_t *str1, const char_t *str2)
{
    cassert_no_null(str1);
    cassert_no_null(str2);
    return strcmp(cast_const(str1, char), cast_const(str2, char));
}

/*---------------------------------------------------------------------------*/

int blib_strncmp(const char_t *str1, const char_t *str2, const uint32_t n)
{
    cassert_no_null(str1);
    cassert_no_null(str2);
    return strncmp(cast_const(str1, char), cast_const(str2, char), (size_t)n);
}

/*---------------------------------------------------------------------------*/

uint32_t blib_strftime(char_t *dest, const uint32_t size, const char_t *format, const int16_t year, const uint8_t month, const uint8_t mday, const uint8_t wday, const uint8_t hour, const uint8_t minute, const uint8_t second)
{
    struct tm tm;
    cassert_no_null(dest);
    cassert(size > 0);
    cassert(month >= 1 && month <= 12);
    cassert(mday >= 1 && mday <= 31);
    cassert(wday <= 6);
    cassert(hour < 24);
    cassert(minute < 60);
    cassert(second < 60);
    tm.tm_year = (int)(year - 1900);
    tm.tm_mon = (int)(month - 1);
    tm.tm_mday = (int)mday;
    tm.tm_wday = (int)wday;
    tm.tm_hour = (int)hour;
    tm.tm_min = (int)minute;
    tm.tm_sec = (int)second;
    return (uint32_t)strftime(cast(dest, char), (size_t)size, cast_const(format, char), &tm);
}

/*---------------------------------------------------------------------------*/

int64_t blib_strtol(const char_t *str, char_t **endptr, uint32_t base, bool_t *err)
{
#if _MSC_VER > 1700
    int64_t v = strtoll(cast_const(str, char), dcast(endptr, char), (int)base);
#else
    int64_t v = strtol(cast_const(str, char), dcast(endptr, char), (int)base);
#endif

    if (err != NULL)
    {
        if (errno == ERANGE)
            *err = TRUE;
        else
            *err = FALSE;
    }

    return v;
}

/*---------------------------------------------------------------------------*/

uint64_t blib_strtoul(const char_t *str, char_t **endptr, uint32_t base, bool_t *err)
{
#if defined(_MSC_VER)
#if _MSC_VER > 1700
    uint64_t v = strtoull(cast_const(str, char), dcast(endptr, char), (int)base);
#else
    uint64_t v = strtoul(cast_const(str, char), dcast(endptr, char), (int)base);
#endif
#else
    uint64_t v = strtoull(cast_const(str, char), dcast(endptr, char), (int)base);
#endif

    if (err != NULL)
    {
        if (errno == ERANGE)
            *err = TRUE;
        else
            *err = FALSE;
    }

    return v;
}

/*---------------------------------------------------------------------------*/

real32_t blib_strtof(const char_t *str, char_t **endptr, bool_t *err)
{
#if defined(_MSC_VER)
#if _MSC_VER > 1700
    real32_t v = (real32_t)strtof(cast_const(str, char), dcast(endptr, char));
#else
    real32_t v = (real32_t)atof(cast_const(str, char));
    unref(endptr);
#endif
#else
    real32_t v = (real32_t)strtof(cast_const(str, char), dcast(endptr, char));
#endif

    if (err != NULL)
    {
        if (errno == ERANGE)
            *err = TRUE;
        else
            *err = FALSE;
    }

    return v;
}

/*---------------------------------------------------------------------------*/

real64_t blib_strtod(const char_t *str, char_t **endptr, bool_t *err)
{
#if defined(_MSC_VER)
#if _MSC_VER >= 1100
    real64_t v = (real64_t)strtod(cast_const(str, char), dcast(endptr, char));
#elif _MSC_VER > 1004
    real64_t v = (real64_t)atod(cast_const(str, char));
    unref(endptr);
#else
    real64_t v = (real64_t)atof(cast_const(str, char));
    unref(endptr);
#endif
#else
    real64_t v = (real64_t)strtod(cast_const(str, char), dcast(endptr, char));
#endif

    if (err != NULL)
    {
        if (errno == ERANGE)
            *err = TRUE;
        else
            *err = FALSE;
    }

    return v;
}

/*---------------------------------------------------------------------------*/

void blib_qsort(byte_t *array, const uint32_t nelems, const uint32_t size, FPtr_compare func_compare)
{
    cassert_no_nullf(func_compare);
    qsort(cast(array, void), (size_t)nelems, (size_t)size, func_compare);
}

/*---------------------------------------------------------------------------*/

void blib_qsort_ex(const byte_t *array, const uint32_t nelems, const uint32_t size, FPtr_compare_ex func_compare, const byte_t *data)
{
    cassert_no_nullf(func_compare);
    _qsort_ex(cast_const(array, void), nelems, size, func_compare, cast_const(data, void));
}

/*---------------------------------------------------------------------------*/

bool_t blib_bsearch(const byte_t *array, const byte_t *key, const uint32_t nelems, const uint32_t size, FPtr_compare func_compare, uint32_t *pos)
{
    uint32_t st, ed;
    int compare;

    if (nelems == 0)
    {
        ptr_assign(pos, 0);
        return FALSE;
    }

    st = 0;
    ed = nelems - 1;

    /* Check if first is bigger than 'elem' */
    compare = func_compare(array, key);
    if (compare > 0)
    {
        ptr_assign(pos, 0);
        return FALSE;
    }
    else if (compare == 0)
    {
        ptr_assign(pos, 0);
        return TRUE;
    }

    /* Check if last is smaller than 'elem' */
    if (nelems > 1)
        compare = func_compare(array + (ed * size), key);

    if (compare < 0)
    {
        ptr_assign(pos, nelems);
        return FALSE;
    }
    else if (compare == 0)
    {
        cassert(nelems > 1);
        ptr_assign(pos, ed);
        return TRUE;
    }

    /* Always data[st] is less than 'elem' & data[ed] is greather than 'elem' */
    for (;;)
    {
        /* 'elem' doesn't exists. Its go after [st] */
        if (ed - st == 1)
        {
            ptr_assign(pos, st + 1);
            return FALSE;
        }
        else
        {
            uint32_t mid = (ed + st) / 2;
            cassert(mid > st && mid < ed);
            compare = func_compare(array + (mid * size), key);
            if (compare < 0)
            {
                st = mid;
            }
            else if (compare > 0)
            {
                ed = mid;
            }
            else
            {
                ptr_assign(pos, mid);
                return TRUE;
            }
        }
    }
}

/*---------------------------------------------------------------------------*/

bool_t blib_bsearch_ex(const byte_t *array, const byte_t *key, const uint32_t nelems, const uint32_t size, FPtr_compare_ex func_compare, const byte_t *data, uint32_t *pos)
{
    uint32_t st, ed;
    int compare;

    if (nelems == 0)
    {
        ptr_assign(pos, 0);
        return FALSE;
    }

    st = 0;
    ed = nelems - 1;

    /* Check if first is bigger than 'elem' */
    compare = func_compare(array, key, data);
    if (compare > 0)
    {
        ptr_assign(pos, 0);
        return FALSE;
    }
    else if (compare == 0)
    {
        ptr_assign(pos, 0);
        return TRUE;
    }

    /* Check if last is smaller than 'elem' */
    if (nelems > 1)
        compare = func_compare(array + (ed * size), key, data);

    if (compare < 0)
    {
        ptr_assign(pos, nelems);
        return FALSE;
    }
    else if (compare == 0)
    {
        cassert(nelems > 1);
        ptr_assign(pos, ed);
        return TRUE;
    }

    /* Always data[st] is less than 'elem' & data[ed] is greather than 'elem' */
    for (;;)
    {
        /* 'elem' doesn't exists. Its go after [st] */
        if (ed - st == 1)
        {
            ptr_assign(pos, st + 1);
            return FALSE;
        }
        else
        {
            uint32_t mid = (ed + st) / 2;
            cassert(mid > st && mid < ed);
            compare = func_compare(array + (mid * size), key, data);
            if (compare < 0)
            {
                st = mid;
            }
            else if (compare > 0)
            {
                ed = mid;
            }
            else
            {
                ptr_assign(pos, mid);
                return TRUE;
            }
        }
    }
}

/*---------------------------------------------------------------------------*/

#if defined(_MSC_VER)
#pragma warning(disable : 4996)
#endif
const char_t *blib_getenv(const char_t *name)
{
    cassert_no_null(name);
    return cast_const(getenv(cast_const(name, char)), char_t);
}

/*---------------------------------------------------------------------------*/

int32_t blib_setenv(const char_t *name, const char_t *value)
{
    cassert_no_null(name);
    cassert_no_null(value);
#if defined(__WINDOWS__)
    return (int32_t)_putenv_s(cast_const(name, char), cast_const(value, char));
#else
    return (int32_t)setenv(cast_const(name, char), cast_const(value, char), 1);
#endif
}

#if defined(_MSC_VER)
#pragma warning(default : 4996)
#endif

/*---------------------------------------------------------------------------*/

void blib_atexit(void (*func)(void))
{
    _sewer_atexit(func);
}

/*---------------------------------------------------------------------------*/

void blib_abort(void)
{
    abort();
}

/*---------------------------------------------------------------------------*/

void blib_exit(int code)
{
    exit(code);
}

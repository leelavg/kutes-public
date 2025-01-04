/*
 a proxy allocator providing malloc api on top of sdk for auditing memory,
 only single threaded at the moment.
*/
#include <kt.h>

typedef struct _addr_t addr;
typedef struct _context_t context;

struct _addr_t
{
    void *ptr;
    size_t size;
};
DeclSt(addr);

struct _context_t
{
    char_t name[10];
    SetSt(addr) *set;
};

static void *sdk_malloc(void *ctx, size_t size)
{

    context *ct = cast(ctx, context);
    addr *val, key;
    void *ptr = heap_malloc_imp(size, ct->name, FALSE);

    key.ptr = ptr;
    val = setst_insert(ct->set, &key, addr);
    if (!val)
        val = setst_get(ct->set, &key, addr);
    val->ptr = ptr;
    val->size = size;
    return ptr;
}

static void *sdk_realloc(void *ctx, void *ptr, size_t old_size, size_t size)
{
    context *ct = cast(ctx, context);
    addr *val, key;
    void *newptr = heap_realloc(ptr, old_size, size, cast(ctx, context)->name);

    key.ptr = ptr;
    if (ptr == newptr)
    {
        val = setst_get(ct->set, &key, addr);
        cassert_no_null(val);
    }
    else
    {
        setst_delete(ct->set, &key, NULL, addr);
        key.ptr = newptr;
        val = setst_insert(ct->set, &key, addr);
        val->ptr = newptr;
    }
    val->size = size;
    return newptr;
}

static void sdk_free(void *ctx, void *ptr)
{
    context *ct = cast(ctx, context);
    addr *val, key;
    key.ptr = ptr;
    val = setst_get(ct->set, &key, addr);
    cassert_no_null(val);
    heap_free(dcast(&ptr, byte_t), val->size, ct->name);
    setst_delete(ct->set, &key, NULL, addr);
}

static int addr_cmp(const addr *a, const addr *b)
{
    if (a->ptr < b->ptr)
        return -1;
    else if (a->ptr > b->ptr)
        return 1;
    else
        return 0;
}

yyjson_alc *alc_init(const char_t *name)
{
    context *ct = heap_new(context);
    struct yyjson_alc *alc = heap_new(yyjson_alc);
    alc->malloc = sdk_malloc;
    alc->realloc = sdk_realloc;
    alc->free = sdk_free;

    str_copy_c(ct->name, sizeof(ct->name), name);
    ct->set = setst_create(addr_cmp, addr);
    alc->ctx = ct;
    return alc;
}

void alc_dest(yyjson_alc **alc)
{
    context *ct = cast((*alc)->ctx, context);
    setst_destroy(&ct->set, NULL, addr);
    heap_delete(&ct, context);
    heap_delete(alc, yyjson_alc);
}

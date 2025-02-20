#include "kt.h"
#include <rax.h>

struct _history_t
{
    Stream *stm;
    rax *rt;
    raxIterator rit;
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

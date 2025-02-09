/*
 * NAppGUI Cross-platform C SDK
 * 2015-2025 Francisco Garcia Collado
 * MIT Licence
 * https://nappgui.com/en/legal/license.html
 *
 * File: buffer.c
 *
 */

/* Fixed size memory buffers */

#include "buffer.h"
#include "heap.h"
#include "stream.h"
#include <sewer/bmem.h>
#include <sewer/cassert.h>

/*---------------------------------------------------------------------------*/

#define i_SIZE(buffer) *cast(buffer, uint32_t)
#define i_DATA(buffer) cast(buffer, byte_t) + sizeof(uint32_t)

/*---------------------------------------------------------------------------*/

Buffer *buffer_create(const uint32_t size)
{
    Buffer *buffer = cast(heap_malloc(size + sizeof32(uint32_t), "Buffer"), Buffer);
    i_SIZE(buffer) = size;
    return buffer;
}

/*---------------------------------------------------------------------------*/

Buffer *buffer_with_data(const byte_t *data, const uint32_t size)
{
    Buffer *buffer = buffer_create(size);
    bmem_copy(i_DATA(buffer), data, size);
    return buffer;
}

/*---------------------------------------------------------------------------*/

Buffer *buffer_read(Stream *stream)
{
    uint32_t size = stm_read_u32(stream);
    Buffer *buffer = buffer_create(size);
    stm_read(stream, buffer_data(buffer), size);
    return buffer;
}

/*---------------------------------------------------------------------------*/

void buffer_destroy(Buffer **buffer)
{
    cassert_no_null(buffer);
    cassert_no_null(*buffer);
    heap_free(dcast(buffer, byte_t), i_SIZE(*buffer) + sizeof32(uint32_t), "Buffer");
}

/*---------------------------------------------------------------------------*/

uint32_t buffer_size(const Buffer *buffer)
{
    cassert_no_null(buffer);
    return i_SIZE(buffer);
}

/*---------------------------------------------------------------------------*/

byte_t *buffer_data(Buffer *buffer)
{
    cassert_no_null(buffer);
    return i_DATA(buffer);
}

/*---------------------------------------------------------------------------*/

const byte_t *buffer_const(const Buffer *buffer)
{
    cassert_no_null(buffer);
    return i_DATA(buffer);
}

/*---------------------------------------------------------------------------*/

void buffer_write(Stream *stream, const Buffer *buffer)
{
    cassert_no_null(buffer);
    stm_write_u32(stream, i_SIZE(buffer));
    stm_write(stream, i_DATA(buffer), i_SIZE(buffer));
}

/*
  Copyright 2010,2013 Karl Robillard

  This file is part of the Boron programming language.

  Boron is free software: you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Boron is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with Boron.  If not, see <http://www.gnu.org/licenses/>.
*/


#define setBit(mem,n)   (mem[(n)>>3] |= 1<<((n)&7))


static void ur_binAppendInt( UBuffer* bin, uint32_t n,
                             UAtom size, int bigEndian )
{
    uint8_t* bp;

    ur_binReserve( bin, bin->used + 4 );
    bp = bin->ptr.b + bin->used;

    switch( size )
    {
        case UR_ATOM_U8:
            *bp = n;
            bin->used += 1;
            break;

        case UR_ATOM_U16:
            if( bigEndian )
            {
                *bp++ = n >> 8;
                *bp   = n;
            }
            else
            {
                *bp++ = n;
                *bp   = n >> 8;
            }
            bin->used += 2;
            break;

        case UR_ATOM_U32:
            if( bigEndian )
            {
                *bp++ = n >> 24;
                *bp++ = n >> 16;
                *bp++ = n >> 8;
                *bp   = n;
            }
            else
            {
                *bp++ = n;
                *bp++ = n >> 8;
                *bp++ = n >> 16;
                *bp   = n >> 24;
            }
            bin->used += 4;
            break;
    }
}


#ifdef __BIG_ENDIAN__
#define NATIVE_ENDIAN   1
#else
#define NATIVE_ENDIAN   0
#endif

union ConNum32 {
    uint32_t i;
    float f;
};

typedef struct {
    UBuffer* bin;
    int big;
    UAtom size;
} BinConstructState;

static int binary_construct2(UThread* ut, BinConstructState* cs,
                             const UCell* cell)
{
switch_cell:
    switch (ur_type(cell))
    {
        case UT_INT:
            ur_binAppendInt(cs->bin, ur_int(cell), cs->size, cs->big);
            break;

        case UT_DOUBLE:
        {
            // Emit as 32-bit float for now.
            union ConNum32 n;
            n.f = ur_double(cell);
            ur_binAppendInt(cs->bin, n.i, UR_ATOM_U32, cs->big);
        }
            break;

        case UT_COORD:
        {
            int i, len = cell->coord.len;
            for (i = 0; i < len; ++i)
                ur_binAppendInt(cs->bin, cell->coord.n[i], cs->size, cs->big);
        }
            break;

        case UT_VEC3:
            if (cs->big == NATIVE_ENDIAN)
                ur_binAppendData(cs->bin, (const uint8_t*) cell->vec3.xyz, 12);
            else {
                union ConNum32 n;
                int i;
                for (i = 0; i < 3; ++i) {
                    n.f = cell->vec3.xyz[i];
                    ur_binAppendInt(cs->bin, n.i, UR_ATOM_U32, cs->big);
                }
            }
            break;

        case UT_WORD:
            switch (ur_atom(cell))
            {
                case UR_ATOM_U8:
                case UR_ATOM_U16:
                case UR_ATOM_U32:
                    cs->size = ur_atom(cell);
                    break;

                case UR_ATOM_BIG_ENDIAN:
                    cs->big = 1;
                    break;

                case UR_ATOM_LITTLE_ENDIAN:
                    cs->big = 0;
                    break;

                default:
                    if( ! (cell = ur_wordCell(ut, cell)) )
                        return UR_THROW;
                    goto switch_cell;
            }
            break;

        case UT_BINARY:
        {
            UBinaryIter b2;
            ur_binSlice(ut, &b2, cell);
            if (b2.end > b2.it)
                ur_binAppendData(cs->bin, b2.it, b2.end - b2.it);
        }
            break;

        case UT_BLOCK:
        {
            BinConstructState cs2 = *cs;
            UBlockIt b2;
            ur_blockIt(ut, &b2, cell);
            ur_foreach(b2) {
                if (binary_construct2(ut, &cs2, b2.it) == UR_THROW)
                    return UR_THROW;
            }
        }
            break;

        default:
            return ur_error(ut, UR_ERR_TYPE,
                            "Invalid construct binary! type (%s)",
                            ur_atomCStr(ut, ur_type(cell)));
    }
    return UR_OK;
}


static int binary_construct( UThread* ut, const UCell* blkC, const UCell* binC )
{
    BinConstructState cs;
    UBlockIt bi;

    cs.big  = 0;
    cs.size = UR_ATOM_U32;
    cs.bin  = ur_bufferSerM(binC);
    if (! cs.bin)
        return UR_THROW;

    ur_blockIt(ut, &bi, blkC);
    ur_foreach(bi) {
        if (binary_construct2(ut, &cs, bi.it) == UR_THROW)
            return UR_THROW;
    }
    return UR_OK;
}


extern int string_append( UThread* ut, UBuffer* buf, const UCell* val );

static int sc_appendEval( UThread* ut, UBuffer* str, const UCell* val )
{
    if( ur_is(val, UT_WORD) )
    {
        if( ! (val = ur_wordCell( ut, val )) )
            return UR_THROW;
        if( ur_is(val, UT_NONE) )
            return UR_OK;
    }
    return string_append( ut, str, val );
}


/*
    Create output string from input with changes.

    The plan block is simply a list of search and replace values.
*/
static int string_construct( UThread* ut, UBuffer* str,
                             const UCell* inputC, const UCell* blkC )
{
#define SC_BIT_COUNT    256
    USeriesIter in;
    UBlockIt bi;
    UBuffer cset;
    const UCell* begin;
    UIndex copyPos;
    int ch;
    int fp;


    ur_blockIt( ut, &bi, blkC );
    begin = bi.it;
    if( (bi.end - bi.it) & 1 )
        --bi.end;

    // Create bitset of search value characters.
    ur_binInit( &cset, SC_BIT_COUNT / 8 );
    cset.used = SC_BIT_COUNT / 8;
    memSet( cset.ptr.b, 0, cset.used );
    for( ; bi.it != bi.end; bi.it += 2 )
    {
        if( ur_is(bi.it, UT_CHAR) )
        {
            ch = ur_int(bi.it);
        }
        else if( ur_is(bi.it, UT_STRING) )
        {
            USeriesIter si;
            ur_seriesSlice( ut, &si, bi.it );
            ch = ur_strChar( si.buf, si.it );
        }
        else
        {
            ch = -1;
        }

        if( ch >= 0 && ch < SC_BIT_COUNT )
            setBit( cset.ptr.b, ch );
    }

    // Append to str with replacements.
    ur_seriesSlice( ut, &in, inputC );
    copyPos = in.it;
    while( (fp = ur_strFindChars(in.buf, in.it, in.end, cset.ptr.b, cset.used))
            > -1 )
    {
        ch = ur_strIsUcs2(in.buf) ? in.buf->ptr.u16[ fp ]
                                  : in.buf->ptr.b[ fp ];

        for( bi.it = begin; bi.it != bi.end; bi.it += 2 )
        {
            if( ur_is(bi.it, UT_CHAR) )
            {
                if( ch == ur_int(bi.it) )
                {
                    in.it = fp + 1;
match:
                    ur_strAppend( str, in.buf, copyPos, fp );
                    copyPos = in.it;
                    if( sc_appendEval( ut, str, bi.it + 1 ) != UR_OK )
                        return UR_THROW;
                    break;
                }
            }
            else if( ur_is(bi.it, UT_STRING) )
            {
                USeriesIter inA;
                USeriesIter si;
                int len;

                ur_seriesSlice( ut, &si, bi.it );
                len = si.end - si.it;

                inA.buf = in.buf;
                inA.it  = fp;
                inA.end = in.end;

                if( ur_strMatch( &inA, &si, 0 ) == len )
                {
                    in.it = fp + len;
                    goto match;
                }
            }
        }
        if( bi.it == bi.end )
            ++in.it;
    }
    ur_strAppend( str, in.buf, copyPos, in.end );

    ur_binFree( &cset );
    return UR_OK;
}


UStatus bitset_construct( UThread* ut, const UCell* strC, UCell* res )
{
    uint8_t* bits;
    int c, prev, range;
    int cmax = 0;
    USeriesIter si;
    UIndex start;

    ur_seriesSlice( ut, &si, strC );
    start = si.it;
    ur_foreach( si )
    {
        c = ur_strChar( si.buf, si.it ) + 1;
        if( cmax < c )
            cmax = c;
    }
    if( cmax && cmax < 256 )
        cmax = 256;

    bits = ur_makeBitsetCell( ut, cmax, res )->ptr.b;   // gc!
    if( bits )
    {
        si.buf = ur_bufferSer( strC );  // Re-aquire buf.

        prev = range = -1;
        si.it = start;
        ur_foreach( si )
        {
            c = ur_strChar( si.buf, si.it );
            if (c == '-') {
                if (prev >= 0 && range < 0) {
                    range = prev;
                } else {
                    range = -1;
                    setBit(bits, c);
                }
            } else if (range >= 0) {
                if (range > c) {
                    cmax = range;
                    range = c;
                    c = cmax;
                }
                for (; range <= c; ++range)
                    setBit(bits, range);
                prev = range = -1;
            } else {
                if (prev >= 0)
                    setBit(bits, prev);
                prev = c;
            }
        }
        if (range >= 0)
            setBit(bits, '-');
        if (prev >= 0)
            setBit(bits, prev);
    }
    return UR_OK;
}


extern int bitset_make( UThread* ut, const UCell* from, UCell* res );

/*-cf-
    construct
        object  datatype!/binary!/string!
        plan    string!/block!
    return: New value.
    group: data

    Make or append values with a detailed specification.

    The string! plan is simply pairs of search & replace values applied
    to a copy of an existing string.  Both the search pattern and replacement
    value types must be char! or string!.

        construct "$NAME travels > $CITY" [
            '>'     "to"
            "$NAME" "Joseph"
            "$CITY" "Paris"
        ]
        == "Joseph travels to Paris"

    It is much more efficient to replace text using construct than calling the
    replace function multiple times.  In the future more rules may be added.

    For bitset! the plan is a string in which dash characters denote a range
    of bits to set. Use two consecutive dashes to include the dash character
    itself.  For example, a bitset of hexidecimal characters would be:

        construct bitset! "a-fA-F0-9"

    A binary! plan concatenates numbers with specified byte sizes and endianess.
    These default to 'u32 and 'little-endian respectively.
    Any words and blocks will be evaluated recursively.
    Double! and vec3! values are always output as 32-bit floats.

        header: [big-endian 0x11 0x2233]
        data:   [u16 0x11 0x2233]
        construct binary! [header data]
        == #{000000110000223311003322}
*/
CFUNC(cfunc_construct)
{
    const UCell* plan = a1+1;
    const UBuffer* in;
    int dt;

    switch( ur_type(a1) )
    {
        case UT_DATATYPE:
            dt = ur_datatype(a1);
            if( dt == UT_BINARY )
            {
                ur_makeBinaryCell( ut, 0, res );
                goto con_binary;
            }
            else if( dt == UT_BITSET )
            {
                if( ur_is(plan, UT_STRING) )
                    return bitset_construct( ut, plan, res );
                if( ur_is(plan, UT_CHAR) )
                    return bitset_make( ut, plan, res );
                goto bad_plan;
            }
            break;

        case UT_BINARY:
            *res = *a1;
con_binary:
            if( ! ur_is(plan, UT_BLOCK) )
                goto bad_plan1;
            return binary_construct( ut, plan, res );

        case UT_STRING:
            if( ! ur_is(plan, UT_BLOCK) )
                goto bad_plan1;
            in = ur_bufferSer(a1);
            return string_construct( ut,
                    ur_makeStringCell( ut, in->form, in->used, res ),  // gc!
                    a1, plan );
    }
    return boron_badArg( ut, ur_type(a1), 0 );

bad_plan1:
    dt = ur_type(a1);
bad_plan:
    return ur_error( ut, UR_ERR_TYPE, "Invalid plan type for construct %s",
                     ur_atomCStr(ut,dt) );
}


//EOF

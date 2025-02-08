/*
  Copyright 2009,2012,2021,2024 Karl Robillard

  This file is part of the Urlan datatype system.

  Urlan is free software: you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Urlan is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with Urlan.  If not, see <http://www.gnu.org/licenses/>.
*/


#include <assert.h>
#include <stddef.h>
#include "urlan.h"


/** \defgroup dt_path Datatype Path
  \ingroup urlan
  @{
*/ 


// Return 0, UR_SELECT_ATOM, or UR_SELECT_INT.
static int _selectValue( const UCell* cell, UAtom* atom )
{
    if( ur_is(cell, UT_INT) )
    {
        int64_t n = ur_int(cell);
        if( (n & 0xffff) == n )
        if( n < 0x8000 && n >= -32768 )
        {
            *atom = n;
            return UR_SELECT_INT;
        }
    }
    else if( ur_is(cell, UT_WORD) )
    {
        *atom = ur_atom(cell);
        return UR_SELECT_ATOM;
    }
    return 0;
}


/**
  Initialize path cell and generate block if needed.

  The nodes & cell argument may point to the same UCell.

  \param nodes  Source cells starting with word!/lit-word!.
  \param size   Number of path nodes to copy.
  \param cell   Cell to initialize.

  \return  Pointer to block buffer or NULL if contained within UCellWord.
*/
UBuffer* ur_makePathCell( UThread* ut, const UCell* nodes, int size,
                          UCell* cell )
{
    UBuffer* buf;
    UIndex bufN;
    int dt = ur_type(nodes);
    UAtom sel[2];
    int st1, st2;


    // Check if nodes can fit in UCellWord.
    if( size < 4 && dt != UT_GETWORD )
    {
        if( size == 3 )
        {
            st2 = _selectValue( nodes+2, sel+1 );
            if( ! st2 )
                goto path_block;
        }
        else
        {
            st2 = 0;
            sel[1] = 0;
        }
        st1 = _selectValue( nodes+1, sel );
        if( ! st1 )
            goto path_block;

        if( cell != nodes )
            *cell = *nodes;
        ur_type(cell) = (dt == UT_LITWORD) ? UT_LITPATH : UT_PATH;
        cell->word.selType = (st2 << 2) | st1;
        cell->word.sel[0] = sel[0];
        cell->word.sel[1] = sel[1];
        return NULL;
    }

path_block:
    dt = (dt == UT_LITWORD) ? UT_LITPATH : UT_PATH;

    buf = ur_genBuffers( ut, 1, &bufN );    // gc!
    ur_blkInit( buf, dt, size );
    ur_blkAppendCells( buf, cell, size );

    if( dt == UT_LITPATH )
        buf->ptr.cell->id.type = UT_WORD;   // Change UT_LITWORD to UT_WORD.

    ur_initSeries( cell, dt, bufN );
    return buf;
}


#define WORD_UNSET_MSG  "Path word '%s is unset"
#define UNSEL_TYPE_MSG  "Unselectable type %s"

/**
  Get a pointer to the last value that a path! refers to.

  If the last path node is not a datatype that contains UCells then lastCell
  will be set to tmp.

  \param pi         Path iterator.
  \param tmp        Cell to temporarily hold values for datatypes that don't
                    contain UCells.
  \param lastCell   Set to the address of the cell holding the last value in
                    the path or tmp.

  \return Type of the first node (UT_WORD or UT_GETWORD), or UR_THROW if
          path evaluation fails.

  \sa ur_pathCell, ur_wordCell
*/
int ur_pathResolve( UThread* ut, UBlockIt* pi, UCell* tmp, UCell** lastCell )
{
    const UCell* obj = 0;
    const UCell* selector;
    const UCell* (*selectf)( UThread*, const UCell*, const UCell*, UCell* );
    int type;

    if( pi->it == pi->end )
    {
bad_word:
        return ur_error( ut, UR_ERR_SCRIPT,
                         "Path must start with a word!/get-word!");
    }

    type = ur_type(pi->it);
    if( type != UT_WORD && type != UT_GETWORD )
        goto bad_word;

    if( ! (obj = ur_wordCell( ut, pi->it )) )
        return UR_THROW;
    if( ur_is(obj, UT_UNSET) )
        return ur_error(ut, UR_ERR_SCRIPT, WORD_UNSET_MSG, ur_wordCStr(pi->it));

    while( ++pi->it != pi->end )
    {
        // If the select method is NULL, return obj as the result and leave
        // pi->it pointing to the untraversed path segments.
        selectf = ut->types[ ur_type(obj) ]->select;
        if( ! selectf )
            break;

        selector = pi->it;
        if( ur_is(selector, UT_GETWORD) )
        {
            if( ! (selector = ur_wordCell( ut, selector )) )
                return UR_THROW;
        }

        obj = selectf( ut, obj, selector, tmp );
        if( ! obj )
            return UR_THROW;
    }

    *lastCell = (UCell*) obj;
    return type;
}


static const UAtom* _pathSelectCell(int stype, const UAtom* sel, UCell* cell)
{
    if( (stype & 3) == UR_SELECT_ATOM ) {
        ur_setId(cell, UT_WORD);
        ur_setWordUnbound(cell, *sel);
    } else {
        ur_setId(cell, UT_INT);
        ur_int(cell) = *((int16_t*) sel);
    }
    return ++sel;
}


/**
 * \param dest  Cells to fill with path nodes.  Must have enough space to
 *              hold three UCells.
 *
 * \return Number of path nodes.
 */
int ur_pathSelectCells(const UCell* selC, UCell* dest)
{
    UCell* it = dest;
    const UAtom* sval = selC->word.sel;
    int stype = selC->word.selType /*& 15*/;
    assert(stype);

    *it = *selC;
    it->word.type = UT_WORD;
    ++it;

    do {
        sval = _pathSelectCell(stype, sval, it++);
        stype >>= 2;
    } while( stype );
    return it - dest;
}


/**
  Get a pointer to the last value that a select path! cell refers to.

  \param tmp        Cell to temporarily hold values for datatypes that don't
                    contain UCells.
  \param remain     An expanded (UCell series) version of the remaining path
                    is put at this address then the pointer is set to the end
                    position.  It must have enough space to hold two UCells.

  \return Address of the cell holding the last value in the path (or tmp).
          NULL is returned if an error was thrown.
*/
const UCell* ur_pathSelect( UThread* ut, const UCell* selC, UCell* tmp,
                            UCell** remain )
{
    int stype;
    int useRemain = 0;
    const UAtom* sval;
    const UCell* (*selectf)( UThread*, const UCell*, const UCell*, UCell* );
    UCell* psel = *remain;
    const UCell* obj = ur_wordCell( ut, selC );
    if( ! obj )
        return NULL;
    if( ur_is(obj, UT_UNSET) ) {
        ur_error(ut, UR_ERR_SCRIPT, WORD_UNSET_MSG, ur_wordCStr(selC));
        return NULL;
    }

    sval  = selC->word.sel;
    stype = selC->word.selType /*& 15*/;
    do
    {
        sval = _pathSelectCell(stype, sval, psel);

        if (useRemain) {
            *remain = ++psel;
            break;
        }
        selectf = ut->types[ ur_type(obj) ]->select;
        if( ! selectf ) {
            // If the select method is NULL, return obj as the result and fill
            // remain with the untraversed path segments.
            useRemain = 1;
            *remain = ++psel;
        } else {
            obj = selectf( ut, obj, psel, tmp );
            if( ! obj )
                return NULL;
        }

        stype >>= 2;
    }
    while( stype );

    return obj;
}


// NOTE: ur_pathCell isn't used internally but it may be needed by users.
#if 0
/**
  Get the value which a path refers to.

  \param path   Valid UT_PATH cell.
  \param res    Set to value at end of path.

  \return UT_WORD/UT_GETWORD/UR_THROW

  \sa ur_pathResolve, ur_wordCell
*/
int ur_pathCell( UThread* ut, const UCell* path, UCell* res )
{
    const UCell* last;
    int wordType;

    if( path->word.selType )
    {
        UCell tmp[2];
        UCell* remain = tmp;
        last = ur_pathSelect( ut, path, res, &remain );
        if( ! last )
            return UR_THROW;
        wordType = UT_WORD;
    }
    else
    {
        UBlockIt bi;
        ur_blockIt( ut, &bi, path );
        if( ! (wordType = ur_pathResolve( ut, &bi, res, (UCell**) &last )) )
            return UR_THROW;
    }

    if( last != res )
        *res = *last;
    return wordType;
}
#endif


/*
  Returns zero if word not found.
*/
static UCell* ur_blkSelectWord( UBuffer* buf, UAtom atom )
{
    // Should this work on cell and use UBlockIter?
    UCell* it  = buf->ptr.cell;
    UCell* end = it + buf->used;
    while( it != end )
    {
        // Checking atom first would be faster (it will fail more often and
        // is a quicker check), but the atom field may not be intialized
        // memory so memory checkers will report an error.
        if( ur_isWordType( ur_type(it) ) && (ur_atom(it) == atom) )
        {
            if( ++it == end )
                return 0;
            return it;
        }
        ++it;
    }
    return 0;
}


extern int coord_poke( UThread*, UCell* cell, int index, const UCell* src );
extern int vec3_poke ( UThread*, UCell* cell, int index, const UCell* src );

/**
  Set path.  This copies src into the cell which the path refers to.

  If any of the path words are unbound (or bound to the shared environment)
  then an error is generated and UR_THROW is returned.

  \param path   Valid path cell.
  \param src    Source value to copy.

  \return UR_OK/UR_THROW
*/
UStatus ur_setPath( UThread* ut, const UCell* path, const UCell* src )
{
    UBuffer* buf;
    UCell* last;
    UCell* res;
    int type, stype0;
    int index;

    if( (stype0 = path->word.selType) )
    {
        const UAtom* sel;
        int stype1;

        if( ! (last = (UCell*) ur_wordCell( ut, path )) )
            return UR_THROW;
        if( ur_is(last, UT_UNSET) )
        {
            return ur_error( ut, UR_ERR_SCRIPT, WORD_UNSET_MSG,
                             ur_wordCStr( path ) );
        }

        sel = path->word.sel;
        stype1 = stype0 & (3 << 2);
        if( stype1 )
        {
            const UCell* (*selectf)( UThread*, const UCell*, const UCell*,
                                     UCell* );
            UCell selector;
            UCell* psel = &selector;

            sel = _pathSelectCell(stype0, sel, psel);

            if( (selectf = ut->types[ ur_type(last) ]->select) )
            {
                res = ur_push( ut, UT_UNSET );
                last = (UCell*) selectf( ut, last, psel, res );
                ur_pop( ut );
                if( ! last )
                    return UR_THROW;
            }
            else
            {
                return ur_error( ut, UR_ERR_SCRIPT, UNSEL_TYPE_MSG,
                                 ur_atomCStr(ut, ur_type(last)) );
            }

            stype0 = stype1 >> 2;
        }
        else
        {
            stype0 &= 3;
        }

        switch( stype0 )
        {
            case UR_SELECT_INT:
                index = *((int16_t*) sel) - 1;
                goto set_integer;
            case UR_SELECT_ATOM:
                index = *sel;
                goto set_word;
        }
    }
    else
    {
        UBlockIt pi;
        const UBuffer* pbuf;

        // The last path node is not included in the resolve function as it is
        // handled differently for assignment.
#if 1
        pbuf = ur_bufferSer( path );
        pi.it  = pbuf->ptr.cell;
        pi.end = pi.it + pbuf->used - 1;
#else
        ur_blockIt( ut, &pi, path );
        --pi.end;
#endif

        res = ur_push( ut, UT_UNSET );
        type = ur_pathResolve( ut, &pi, res, &last );
        ur_pop( ut );
        if( type == UR_THROW )
            return UR_THROW;

        switch( ur_type(pi.it) )
        {
            case UT_INT:
                index = (int) ur_int(pi.it) - 1;
                goto set_integer;
            case UT_WORD:
                index = ur_atom(pi.it);
                goto set_word;
        }
    }
    goto bad_node;

set_integer:
    type = ur_type(last);
    if( ur_isSeriesType(type) )
    {
        if( ! (buf = ur_bufferSerM(last)) )
            return UR_THROW;
        index += last->series.it;
        ((USeriesType*) ut->types[ type ])->poke( buf, index, src );
        return UR_OK;
    }
    else if( type == UT_VEC3 )
        return vec3_poke( ut, last, index, src );
    else if( type == UT_COORD )
        return coord_poke( ut, last, index, src );
    goto bad_node;

set_word:
    // Here index holds the atom to lookup.
    type = ur_type(last);
    if( type == UT_CONTEXT )
    {
        if( ! (buf = ur_bufferSerM(last)) )
            return UR_THROW;
        index = ur_ctxLookup( buf, index );
        if( index < 0 )
            goto bad_node;
        buf->ptr.cell[ index ] = *src;
        return UR_OK;
    }
    else if( type == UT_BLOCK )
    {
        if( ! (buf = ur_bufferSerM(last)) )
            return UR_THROW;
        res = ur_blkSelectWord( buf, index );
        if( res )
        {
            *res = *src;
            return UR_OK;
        }
    }
    //goto bad_node;

bad_node:
    return ur_error( ut, UR_ERR_SCRIPT, "Cannot set path! (invalid node)" );
}


/* index is zero-based */
void path_pick( UThread* ut, const UCell* cell, int index, UCell* res )
{
    int stype = cell->word.selType;
    if (stype) {
#if 0
        if (index >= 0 && index < 3) {
            UCell tmp[3];
            int n = ur_pathSelectCells(cell, tmp);
            if (index < n) {
                *res = tmp[index];
                return;
            }
        }
#else
        switch (index) {
            case 0:
                *res = *cell;
                ur_type(res) = UT_WORD;
                return;
            case 1:
                _pathSelectCell(stype, cell->word.sel, res);
                return;
            case 2:
                stype >>= 2;
                if (stype) {
                    _pathSelectCell(stype, cell->word.sel + 1, res);
                    return;
                }
                break;
        }
#endif
    } else {
        // Same as block_pick().
        const UBuffer* buf = ur_bufferSer(cell);
        if (index > -1 && index < buf->used) {
            *res = buf->ptr.cell[ index ];
            return;
        }
    }
    ur_setId(res, UT_NONE);
}


/** @} */ 


//EOF

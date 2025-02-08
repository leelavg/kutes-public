/*
  Boron Evaluator
  Copyright 2016-2019,2023 Karl Robillard
*/


#include <assert.h>
#include <string.h>
#include "boron.h"
#include "i_parse_blk.c"
#include "mem_util.h"

//#define REPORT_EVAL


/** \struct UCellCFuncEval
  Structure of CFUNC a1 argument when boron_defineCFunc() signature is ":eval".
*/
/** \var UCellCFuncEval::avail
  Number of valid cells following pos.
*/
/** \var UCellCFuncEval::pos
  Block program counter.  This must be updated before the CFUNC exits.
*/
/** \def boron_evalPos
  Macro to access :eval function program counter from inside CFUNC.
*/
/** \def boron_evalAvail
  Macro to get the number of cells available for evaluation following
  UCellCFuncEval::pos inside CFUNC.
*/


#define UT(ptr)     ((UThread*) ptr)
#define PTR_I       (void*)(intptr_t)
#define cp_error    (void*)(intptr_t) ur_error


enum FuncArgumentOpcodes
{
    // Instruction       Data       Description
    // -------------------------------------------------------------------
    FO_clearLocal,    // n          Set locals on stack to UT_NONE.
    FO_fetchArg,      //            Push _eval1 result onto stack.
    FO_litArg,        //            Push UCell at current pc onto stack.
    FO_checkType,     // t          Check last argument type.
    FO_checkTypeMask, // w u16 ..   Check last argument type mask.
    FO_optionRecord,  //            Push options record cell.
    FO_variant,       // n          Push variant int! onto stack.
    FO_end            //            End function program.
};

// 16-bit alignment pad.
#define CHECK_TYPE_PAD  0x10


enum ArgRuleId
{
    AR_WORD, AR_TYPE, AR_LITW, AR_OPT, AR_LOCAL, AR_VARIANT
};


#define BAR     1   // compileAtoms[1]  "|"
#define LOCAL   2   // compileAtoms[2]  "local"
#define EXTERN  3   // compileAtoms[3]  "extern"
#define NOTRACE 4   // compileAtoms[4]  "no-trace"

// _argRules offsets
#define LWORD       46
#define LOCAL_OPT   LWORD+8
#define FIND_LOCAL  LWORD+16

static const uint8_t _argRules[ 76 ] =
{
    PB_SomeR, 3, PB_End,

    PB_Next, 6,
    PB_Rule, LOCAL_OPT, PB_AnyTs, LWORD, PB_ReportEnd, AR_LOCAL,

    PB_Next, 4,
    PB_Type, UT_WORD, PB_ReportEnd, AR_WORD,

    PB_Next, 4,
    PB_Type, UT_DATATYPE, PB_ReportEnd, AR_TYPE,

    PB_Next, 4,
    PB_Type, UT_LITWORD, PB_ReportEnd, AR_LITW,

    PB_Next, 6,
    PB_Type, UT_OPTION, PB_AnyTs, LWORD, PB_ReportEnd, AR_OPT,

    PB_Next, 4,
    PB_Type, UT_INT, PB_ReportEnd, AR_VARIANT,

    PB_Type, UT_STRING, PB_End,     // Ignore comment string!

    // LWORD word!/lit-word!/set-word! (set-word! only needed by funct)
    0x00,0xE0,0x00,0x00,0x00,0x00,0x00,0x00,

    // LOCAL_OPT
    PB_Next, 3,
    PB_LitWord, BAR,   PB_End,
    PB_LitWord, LOCAL, PB_End,

    // FIND_LOCAL
    PB_Next, 3,
    PB_ToLitWord, BAR,   PB_End,
    PB_ToLitWord, LOCAL, PB_End
};


typedef struct
{
    UIndex  atom;
    uint8_t optionIndex;
    uint8_t argCount;
    uint8_t programOffset;
    uint8_t _pad;
}
OptionEntry;


typedef struct
{
    uint16_t progOffset;
    uint8_t  funcFlags;
    uint8_t  optionCount;
    OptionEntry opt[ 1 ];
}
ArgProgHeader;


#define OPTION_FLAGS    id.ext
#define MAX_OPTIONS 8

typedef struct
{
    UBlockParser bp;
    UBuffer sval;           // Context of stack value bindings.
    UBuffer localWords;     // Array of atoms.
    UBuffer externWords;    // Array of atoms.
    UBuffer* bin;
    UIndex stackMapN;
    int origUsed;
    int argEndPc;
    int funcArgCount;
    int optionCount;
    OptionEntry opt[ MAX_OPTIONS ];
}
ArgCompiler;


int boron_copyWordValue( UThread* ut, const UCell* wordC, UCell* res )
{
    const UCell* cell;
    if( ! (cell = ur_wordCell( ut, wordC )) )
		return UR_THROW;
    *res = *cell;
    return UR_OK;
}


static void _defineArg( UBuffer* ctx, int binding, UIndex stackMapN,
                        UIndex atom, UIndex index, int16_t optArgN )
{
    UCell* cell;
    ur_ctxAppendWord( ctx, atom );
    cell = ctx->ptr.cell + ctx->used - 1;
    ur_setId( cell, UT_WORD );
    ur_binding(cell) = binding;
    cell->word.ctx   = stackMapN;
    cell->word.atom  = atom;
    cell->word.index = index;
    cell->word.sel[1] = optArgN;
}


#define EMIT(op)    prog->ptr.b[ prog->used++ ] = op

static void _argRuleHandler( UBlockParser* par, int rule,
                             const UCell* it, const UCell* end )
{
    ArgCompiler* ap = (ArgCompiler*) par;
    UBuffer* prog = ap->bin;
    int op;

    // TODO: Ensure all words in spec. are unique.

    //printf( "KR   arg rule %d (used %d)\n", rule, prog->used );
    switch( rule )
    {
        case AR_WORD:
            if( it->word.atom < UT_MAX )
            {
                EMIT( FO_checkType );
                EMIT( it->word.atom );
                break;
            }
            op = FO_fetchArg;
            goto emit_arg;

        case AR_TYPE:
            if( ur_datatype(it) == UT_TYPEMASK )
            {
                UIndex wpos;
                int which = 0;
                int wbit;
                int64_t mask = (((int64_t) it->datatype.mask1) << 32) |
                               it->datatype.mask0;

                // Type masks are not part of estimatedSize so they need
                // a dedicated reserve.
                ur_binReserve( prog, prog->used + 12 );

                EMIT( FO_checkTypeMask );
                wpos = prog->used;
                EMIT( 0 );
                if( prog->used & 1 )
                {
                    which |= CHECK_TYPE_PAD;
                    EMIT( 0 );
                }

                for( wbit = 1; wbit <= 4; wbit <<= 1 )
                {
                    if( mask & 0xffff )
                    {
                        which |= wbit;
                        *((uint16_t*) (prog->ptr.b + prog->used)) = mask;
                        prog->used += 2;
                    }
                    mask >>= 16;
                }

                prog->ptr.b[ wpos ] = which;
            }
            else
            {
                EMIT( FO_checkType );
                EMIT( ur_datatype(it) );
            }
            break;

        case AR_LITW:
            op = FO_litArg;
emit_arg:
            EMIT( op );
            if( ! ap->optionCount )
                ++ap->funcArgCount;
            if( ap->stackMapN )
                _defineArg( &ap->sval, BOR_BIND_FUNC, ap->stackMapN,
                            it->word.atom, ap->sval.used, 0 );
            break;

        case AR_OPT:
            if( it->word.atom == par->atoms[ EXTERN ] )
            {
                for( ++it; it != end; ++it )
                    ur_arrAppendInt32( &ap->externWords, it->word.atom );
                break;
            }
            else if( it->word.atom == par->atoms[ NOTRACE ] )
            {
                par->rflag |= FUNC_FLAG_NOTRACE;
                break;
            }

            if( ap->optionCount < MAX_OPTIONS )
            {
                OptionEntry* ent;
                int n;
                int argCount = 0;

                if( ap->stackMapN )
                    _defineArg( &ap->sval, BOR_BIND_OPTION, ap->stackMapN,
                                it->word.atom, ap->optionCount, 0 );

                if( ! ap->optionCount )
                {
                    ap->argEndPc = prog->used;
                    EMIT( FO_end );     // Terminate args.
                    EMIT( FO_end );     // Reserve space for FO_clearLocal.
                }

                ent = &ap->opt[ ap->optionCount++ ];
                ent->atom = it->word.atom;
                ent->optionIndex = ap->optionCount;
                ent->programOffset = 0;
                ent->_pad = 0;
                ++it;
                n = end - it;
                if( n )
                {
                    ent->programOffset = prog->used - ap->origUsed;
                    for( ; it != end; ++it )
                    {
                        if( it->word.atom < UT_MAX )
                        {
                            EMIT( FO_checkType );
                            EMIT( it->word.atom );
                        }
                        else
                        {
                            EMIT(ur_is(it,UT_LITWORD) ? FO_litArg:FO_fetchArg);
                            if( ap->stackMapN )
                            {
                                _defineArg( &ap->sval, BOR_BIND_OPTION_ARG,
                                            ap->stackMapN, it->word.atom,
                                            ap->optionCount - 1, argCount );
                            }
                            ++argCount;
                        }
                        // TODO: Handle lit-word!
                    }
                    EMIT( FO_end );
                }
                ent->argCount = argCount;
            }
            break;

        case AR_LOCAL:
            for( ++it; it != end; ++it )
                ur_arrAppendInt32( &ap->localWords, it->word.atom );
            break;

        case AR_VARIANT:
            EMIT( FO_variant );
            EMIT( ur_int(it) );
            break;
    }
}


#define AUTO_LOCALS
#ifdef AUTO_LOCALS
static void _appendSetWords( UThread* ut, UBuffer* buf, const UCell* blkC,
                             const UBuffer* argCtx )
{
    UBlockIt bi;
    ur_blockIt( ut, &bi, blkC );
    ur_foreach( bi )
    {
        int type = ur_type(bi.it);
        if( type == UT_SETWORD )
        {
            if( ur_ctxLookup( argCtx, ur_atom(bi.it) ) < 0 )
                ur_arrAppendInt32( buf, bi.it->word.atom );
        }
        else if( type == UT_BLOCK || type == UT_PAREN )
            _appendSetWords( ut, buf, bi.it, argCtx );
    }
}
#endif


static void _zeroDuplicateU32( UBuffer* a )
{
    uint32_t* it  = a->ptr.u32;
    uint32_t* end = it + a->used;
    for( ; it != (end - 1); ++it )
    {
        if( find_uint32_t( it + 1, end, *it ) )
            *it = 0;
    }
}


static void _zeroDiffU32( UBuffer* a, const UBuffer* b )
{
    uint32_t* it  = a->ptr.u32;
    uint32_t* end = it + a->used;
    const uint32_t* bIt  = b->ptr.u32;
    const uint32_t* bEnd = bIt + b->used;
    for( ; it != end; ++it )
    {
        if( find_uint32_t( bIt, bEnd, *it ) )
            *it = 0;
    }
}


static void _removeValueU32( UBuffer* buf, uint32_t val )
{
    uint32_t* it  = buf->ptr.u32;
    uint32_t* end = it + buf->used;
    uint32_t* out;

    it = (uint32_t*) find_uint32_t( it, end, val );
    if( ! it )
        return;
    for( out = it++; it != end; ++it )
    {
        if( *it != 0 )
            *out++ = *it;
    }
    buf->used = out - buf->ptr.u32;
}


extern const UAtom* boron_compileAtoms( BoronThread* );

/*
  Compile function argument fetching program.

  \param specC      Cell of specification UT_BLOCK slice.
  \param prog       Program is appended to this UT_BINARY buffer.
  \param bodyN      Buffer index of code block or 0 for cfunc!.
  \param sigFlags   Contents set to non-zero if /no-trace used in spec. block.

  The spec. block must only contain the following patterns:
      word!/lit-word!
      option! any word!/lit-word!
      '| any word!

  If bodyN is not zero, all argument and local words in the block will be
  bound to it.

  During evaluation the following cells can be placed on the UThread stack:

    [ opt-record ][ arguments ][ locals ][ opt args ][ optN args ]
                  ^
                  a1

  The stack map (or a1 CFUNC argument) points to the first argument cell.

  The option record indicates which options were evaluated and holds offsets to
  the optional agruments.  This is a single cell of type UT_UNSET.  Optional
  argument values are only present on the stack if the associated option was
  used and are ordered according to how the options occur in the invoking path!.
*/
void boron_compileArgProgram( BoronThread* bt, const UCell* specC,
                              UBuffer* prog, UIndex bodyN, int* sigFlags )
{
    UThread* ut = UT(bt);
    ArgCompiler ac;
    ArgProgHeader* head;
    const int headerSize = 4;
    int estimatedSize;
    int localCount = 0;         // Local values (no arguments).


    ac.origUsed = prog->used;
    ac.argEndPc = 0;
    ac.funcArgCount = 0;
    ac.optionCount = 0;
    ur_blockIt( ut, (UBlockIt*) &ac.bp.it, specC );

    estimatedSize = ac.bp.end - ac.bp.it;
    estimatedSize += (estimatedSize / 2) + 2;   // FO_checkType + FO_clearLocal
    estimatedSize = (estimatedSize + 3) & ~3;   // 32-bit align

    ur_binReserve( prog, prog->used + headerSize + estimatedSize);
    prog->used += headerSize;   // Reserve space for start of ArgProgHeader.

    //dumpBuf(ut, specC->series.buf);
    //printf("KR compile arg %ld (est %d bytes)\n",
    //       ac.bp.end - ac.bp.it, estimatedSize);

    if( ac.bp.it != ac.bp.end )
    {
        ac.bp.ut = ut;
        ac.bp.atoms  = boron_compileAtoms(bt);
        ac.bp.rules  = _argRules;
        ac.bp.report = _argRuleHandler;
        ac.bp.rflag  = 0;
        ac.bin = prog;
        ac.stackMapN = bodyN;
        ur_ctxInit( &ac.sval, 0 );
        ur_arrInit( &ac.localWords,  sizeof(uint32_t), 0 );
        ur_arrInit( &ac.externWords, sizeof(uint32_t), 0 );

        {
        const UCell* start = ac.bp.it;
        const UCell* end   = ac.bp.end;
        const UCell* local;
        if( ur_parseBlockI( &ac.bp, _argRules + FIND_LOCAL, start ) )
            local = ac.bp.end = ac.bp.it;
        else
            local = NULL;
        ac.bp.it = start;
        ur_parseBlockI( &ac.bp, ac.bp.rules, start );
        if( local )
        {
            ac.bp.it  = local;
            ac.bp.end = end;
            ur_parseBlockI( &ac.bp, ac.bp.rules, local );
        }
        }

        if( bodyN )
        {
#ifdef AUTO_LOCALS
            UCell tmp;
            ur_initSeries( &tmp, UT_BLOCK, bodyN );
            _appendSetWords( ut, &ac.localWords, &tmp, &ac.sval );
#endif
            if( ac.localWords.used > 1 )
                _zeroDuplicateU32( &ac.localWords );
            if( ac.externWords.used )
                _zeroDiffU32( &ac.localWords, &ac.externWords );
            _removeValueU32( &ac.localWords, 0 );

            if( ac.localWords.used )
            {
                const UIndex* ai = ac.localWords.ptr.i32;
                const UIndex* ae = ai + ac.localWords.used;
                int localIndex = ac.funcArgCount;

                while( ai != ae )
                {
                    //printf( "KR local %d %s\n", localIndex,
                    //        ur_atomCStr(ut, *ai) );
                    _defineArg( &ac.sval, BOR_BIND_FUNC, bodyN,
                                *ai++, localIndex++, 0 );
                }
                localCount = ac.localWords.used;
            }

            if( ac.sval.used )
            {
                const UBuffer* body = ur_bufferEnv(ut, bodyN);
                // ur_bindCopy uses UR_BIND_USER+2.
                assert(BOR_BIND_OPTION_ARG == UR_BIND_USER+2);

                ur_ctxSort( &ac.sval );
                ur_bindCopy( ut, &ac.sval,
                             body->ptr.cell, body->ptr.cell + body->used );
            }
        }
        ur_ctxFree( &ac.sval );
        ur_arrFree( &ac.localWords );
        ur_arrFree( &ac.externWords );

        // Insert OptionEntry table & FO_optionRecord.
        if( ac.optionCount )
        {
            OptionEntry* ent;
            OptionEntry* ee;
            const int optRecSize = 1;
            int tsize = ac.optionCount * sizeof(OptionEntry) + optRecSize;
            int newUsed = prog->used + tsize;

            ur_binReserve( prog, newUsed + 3 );     // +3 for alignment pad.
            ent = (OptionEntry*) (prog->ptr.b + ac.origUsed + headerSize);
            ee  = ent + ac.optionCount;
            memmove( ((char*) ent) + tsize, ent,
                     prog->used - ac.origUsed - headerSize );
            memcpy( ent, ac.opt, tsize - optRecSize );
            prog->used = newUsed;

            for( ; ent != ee; ++ent )
            {
                if( ent->programOffset )
                    ent->programOffset += tsize;
            }

            *((uint8_t*) ent) = FO_optionRecord;

            if( localCount )
            {
                uint8_t* endArg = prog->ptr.b + ac.argEndPc + tsize;
                *endArg++ = FO_clearLocal;
                *endArg = localCount;
            }
        }

        *sigFlags = ac.bp.rflag;
    }
    else
    {
        *sigFlags = 0;
    }

    if( ! ac.optionCount )
    {
        if( localCount )
        {
            EMIT( FO_clearLocal );
            EMIT( localCount );
        }
        else
            EMIT( FO_end );
    }

    // Pad to 32-bit align OptionEntry table when programs are concatenated.
    while( prog->used & 3 )
        EMIT( FO_end );


    head = (ArgProgHeader*) (prog->ptr.b + ac.origUsed);
    head->progOffset  = headerSize + ac.optionCount * sizeof(OptionEntry);
    head->funcFlags = *sigFlags;
    head->optionCount = ac.optionCount;

    if (bodyN && (ac.optionCount || ac.funcArgCount || localCount))
        head->funcFlags |= FUNC_FLAG_NEEDSTACK;
}


#if 0
extern void datatype_toString(UThread*, const UCell*, UBuffer* str, int depth);

void boron_argProgramToStr( UThread* ut, const void* prog, UBuffer* str )
{
    ArgProgHeader* head = (ArgProgHeader*) prog;
    const uint8_t* pc = ((const uint8_t*) head) + head->progOffset;
    int op;
    int start = 1;

    if (head->optionCount) {
        ur_strAppendInt(str, head->optionCount);
        ur_strAppendChar(str, ' ');
    }

    ur_strAppendChar( str, '[' );
    while( (op = *pc++) < FO_end )
    {
        if (start)
            start = 0;
        else
            ur_strAppendChar(str, ' ');

        switch( op )
        {
            case FO_clearLocal:
                op = *pc++;
                ur_strAppendChar( str, 'L' );
                ur_strAppendChar( str, op+'0' );
                break;

            case FO_fetchArg:
                ur_strAppendChar( str, 'a' );
                break;

            case FO_litArg:
                ur_strAppendCStr( str, "'a" );
                break;

            case FO_checkType:
                op = *pc++;
                ur_strAppendCStr( str, ur_atomCStr(ut, op) );
                break;

            case FO_checkTypeMask:
            {
                UCell dt;
                int64_t mask = 0;
                int which = *pc++;
                if( which & CHECK_TYPE_PAD )
                    ++pc;
                if( which & 1 )
                {
                    mask |= *((uint16_t*) pc);
                    pc += 2;
                }
                if( which & 2 )
                {
                    mask |= ((int64_t) *((uint16_t*) pc)) << 16;
                    pc += 2;
                }
                if( which & 4 )
                {
                    mask |= ((int64_t) *((uint16_t*) pc)) << 32;
                    pc += 2;
                }
                ur_setId(&dt, UT_DATATYPE);
                ur_datatype(&dt) = UT_TYPEMASK;
                dt.datatype.mask0 = mask;
                dt.datatype.mask1 = mask >> 32;
                datatype_toString( ut, &dt, str, 0 );
            }
                break;

            case FO_optionRecord:
                ur_strAppendChar( str, 'R' );
                break;

            case FO_variant:
                ur_strAppendInt( str, *pc++ );
                break;
        }
    }
    ur_strAppendChar( str, ']' );
}
#endif


/**
  Throw a standardized error for an unexpected function argument.

  \param atom   Datatype name.
  \param argN   Zero-based argument number.

  \return UR_THROW
*/
UStatus boron_badArg( UThread* ut, UIndex atom, int argN )
{
    return ur_error( ut, UR_ERR_TYPE, "Unexpected %s for argument %d",
                     ur_atomCStr(ut, atom), argN + 1 );
}


/*
  Reuse the top EvalFrame (which must be EOP_CALL_CFUNC).
 
  \param extraFrames  Number of extra frames to use.
  \param keepStack    Store the UThread stack.used level from before the call.
                      If NULL then stack.used is immediately reset to its
                      pre-call level.

  \return First uninitialized frame.
*/
EvalFrame* boron_reuseFrame(UThread* ut, int extraFrames, int* keepStack)
{
    UBuffer* buf = &BT->evalOp;
    EvalFrame* ef = ur_ptr(EvalFrame, buf) + buf->used - 1;
    assert(ef->block.eop == EOP_CALL_CFUNC);

    if (keepStack)
        *keepStack = ef->call.origStack;
    else
        ut->stack.used = ef->call.origStack;

    if (ef[-1].block.eop == EOP_OPTION_IT)
    {
        --buf->used;
        --ef;
    }

    if (extraFrames)
    {
        buf->used += extraFrames;
        if (buf->used > ur_avail(buf))
        {
            ur_error(ut, UR_ERR_INTERNAL, "EvalFrame overflow");
            return NULL;
        }
    }
    return ef;
}


void boron_initEvalCatch(EvalFrame* ef,
                         UStatus (*handler)(UThread*, EvalFrame*),
                         int origStack, UCell* result)
{
    ef->invoke.eop        = EOP_CATCH;
    ef->invoke.state      = 1;          // opCount
    ef->invoke.origStack  = origStack;
    ef->invoke.dat.catchf = handler;
    ef->invoke.result     = result;
}


/**
  Begin a section where boron_evalBlock() can be recursively called.

  Use boron_evalSetTop() where the recursive section ends.

  \return Evaluator operation stack position before EOP_RUN_RECURSE is pushed.
*/
UIndex boron_evalRecurse(UThread* ut, UCell* result)
{
    UIndex top = BT->evalOp.used;
    EvalFrame* ef = boron_pushEvalFrame(ut);
    if (ef) {
        ef->invoke.eop        = EOP_RUN_RECURSE;
        ef->invoke.state      = 1;          // opCount
        ef->invoke.origStack  = ut->stack.used;
        ef->invoke.dat.catchf = NULL;
        ef->invoke.result     = result;
    }
    return top;
}


/**
  Reset evaluator operation stack position.

  \param top    New evaluator operation stack position.
*/
void boron_evalSetTop(UThread* ut, UIndex top)
{
    BT->evalOp.used = top;
}


void boron_initEvalBlock(EvalFrame* ef, UThread* ut, UIndex blkN, UCell* result)
{
    const UBuffer* buf = ur_bufferEnv(ut, blkN);

    ef->block.eop       = EOP_DO_BLOCK;
    ef->block.funcFlags = 0;
    ef->block.origStack = ut->stack.used;
    ef->block.codeBlk   = blkN;
    ef->block.it        = buf->ptr.cell;
    ef->block.end       = buf->ptr.cell + buf->used;
    ef->block.result    = result;
}


EvalFrame* boron_pushEvalFrame(UThread* ut)
{
    UBuffer* buf = &BT->evalOp;
    if (buf->used < ur_avail(buf))
    {
        EvalFrame* ef = ur_ptr(EvalFrame, buf) + buf->used;
        ++buf->used;
        return ef;
    }
    return NULL;
}


int boron_resetEvalFrame(UThread* ut, const EvalFrame* end)
{
    UBuffer* buf = &BT->evalOp;
    buf->used = end - ur_ptr(EvalFrame, buf);
    return CFUNC_REFRAMED;
}


EvalFrame* boron_findEvalFrame(UThread* ut, int op)
{
    UBuffer* buf = &BT->evalOp;
    EvalFrame* start = ur_ptr(EvalFrame, buf);
    EvalFrame* ef = start + buf->used - 1;
    for (; ef != start; --ef)
    {
        if (ef->block.eop == op)
            return ef;
    }
    return NULL;
}


/*
  Reuse the top EvalFrame (which must be EOP_CALL_CFUNC) and reset the
  UThread stack.used to its level before the call.
*/
static EvalFrame* _reuseFrame(BoronThread* bt, int eop)
{
    UBuffer* buf = &bt->evalOp;
    EvalFrame* ef = ur_ptr(EvalFrame, buf) + buf->used - 1;
    assert(ef->block.eop == EOP_CALL_CFUNC);
    bt->thread.stack.used = ef->call.origStack;

    if (ef[-1].block.eop == EOP_OPTION_IT)
    {
        --buf->used;
        --ef;
    }
    ef->block.eop = eop;
    return ef;
}


static EvalFrame* _pushEvalFrame(BoronThread* bt, int eop)
{
    UBuffer* buf = &bt->evalOp;
    if (buf->used < ur_avail(buf))
    {
        EvalFrame* ef = ur_ptr(EvalFrame, buf) + buf->used;
        ++buf->used;
        ef->block.eop = eop;
        return ef;
    }
    return NULL;
}


static void _initEvalBlock( EvalFrame* ef, UThread* ut, UIndex blkN,
                            int stackPad, UCell* result )
{
    const UBuffer* buf = ur_bufferEnv(ut, blkN);
    ef->block.funcFlags = 0;
    ef->block.origStack = ut->stack.used - stackPad;
    ef->block.codeBlk = blkN;
    ef->block.it  = buf->ptr.cell;
    ef->block.end = buf->ptr.cell + buf->used;
    ef->block.result = result;
}


static void _initEvalReduce( EvalFrame* ef, UThread* ut, const UCell* blkC,
                             UIndex resBlkN )
{
    ef->reduce.origStack = ut->stack.used;
    ef->reduce.codeBlk = blkC->series.buf;
    ur_blockIt(ut, (UBlockIt*) &ef->reduce.it, blkC);
    ef->reduce.resBlk = resBlkN;
}


#define CALL_TRACE_NA   -1
#define CALL_TRACE_SKIP -2

// EOP_CALL_CFUNC
static void _initEvalCallC( EvalFrame* ef, UThread* ut, const UCell* cfunc,
                            int origStack, UCell* result )
{
    EvalFrame* pf;
    const UBuffer* blk;
    const ArgProgHeader* head = (const ArgProgHeader*)
                (ur_bufferSer(cfunc)->ptr.b +
                 ((const UCellFunc*) cfunc)->argProgOffset);
    ef->call.eop = EOP_CALL_CFUNC;
    ef->call.origStack = origStack;
    ef->call.argsPos = ut->stack.used;
    ef->call.pc = ((const uint8_t*) head) + head->progOffset;
    ef->call.funC = cfunc;
    ef->call.result = result;

    pf = ef - 1;
    if (pf->block.eop == EOP_OPTION_IT)
    {
        pf->block.result = (UCell*) head;
        --pf;
    }

    if (ur_flags(cfunc, FUNC_FLAG_NOTRACE))
        ef->call.tracePos = CALL_TRACE_SKIP;
    else
    {
        while ((1<<pf->block.eop & MASK_EOP_DO) == 0)
            --pf;
        blk = ur_bufferEnv(ut, pf->block.codeBlk);
        ef->call.tracePos = pf->block.it - blk->ptr.cell;
    }
}


// EOP_CALL_FUNC
static void _initEvalCallF( EvalFrame* ef, UThread* ut, const UCell* func,
                            int origStack, UCell* result )
{
    const UBuffer* blk = ur_bufferSer(func);
    const ArgProgHeader* head = (const ArgProgHeader*)
                                ur_bufferSer(blk->ptr.cell)->ptr.v;
    ef->call.eop = EOP_CALL_FUNC;
    ef->call.funcFlags = head->funcFlags;
    ef->call.origStack = origStack;
    ef->call.argsPos = ut->stack.used;
    ef->call.pc = ((const uint8_t*) head) + head->progOffset;
    ef->call.funC = func;
    ef->call.result = result;

    if (ef[-1].block.eop == EOP_OPTION_IT)
        ef[-1].block.result = (UCell*) head;
}


/**
  Reuse the current CFUNC call frame for a EOP_DO_BLOCK operation.

  This should be called as the final statement inside a BoronCFunc to
  immediately do the specified block.

  \param res    Address for result.  May be NULL to discard result.
  \param flags  May be zero or FUNC_FLAG_NOTRACE.

  \return CFUNC_REFRAMED.
*/
int boron_reframeDoBlock( UThread* ut, UIndex blkN, UCell* res, int flags )
{
    int pad = 0;
    EvalFrame* ef = _reuseFrame(BT, EOP_DO_BLOCK);
#if 0
    pad = ur_isShared(blkN) ? 0 : 1;
    if (pad)
        ur_pushCell(ut, blkC);      // Hold code block.
#endif
    if (! res) {
        res = ur_push(ut, UT_UNSET);
        pad = 1;
    }
    _initEvalBlock(ef, ut, blkN, pad, res);
    ef->block.funcFlags = flags;
    return CFUNC_REFRAMED;
}


/**
  Reuse the current CFUNC call frame for a EOP_DO_BLOCK1 operation.

  This should be called as the final statement inside a BoronCFunc to
  immediately do the specified block.

  \param valueFunc  This is invoked after the result of each statement is
                    evaluated, and once after the block has completed.
                    Check if EvalFrameInvoke::state is DO_BLOCK1_COMPLETE.

  \return CFUNC_REFRAMED.
*/
int boron_reframeDoBlock1(UThread* ut, UIndex blkN,
                          UStatus (*valueFunc)(UThread*, EvalFrameInvoke*),
                          UCell* res)
{
    int origStack;
    EvalFrame* ef = boron_reuseFrame(ut, 1, &origStack);
    ef->invoke.eop       = EOP_INVOKE;
    ef->invoke.state     = DO_BLOCK1_START;
    ef->invoke.origStack = origStack;
    ef->invoke.userBuf   = 0;
    ef->invoke.func      = valueFunc;
    ef->invoke.result    = res;

    ++ef;
    ef->block.eop = EOP_DO_BLOCK1;
    _initEvalBlock(ef, ut, blkN, 0, res);
    return CFUNC_REFRAMED;
}


int boron_breakDoBlock1(UThread* ut, EvalFrameInvoke* ef)
{
    UBuffer* evalOp = &BT->evalOp;
    ut->stack.used = ef->origStack;
    evalOp->used -= 2;
    return CFUNC_REFRAMED;
}


/**
  Reuse the current CFUNC call frame for reduction of a block.

  \param blkC       Cell of type UT_BLOCK or UT_PAREN to reduce.
  \param res        Result cell where the new block! is placed.
  \param complete   Function to call when reduce has completed.

  \return EvalFrameInvoke.
*/
EvalFrame* boron_reframeReduce(UThread* ut, const UCell* blkC, UCell* res,
                            UStatus (*complete)(UThread*, EvalFrameInvoke*))
{
    int origStack;
    EvalFrame* ef;
    UIndex blkN = ur_makeBlock(ut, 0);
    ur_initSeries(res, UT_BLOCK, blkN);

    ef = boron_reuseFrame(ut, 1, &origStack);
    ef->invoke.eop       = EOP_INVOKE;
    //ef->invoke.state
    ef->invoke.origStack = origStack;
    ef->invoke.userBuf   = blkN;
    ef->invoke.func      = complete;
    //ef->invoke.dat
    ef->invoke.result    = res;

    ++ef;
    ef->invoke.eop = EOP_REDUCE;
    _initEvalReduce(ef, ut, blkC, blkN);
    return ef - 1;
}


/*
  Push EOP_DO_BLOCK EvalFrame onto evalOp stack.
*/
void boron_startFibre( UThread* ut, UIndex blkN, UCell* res )
{
    EvalFrame* ef = _pushEvalFrame(BT, EOP_DO_BLOCK);
    assert(ef);
    _initEvalBlock(ef, ut, blkN, 0, res);
}


/*
void boron_endFibre( UThread* ut )
{
    (void) ut;
}


int boron_waitFibre( UThread* ut )
{
    (void) ut;
    return 0;
}
*/


#ifdef REPORT_EVAL
#define REPORT(msg,...)       printf(msg,__VA_ARGS__)
#else
#define REPORT(msg,...)
#endif

int ur_pathResolve( UThread*, UBlockIt* pi, UCell* tmp, UCell** lastCell );
const UCell* ur_pathSelect( UThread* ut, const UCell* selC, UCell* tmp,
                            UCell** expand );

#define INLINE_WORDVAL(it) \
    if( ur_binding(it) == UR_BIND_ENV ) \
        cell = (ut->sharedStoreBuf - it->word.ctx)->ptr.cell + it->word.index;\
    else if( ur_binding(it) == UR_BIND_THREAD ) \
        cell = (ut->dataStore.ptr.buf+it->word.ctx)->ptr.cell + it->word.index;\
    else if( ! (cell = ur_wordCell(ut, it)) ) \
        return NULL;

const UCell* boron_eval1F(UThread* ut, const UCell* it, const UCell* end,
                          UCell* res)
{
    EvalFrame* ef;
    const UCell* cell;
    int origStack;

eval_again:
    switch( ur_type(it) )
    {
        case UT_WORD:
            INLINE_WORDVAL(it)
            ++it;
            if( ur_is(cell, UT_CFUNC) )
            {
                REPORT("%s cfunc!\n", ur_wordCStr(it-1));
                goto begin_cfunc;
            }
            if( ur_is(cell, UT_FUNC) )
            {
                REPORT("%s func!\n", ur_wordCStr(it-1));
                goto begin_func;
            }
            if( ur_is(cell, UT_UNSET) )
                return cp_error(ut, UR_ERR_SCRIPT, "Unset word '%s",
                                ur_atomCStr(ut, it[-1].word.atom));
            *res = *cell;
            return it;

        case UT_LITWORD:
            *res = *it++;
            ur_type(res) = UT_WORD;
            return it;

        case UT_SETWORD:
        case UT_SETPATH:
            ef = _pushEvalFrame(BT, EOP_SET);
            if (! ef)
                goto overflow;
            ef->set.result = res;
            ef->set.it = it;
            ++it;
            while (it != end &&
                   (ur_is(it, UT_SETWORD) || ur_is(it, UT_SETPATH)))
                ++it;
            ef->set.end = it;
            if (it == end)
                return cp_error(ut, UR_ERR_SCRIPT, "End of block");
            goto eval_again;

        case UT_GETWORD:
            INLINE_WORDVAL(it)
            *res = *cell;
            return ++it;

        case UT_PAREN:
            ef = _pushEvalFrame(BT, EOP_DO_BLOCK);
            if (! ef)
                goto overflow;
            _initEvalBlock(ef, ut, it->series.buf, 0, res);
            return ++it;

        case UT_PATH:
        {
            UBlockIt path;
            const UCell* last;
            int headType;

            if( it->word.selType )
            {
                UBuffer* stack = &ut->stack;
                path.it = path.end = stack->ptr.cell + stack->used;
                last = ur_pathSelect(ut, it++, res, (UCell**) &path.end);
                if( ! last )
                    return NULL;
                if( ur_is(last, UT_CFUNC) || ur_is(last, UT_FUNC) ) {
                    // Keep expanded path on the stack for option! iteration.
                    origStack   = path.it  - stack->ptr.cell;
                    stack->used = path.end - stack->ptr.cell;
                    goto path_call;
                }
            }
            else
            {
                ur_blockIt( ut, &path, it++ );
                headType = ur_pathResolve( ut, &path, res, (UCell**) &last );
                if( headType == UR_THROW )
                    return NULL; //goto traceError;
                if( headType == UT_WORD )
                {
                    origStack = ut->stack.used;
path_call:
                    if( ur_is(last, UT_CFUNC) )
                    {
                        ef = _pushEvalFrame(BT, EOP_OPTION_IT);
                        ef->block.it  = path.it;
                        ef->block.end = path.end;
                        cell = last;
                        goto begin_cfunc_p;
                    }
                    else if( ur_is(last, UT_FUNC) )
                    {
                        cell = last;
                        if (cell->series.it == 0)   // No arguments (does).
                            goto begin_does;

                        ef = _pushEvalFrame(BT, EOP_OPTION_IT);
                        ef->block.it  = path.it;
                        ef->block.end = path.end;
                        goto begin_func_p;
                    }
                }
            }
            if( res != last )
                *res = *last;
        }
            return it;

        case UT_LITPATH:
            *res = *it++;
            ur_type(res) = UT_PATH;
            return it;

        case UT_CFUNC:
            cell = it;
            ++it;
begin_cfunc:
            origStack = ut->stack.used;
begin_cfunc_p:
            if (origStack > BT->stackLimit)
                goto stack_overflow;
            ef = boron_pushEvalFrame(ut);
            if (! ef)
                goto overflow;
            _initEvalCallC(ef, ut, cell, origStack, res);
            return it;

        case UT_FUNC:
            cell = it;
            ++it;
begin_func:
            origStack = ut->stack.used;
begin_func_p:
            if (cell->series.it == 0)   // No arguments (does).
            {
begin_does:
                ef = _pushEvalFrame(BT, EOP_FUNC_BODY);
                if (! ef)
                    goto overflow;
                _initEvalBlock(ef, ut, cell->series.buf, 0, res);
            }
            else
            {
                if (origStack > BT->stackLimit)
                    goto stack_overflow;
                ef = boron_pushEvalFrame(ut);
                if (! ef)
                    goto overflow;
                _initEvalCallF(ef, ut, cell, origStack, res);
            }
            return it;
    }

    *res = *it;
    return ++it;

overflow:
    ur_error(ut, UR_ERR_INTERNAL, "EvalFrame overflow");
    return NULL;

stack_overflow:
    ur_error(ut, UR_ERR_SCRIPT, "Stack overflow");
    return NULL;
}


static int64_t _funcCheckTypeMask(EvalFrame* ef, int type)
{
    int64_t mask = 0;
    const uint8_t* pc = ef->call.pc;
    int which = *pc++;
    if( which & CHECK_TYPE_PAD )
        ++pc;
    if( which & 1 )
    {
        mask |= *((uint16_t*) pc);
        pc += 2;
    }
    if( which & 2 )
    {
        mask |= ((int64_t) *((uint16_t*) pc)) << 16;
        pc += 2;
    }
    if( which & 4 )
    {
        mask |= ((int64_t) *((uint16_t*) pc)) << 32;
        pc += 2;
    }
    ef->call.pc = pc;
    return (1LL << type) & mask;
}


#define FETCH_OPT_ARG   (UR_OK + 1)

// Return argument program counter or...
static int _funcRecordOption(UThread* ut, EvalFrame* cf, EvalFrame* options)
{
next_option:
    while( options->block.it != options->block.end )
    {
        const UCell* oc = options->block.it++;
        const ArgProgHeader* head = (const ArgProgHeader*)
                                    options->block.result;
        const OptionEntry* ent = head->opt;
        int count = head->optionCount;
        int i;
        for( i = 0; i < count; ++i, ++ent )
        {
            if( ent->atom == oc->word.atom )
            {
                UCell* args = ut->stack.ptr.cell + cf->call.argsPos;
                args[-1].OPTION_FLAGS |= 1 << i;
                if( ent->programOffset )
                {
                    ((uint8_t*) args)[-1 - i] =
                        ut->stack.used - cf->call.argsPos;
                    cf->call.pc = ((const uint8_t*) head) + ent->programOffset;
                    return FETCH_OPT_ARG;
                }
                goto next_option;
            }
        }
        return ur_error(ut, UR_ERR_SCRIPT, "Invalid option %s",
                        ur_atomCStr(ut, oc->word.atom));
    }
    return UR_OK;
}


/*
  Evaluate a thread in isolation until it yields, throws an exception, or
  completes.

  This can be called recursively if an EOP_RUN_RECURSE operation is pushed
  prior to the call and popped afterwards.

  \return BoronFibreState
*/
int boron_runFibre( UThread* ut )
{
    EvalFrame* evalFrames;
    EvalFrame* ef;
    EvalFrame* df;      // Most recent EOP_DO_BLOCK frame.
    const UCell* cell;
    UCell* r2 = NULL;
    UBuffer* stack  = &ut->stack;
    UBuffer* evalOp = &BT->evalOp;
    UIndex callTrace;
    int op, efUsed;

    evalFrames = ur_ptr(EvalFrame, evalOp);

    while (evalOp->used)
    {
eval_top:
        ef = evalFrames + evalOp->used - 1;
        switch (ef->block.eop)
        {
        case EOP_DO_BLOCK:
            if (ef->block.it == ef->block.end)
            {
                stack->used = ef->block.origStack;
                --evalOp->used;
                break;
            }
eval_next:
            cell = boron_eval1F(ut, ef->block.it, ef->block.end,
                                ef->block.result);
            if (! cell)
                goto except;
            ef->block.it = cell;
            break;

        case EOP_DO_BLOCK1:
            df = ef - 1;
            if (df->invoke.state == DO_BLOCK1_VALUE)
            {
                op = df->invoke.func(ut, &df->invoke);
                if (op == CFUNC_REFRAMED)
                    goto eval_top;
                if (op == UR_THROW)
                    goto except;
            }
            else
                df->invoke.state = DO_BLOCK1_VALUE;

            if (ef->block.it != ef->block.end)
                goto eval_next;

            stack->used = df->invoke.origStack;
            evalOp->used -= 2;
            df->invoke.state = DO_BLOCK1_COMPLETE;
            if (df->invoke.func(ut, &df->invoke) == UR_THROW)
                goto except;
            break;

        case EOP_FUNC_BODY:
            // Same as EOP_DO_BLOCK except for funcFlags stack map pop.
            if (ef->block.it != ef->block.end)
                goto eval_next;
func_end:
            stack->used = ef->block.origStack;
            evalOp->used -= (ef[-1].block.eop == EOP_OPTION_IT) ? 2 : 1;
            if (ef->block.funcFlags & FUNC_FLAG_NEEDSTACK)
                BT->frames.used -= 2;
            break;

        case EOP_SET:
            for (cell = ef->set.it; cell != ef->set.end; ++cell)
            {
                if (ur_is(cell, UT_SETWORD))
                {
                    if (! ur_setWord(ut, cell, ef->set.result))
                        goto except;
                }
                else
                {
                    if (! ur_setPath(ut, cell, ef->set.result))
                        goto except;
                }
            }
            --evalOp->used;
            break;

        case EOP_REDUCE:
            if (ef->reduce.it == ef->reduce.end)
            {
                stack->used = ef->reduce.origStack;
                --evalOp->used;
                break;
            }
            {
            UBuffer* blk = ur_buffer(ef->reduce.resBlk);
            cell = boron_eval1F(ut, ef->reduce.it, ef->reduce.end,
                                ur_blkAppendNew(blk, UT_UNSET));
            }
            if (! cell)
                goto except;
            ef->reduce.it = cell;
            break;

        case EOP_BLOCK_ITER:
            --evalOp->used;     // NOP
            break;

        case EOP_CATCH:
catch_pop:
            stack->used = ef->invoke.origStack;
            evalOp->used -= ef->invoke.state;
            break;

        case EOP_RUN_RECURSE:
            // Keep this recurse operation on the evalOp stack so
            // boron_runFibre() can be used multiple times.
            // The caller is expected to use boron_popEvalRecurse().
            return BOR_FIBRE_DONE;

        case EOP_INVOKE_LOOP:
            op = ef->invoke.func(ut, &ef->invoke);
            if (op == UR_THROW)
                goto except;
            if (op == UR_OK)
                goto catch_pop;
            break;

        case EOP_INVOKE:
            if (ef->invoke.func(ut, &ef->invoke) == UR_THROW)
                goto except;
            stack->used = ef->invoke.origStack;
            --evalOp->used;
            break;

        //case EOP_OPTION_IT:

        case EOP_CALL_CFUNC:
        case EOP_CALL_FUNC:
arg_prog:
            df = ef - 1;
            while ((1 << df->block.eop & MASK_EOP_DO) == 0)
                --df;
            while( (op = *ef->call.pc++) < FO_end )
            {
                switch( op )
                {
                    case FO_clearLocal:     // Only for func!.
                        op = *ef->call.pc++;
                        {
                        UCell* ls = stack->ptr.cell + stack->used;
                        UCell* lend = ls + op;
                        for( ; ls != lend; ++ls )
                            ur_setId(ls, UT_NONE);
                        }
                        stack->used += op;
                        goto fetch_done;

                    case FO_fetchArg:
                        if( df->block.it == df->block.end )
                            goto func_short;
                        r2 = stack->ptr.cell + stack->used;
                        ++stack->used;
                        ur_setId(r2, UT_NONE);
                        efUsed = evalOp->used;
                        cell = boron_eval1F(ut, df->block.it, df->block.end,r2);
                        if (! cell)
                        {
                            ef = df;
                            goto except;
                        }
                        df->block.it = cell;
                        if (evalOp->used > efUsed)
                            goto eval_top;
                        break;

                    case FO_litArg:
                        if (df->block.it == df->block.end)
                            goto func_short;
                        stack->ptr.cell[ stack->used ] = *df->block.it++;
                        ++stack->used;
                        break;

                    case FO_checkType:
                        op = *ef->call.pc++;
                        r2 = stack->ptr.cell + stack->used - 1;
                        if (ur_type(r2) != op)
                            goto bad_arg;
                        break;

                    case FO_checkTypeMask:
                        r2 = stack->ptr.cell + stack->used - 1;
                        if (! _funcCheckTypeMask(ef, ur_type(r2)))
                            goto bad_arg;
                        break;

                    case FO_optionRecord:
                        r2 = stack->ptr.cell + stack->used;
                        ++stack->used;
                        ef->call.argsPos = stack->used;
                        ur_setId(r2, UT_UNSET);
                        break;

                    case FO_variant:    // Only for cfunc!.
                        r2 = stack->ptr.cell + stack->used;
                        ++stack->used;
                        ur_setId(r2, UT_INT);
                        ur_int(r2) = *ef->call.pc++;
                        break;
                }
            }
fetch_done:
            df = ef - 1;
            if (df->block.eop == EOP_OPTION_IT)
            {
                switch (_funcRecordOption(ut, ef, df)) {
                    case UR_THROW:
                        goto except;
                    case FETCH_OPT_ARG:
                        goto arg_prog;
                }
            }

            if (ef->call.eop == EOP_CALL_CFUNC)
            {
                REPORT("  cfunc! ef:%ld r:%d s:%d,%d,%d\n",
                       ef - evalFrames, ef->call.result - stack->ptr.cell,
                       ef->call.origStack, ef->call.argsPos, stack->used);

                op = ((const UCellFunc*) ef->call.funC)->m.func(ut,
                        stack->ptr.cell + ef->call.argsPos,
                        ef->call.result);
                if (op == CFUNC_REFRAMED)
                    break;
                if (op == UR_THROW)
                    goto except;
                stack->used = ef->call.origStack;
                evalOp->used -= (ef[-1].block.eop == EOP_OPTION_IT) ? 2 : 1;
            }
            else
            {
                const UCell* funC = ef->call.funC;
                if (ef->call.funcFlags & FUNC_FLAG_NEEDSTACK)
                {
                    UIndex* fi;
                    UBuffer* smap = &BT->frames;
                    UIndex newUsed = smap->used + 2;
                    if (newUsed > ur_avail(smap))
                        ur_arrReserve(smap, newUsed);
                    fi = smap->ptr.i32 + smap->used;
                    smap->used = newUsed;
                    fi[0] = funC->series.buf;
                    fi[1] = ef->call.argsPos;
                }

                REPORT("  func! ef:%ld r:%d s:%d,%d,%d\n",
                       ef - evalFrames, ef->call.result - stack->ptr.cell,
                       ef->call.origStack, ef->call.argsPos, stack->used);

                // Replace EOP_CALL_FUNC entry with EOP_FUNC_BODY.
                // The funcFlags, origStack, & result members sit in the
                // same place.
                {
                const UBuffer* body = ur_bufferEnv(ut, funC->series.buf);
                ef->block.eop     = EOP_FUNC_BODY;
                //ef->block.funcFlags
                //ef->block.origStack
                ef->block.codeBlk = funC->series.buf;
                ef->block.it      = body->ptr.cell + funC->series.it;
                ef->block.end     = body->ptr.cell + body->used;
                //ef->block.result  = ef->call.result;
                }
            }
            break;

        default:
            ur_error(ut, UR_ERR_INTERNAL, "Invalid eval opcode");
            return BOR_FIBRE_EXCEPTION;
        }
    }
    return BOR_FIBRE_DONE;

except:
    r2 = ur_exception(ut);
    efUsed = ur_is(r2, UT_WORD) ? ur_atom(r2) : 0;
    if (! ur_is(r2, UT_ERROR))
        r2 = NULL;
    callTrace = CALL_TRACE_NA;

    // Catch exception and trace error! location.
    for (df = ef; df >= evalFrames; --df)
    {
        switch (df->invoke.eop)
        {
        case EOP_CATCH:
        case EOP_INVOKE_LOOP:
            op = df->invoke.dat.catchf(ut, df);
            if (op != UR_THROW)
            {
                if (op == UR_OK)
                {
                    stack->used = df->invoke.origStack;
                    evalOp->used = (df - evalFrames) - (df->invoke.state - 1);
                }
                goto eval_top;
            }
            break;

        case EOP_FUNC_BODY:
            if (efUsed == UR_ATOM_RETURN)
            {
                evalOp->used = (df - evalFrames) + 1;
                ef = df;
                goto func_end;
            }
            // Fall through...
            /* FALLTHRU */

        case EOP_DO_BLOCK:
        case EOP_DO_BLOCK1:
        case EOP_REDUCE:
            if (r2)
            {
                const UCell* doPos;

                // Check FUNC_FLAG_NOTRACE.
                if ((1 << df->invoke.eop & MASK_EOP_FFLAGS) &&
                    (df->block.funcFlags & FUNC_FLAG_NOTRACE))
                {
                    callTrace = CALL_TRACE_SKIP;
                    continue;
                }

                if (callTrace >= 0)
                {
                    doPos = ur_bufferEnv(ut, df->block.codeBlk)->ptr.cell +
                            callTrace;
                    callTrace = CALL_TRACE_NA;
                }
                else if (callTrace == CALL_TRACE_SKIP)
                {
                    callTrace = CALL_TRACE_NA;
                    continue;
                }
                else
                    doPos = df->block.it;

                ur_traceError(ut, r2, df->block.codeBlk, doPos);
            }
            break;

        case EOP_RUN_RECURSE:
            // evalOp->used is reset by the code that pushed EOP_RUN_RECURSE.
            return BOR_FIBRE_EXCEPTION;

        case EOP_CALL_CFUNC:
            if (callTrace == CALL_TRACE_NA)
                callTrace = df->call.tracePos;
            break;
        }
    }
    return BOR_FIBRE_EXCEPTION;

bad_arg:
    boron_badArg(ut, ur_type(r2), stack->used - 1 - ef->call.argsPos);
    goto except;

func_short:
    ur_error(ut, UR_ERR_SCRIPT, "End of block");
    goto except;
}


/*
  -cf-
    do
        value
    return: Result of value.
    group: eval
*/
CFUNC_PUB( cfunc_do )
{
    EvalFrame* ef = NULL;
    int origStack = 0;

    switch( ur_type(a1) )
    {
        case UT_WORD:
        {
            const UCell* cell;
            if (! (cell = ur_wordCell(ut, a1)))
                return UR_THROW;

            if (ur_is(cell, UT_CFUNC) || ur_is(cell, UT_FUNC))
            {
                boron_reuseFrame(ut, -1, NULL);     // Pop EOP_CALL_CFUNC.
                if (! boron_eval1F(ut, cell, cell+1, res))
                    return UR_THROW;
                return CFUNC_REFRAMED;
            }
            *res = *cell;
        }
            break;

        case UT_LITWORD:
            *res = *a1;
            ur_type(res) = UT_WORD;
            break;

        case UT_GETWORD:
            return boron_copyWordValue(ut, a1, res);

        case UT_STRING:
        {
            USeriesIter si;

            ur_seriesSlice(ut, &si, a1);
            if (si.it == si.end)
                ur_setId(res, UT_UNSET);
            else if (ur_strIsUcs2(si.buf))
                return ur_error(ut, UR_ERR_INTERNAL,
                                "FIXME: Cannot do ucs2 string!");
            else
            {
                UCell* newBlk = ur_push(ut, UT_UNSET);
                UIndex blkN;

                blkN = ur_tokenize(ut, si.buf->ptr.c + si.it,
                                       si.buf->ptr.c + si.end, newBlk); // gc!
                if (! blkN)
                {
                    ur_pop(ut);
                    return UR_THROW;
                }

                boron_bindDefault(ut, blkN);

                ef = _reuseFrame(BT, EOP_DO_BLOCK);
                ur_pushCell(ut, newBlk);
                _initEvalBlock(ef, ut, blkN, 1, res);
                return CFUNC_REFRAMED;
            }
        }
            break;

#if !defined(CONFIG_ISOLATE)
        case UT_FILE:
            if (cfunc_load(ut, a1, res) == UR_THROW)
                return UR_THROW;
            if (res->id.type != UT_BLOCK)
                break;
            a1 = res;
            // Fall through to block...
            /* FALLTHRU */
#endif

        case UT_BLOCK:
        case UT_PAREN:
            ef = _reuseFrame(BT, EOP_DO_BLOCK);
            _initEvalBlock(ef, ut, a1->series.buf, 0, res);
            return CFUNC_REFRAMED;

        case UT_PATH:
        {
            UBlockIt path;
            UCell* last;
            int ok;

            if( a1->word.selType )
            {
                UBuffer* stack = &ut->stack;
                path.it = path.end = stack->ptr.cell + stack->used;
                last = (UCell*) ur_pathSelect(ut, a1, res, (UCell**) &path.end);
                if( ! last )
                    return UR_THROW;
                if( ur_is(last, UT_CFUNC) || ur_is(last, UT_FUNC) ) {
                    // boron_reuseFrame below will keep any expanded path on
                    // the stack for option! iteration.
                    stack->used = path.end - stack->ptr.cell;
                    goto path_call;
                }
            }
            else
            {
                ur_blockIt(ut, &path, a1);
                ok = ur_pathResolve(ut, &path, res, &last);
                if( ok == UR_THROW )
                    return UR_THROW;
                if( ok == UT_WORD )
                {
path_call:
                    if( ur_is(last, UT_CFUNC) )
                    {
                        ef = boron_reuseFrame(ut, 1, &origStack);
                        if (! ef )
                            return UR_THROW;
                        ef->block.eop = EOP_OPTION_IT;
                        ef->block.it  = path.it;
                        ef->block.end = path.end;
                        ++ef;
                        a1 = last;
                        goto do_cfunc;
                    }
                    else if( ur_is(last, UT_FUNC) )
                    {
                        a1 = last;
                        if (last->series.it == 0)   // No arguments (does).
                            goto do_does;

                        ef = boron_reuseFrame(ut, 1, &origStack);
                        if (! ef )
                            return UR_THROW;
                        ef->block.eop = EOP_OPTION_IT;
                        ef->block.it  = path.it;
                        ef->block.end = path.end;
                        ++ef;
                        goto do_func;
                    }
                }
            }
            if (res != last)
                *res = *last;
            return UR_OK;
        }

        case UT_LITPATH:
            *res = *a1;
            ur_type(res) = UT_PATH;
            break;

        case UT_CFUNC:
            // Keep a1 on stack to hold cfunc! cell.
            ef = boron_reuseFrame(ut, 0, &origStack);
            if (! ef )
                return UR_THROW;
do_cfunc:
            _initEvalCallC(ef, ut, a1, origStack, res);
            return CFUNC_REFRAMED;

        case UT_FUNC:
            // Keep a1 on stack to hold func! cell.
do_does:
            ef = boron_reuseFrame(ut, 0, &origStack);
            if (! ef )
                return UR_THROW;
            if (a1->series.it == 0)   // No arguments (does).
            {
                ef->block.eop = EOP_FUNC_BODY;
                _initEvalBlock(ef, ut, a1->series.buf,
                               ut->stack.used - origStack, res);
            }
            else
            {
do_func:
                //if (ut->stack.used > BT->stackLimit)
                //    goto stack_overflow;
                _initEvalCallF(ef, ut, a1, origStack, res);
            }
            return CFUNC_REFRAMED;

        default:
            *res = *a1;
            break;
    }
    return UR_OK;
}


/*-cf-
    reduce
        value
    return: Reduced value.
    group: data

    If value is a block then a new block is created with values set to the
    evaluated results of the original.
*/
CFUNC(cfunc_reduce)
{
    if( ur_is(a1, UT_BLOCK) )
    {
        UIndex blkN = ur_makeBlock(ut, 0);
        ur_initSeries(res, UT_BLOCK, blkN);

        EvalFrame* ef = _reuseFrame(BT, EOP_REDUCE);
        _initEvalReduce(ef, ut, a1, blkN);
        return CFUNC_REFRAMED;
    }

    *res = *a1;
    return UR_OK;
}


extern int context_make( UThread* ut, const UCell* from, UCell* res );
extern UDatatype dt_context;

static int context_make_override( UThread* ut, const UCell* from, UCell* res )
{
    if( ! context_make( ut, from, res ) )
        return UR_THROW;
    if( ur_is(from, UT_BLOCK) )
    {
        EvalFrame* ef = _reuseFrame(BT, EOP_DO_BLOCK);
        _initEvalBlock(ef, ut, from->series.buf, 1, ur_push(ut, UT_UNSET));
        return CFUNC_REFRAMED;
    }
    return UR_OK;
}


static void _bindDefaultB( UThread* ut, UIndex blkN )
{
    UBlockIterM bi;
    int type;
    int wrdN;
    UBuffer* threadCtx = ur_threadContext(ut);
    UBuffer* envCtx = ur_envContext(ut);

    bi.buf = ur_buffer( blkN );
    bi.it  = bi.buf->ptr.cell;
    bi.end = bi.it + bi.buf->used;

    ur_foreach( bi )
    {
        type = ur_type(bi.it);
        if( ur_isWordType(type) )
        {
as_word:
            if( threadCtx->used )
            {
                wrdN = ur_ctxLookup( threadCtx, ur_atom(bi.it) );
                if( wrdN > -1 )
                    goto assign;
            }

            if( type == UT_SETWORD )
            {
                wrdN = ur_ctxAppendWord( threadCtx, ur_atom(bi.it) );
                if( envCtx )
                {
                    // Lift default value of word from environment.
                    int ewN = ur_ctxLookup( envCtx, ur_atom(bi.it) );
                    if( ewN > -1 )
                        *ur_ctxCell(threadCtx, wrdN) = *ur_ctxCell(envCtx, ewN);
                }
            }
            else
            {
                if( envCtx )
                {
                    wrdN = ur_ctxLookup( envCtx, ur_atom(bi.it) );
                    if( wrdN > -1 )
                    {
                        // TODO: Have ur_freezeEnv() remove unset words.
                        if( ! ur_is( ur_ctxCell(envCtx, wrdN), UT_UNSET ) )
                        {
                            ur_setBinding( bi.it, UR_BIND_ENV );
                            bi.it->word.ctx = -UR_MAIN_CONTEXT;
                            bi.it->word.index = wrdN;
                            continue;
                        }
                    }
                }
                wrdN = ur_ctxAppendWord( threadCtx, ur_atom(bi.it) );
            }
assign:
            ur_setBinding( bi.it, UR_BIND_THREAD );
            bi.it->word.ctx = UR_MAIN_CONTEXT;
            bi.it->word.index = wrdN;
        }
        else if( ur_isBlockType(type) )
        {
as_block:
            if( ! ur_isShared( bi.it->series.buf ) )
                _bindDefaultB( ut, bi.it->series.buf );
        }
        else if( ur_isPathType(type) )
        {
            if( bi.it->word.selType )
                goto as_word;
            goto as_block;
        }
        /*
        else if( type >= UT_BI_COUNT )
        {
            ut->types[ type ]->bind( ut, it, bt );
        }
        */
    }
}


extern UBuffer* ur_ctxSortU( UBuffer*, int unsorted );

/**
  Bind block in thread dataStore to default contexts.
*/
void boron_bindDefault( UThread* ut, UIndex blkN )
{
    ur_ctxSortU( ur_threadContext( ut ), 16 );
    _bindDefaultB( ut, blkN );
}


UStatus boron_evalBlock(UThread* ut, UIndex blkN, UCell* res)
{
    boron_startFibre(ut, blkN, res);
run:
    switch (boron_runFibre(ut)) {
        case BOR_FIBRE_DONE:
            break;
        case BOR_FIBRE_YIELD:
            /*
            if (! boron_nextFibre(ut))
                boron_waitFibre(ut);
            */
            goto run;
        case BOR_FIBRE_EXCEPTION:
            return UR_THROW;
    }
    return UR_OK;
}


/**
  Run script and put result in the last stack cell.

  To preserve the last value the caller must push a new value on top
  (e.g. boron_stackPush(ut, UT_UNSET)).

  If an exception occurs, the thrown value is at the bottom of the stack
  (use the ur_exception macro).

  \return Result on stack or NULL if an exception is thrown.
*/
UCell* boron_evalUtf8( UThread* ut, const char* script, int len )
{
    UCell* res;
    UIndex bufN;    // Code block.

    if( len < 0 )
        len = strlen(script);

    boron_reset(ut);
    res = ur_stackTop(ut);
    bufN = ur_tokenize(ut, script, script + len, res);  // gc!
    if (! bufN)
        return NULL;

    boron_bindDefault(ut, bufN);

    res = ur_push(ut, UT_UNSET);
    if (! boron_evalBlock(ut, bufN, res))
        res = NULL;
    return res;
}

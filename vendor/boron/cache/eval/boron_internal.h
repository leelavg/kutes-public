#ifndef BORON_INTERNAL_H
#define BORON_INTERNAL_H


#include "env.h"

#ifdef CONFIG_RANDOM
#include "well512.h"
#endif
#ifdef CONFIG_ASSEMBLE
#include <jit/jit.h>
#endif


#define MAX_OPT     8       // LIMIT: 8 options per func/cfunc.
#define OPT_BITS(c) (c)->id._pad0

#define PORT_SITE(dev,pbuf,portC) \
    UBuffer* pbuf = ur_buffer( portC->port.buf ); \
    UPortDevice* dev = (pbuf->form == UR_PORT_SIMPLE) ? \
        (UPortDevice*) pbuf->ptr.v : \
        (pbuf->ptr.v ? *((UPortDevice**) pbuf->ptr.v) : 0)


typedef struct
{
    UEnv    env;
    UBuffer ports;
    UStatus (*funcRead)( UThread*, UCell*, UCell* );
    uint16_t argStackSize;
    uint16_t evalStackSize;
    UAtom   compileAtoms[5];
}
BoronEnv;

#define BENV      ((BoronEnv*) ut->env)


enum EvaluatorOpcodes
{
    EOP_NOP,
    EOP_DO_BLOCK,
    EOP_DO_BLOCK1,
    EOP_FUNC_BODY,
    EOP_SET,
    EOP_REDUCE,
    EOP_BLOCK_ITER,
    EOP_CATCH,
    EOP_RUN_RECURSE,
    EOP_INVOKE_LOOP,
    EOP_INVOKE,
    EOP_OPTION_IT,
    EOP_CALL_CFUNC,
    EOP_CALL_FUNC,
    EOP_WAIT
};

#define MASK_EOP_DO     (1<<EOP_DO_BLOCK | 1<<EOP_DO_BLOCK1 | 1<<EOP_FUNC_BODY | 1<<EOP_REDUCE)
#define MASK_EOP_CATCH  (1<<EOP_CATCH | 1<<EOP_INVOKE_LOOP)
#define MASK_EOP_FFLAGS (1<<EOP_DO_BLOCK | 1<<EOP_FUNC_BODY)


struct EvalFrameBlock {
    uint8_t  eop;
    uint8_t  funcFlags;
    uint16_t origStack;
    UIndex   codeBlk;
    const UCell* it;
    const UCell* end;
    UCell* result;
};

struct EvalFrameReduce {
    uint8_t  eop;
    uint8_t  _pad;
    uint16_t origStack;
    UIndex   codeBlk;
    const UCell* it;
    const UCell* end;
    UIndex   resBlk;
};

typedef union EvalFrame EvalFrame;

typedef struct EvalFrameInvoke  EvalFrameInvoke;

struct EvalFrameInvoke {
    uint8_t  eop;
    uint8_t  state;     // opCount for EOP_CATCH & EOP_INVOKE_LOOP
    uint16_t origStack;
    UIndex   userBuf;
    UStatus (*func)(UThread*, EvalFrameInvoke*);
    union {
        UStatus (*catchf)(UThread*, EvalFrame*);
        void* ptr;
        UIndex i;
    } dat;
    UCell*   result;
};

enum DoBlock1State {
    DO_BLOCK1_START,
    DO_BLOCK1_VALUE,
    DO_BLOCK1_COMPLETE
};

struct EvalFrameCall {
    uint8_t  eop;
    uint8_t  funcFlags;     // For func! only
    uint16_t origStack;
    uint16_t argsPos;
    int16_t  tracePos;      // Code block UCell index, cfunc! only.
    const uint8_t* pc;
    const UCell* funC;
    UCell* result;
};

struct EvalFrameSet {
    uint8_t  eop;
    const UCell* it;
    const UCell* end;
    UCell* result;
};

struct EvalFrameData {
    uint8_t  eop;           // EOP_NOP
    uint8_t  state;
    uint16_t origStack;
    UIndex   index;
    union {
        int32_t i32[6];
        uint64_t u64[3];
        void* ptr[3];
    } var;
};

union EvalFrame {
    struct EvalFrameBlock   block;
    struct EvalFrameReduce  reduce;
    struct EvalFrameInvoke  invoke;
    struct EvalFrameCall    call;
    struct EvalFrameSet     set;
    struct EvalFrameData    data;
};


typedef struct BoronFibre   BoronThread;

struct BoronFibre
{
    UThread thread;
    UBuffer tbin;           // Temporary binary buffer.
    int (*requestAccess)( UThread*, const char* );
    BoronThread* nextFibre;
    UIndex  stackLimit;     // End of available UThread::stack.
    UBuffer frames;         // Function body & locals stack position.
    UBuffer evalOp;         // EvalFrame stack.
    UCell   optionCell;
#ifdef CONFIG_RANDOM
    Well512 rand;
#endif
#ifdef CONFIG_ASSEMBLE
    jit_context_t jit;
    UAtomEntry* insTable;
#endif
};

#define BT      ((BoronThread*) ut)
#define RESULT  (BT->evalData + BT_RESULT)

extern EvalFrame* boron_reuseFrame(UThread*, int extraFrames, int* keepStack);
extern EvalFrame* boron_pushEvalFrame(UThread*);
extern EvalFrame* boron_findEvalFrame(UThread*, int op);
extern int boron_resetEvalFrame(UThread*, const EvalFrame*);
extern void boron_initEvalCatch(EvalFrame* ef,
                            UStatus (*handler)(UThread*, EvalFrame*),
                            int origStack, UCell* result);
extern void boron_initEvalBlock(EvalFrame*, UThread*, UIndex blkN, UCell*);
extern int boron_reframeDoBlock1(UThread*, UIndex blkN,
                            UStatus (*valueFunc)(UThread*, EvalFrameInvoke*),
                            UCell* res);
extern int boron_breakDoBlock1(UThread*, EvalFrameInvoke*);
extern EvalFrame* boron_reframeReduce(UThread*, const UCell* a1, UCell* res,
                            UStatus (*complete)(UThread*, EvalFrameInvoke*));
extern UIndex boron_seriesEnd( UThread* ut, const UCell* cell );


#endif  // BORON_INTERNAL_H

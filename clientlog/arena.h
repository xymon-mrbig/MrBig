#pragma once

#include <setjmp.h>
#include <stdalign.h>
#include <wtypesbase.h>

#define ARENA_MAXNUM_HANDLES 8

typedef enum {
    RETURN_INT,
    RETURN_LONG,
    RETURN_VOID,
} clog_CloseHandleReturnType;

typedef union {
    INT Int;
    LONG Long;
    BYTE Void;
} clog_CloseHandleReturnValue;

typedef struct {
    void *Handle;
    clog_CloseHandleReturnType ReturnType;
    union {
        int (*Int)(void *);
        long (*Long)(void *);
        void (*Void)(void *);
    } CloseHandleFn;
} clog_HandleWrapper;

typedef struct clog__Arena {
    /*struct clog_ArenaState*/ void *State;
    BYTE *End;
} clog_Arena;

typedef struct {
    clog_HandleWrapper HandleStack[ARENA_MAXNUM_HANDLES];
    jmp_buf MemoryErrorHandler;
    clog_Arena Memory;
    DWORD NumHandles;
    BYTE *Start, *End;
    BYTE *CurrentStart;
} clog_ArenaState;

#define clog_DeferError(arena, err) \
    int err;                        \
    if ((err = setjmp(((clog_ArenaState *)(arena)->State)->MemoryErrorHandler)))

#define clog_ThrowError(arena, errorcode) longjmp((void *)((clog_ArenaState *)(arena)->State)->MemoryErrorHandler, errorcode)

void clog_ArenaAppend(clog_Arena *a, const char *format, ...);

void *clog__ArenaAllocate(clog_Arena *a, size_t size, size_t align, size_t count);
#define clog_ArenaAlloc(arena, type, numelements) (type *)clog__ArenaAllocate(arena, sizeof(type), alignof(type), numelements)

clog_ArenaState *clog_ArenaMake(size_t capacity);
void clog_ArenaFreeAll(clog_ArenaState *a);

// We can skip several layers of stack frames through clog_ThrowError,
// so we need to keep track of open handles apart from normal control flow
void clog_Defer(clog_Arena *a, void *handle, clog_CloseHandleReturnType type, void *freeFn);
void clog_IgnorePopDefer(clog_Arena *a);
clog_CloseHandleReturnValue clog_PopDefer(clog_Arena *a);
void clog_PopDeferAll(clog_Arena *a);
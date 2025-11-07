#include "arena.h"
#include <stdio.h>

// Inspiration taken from https://nullprogram.com/blog/2023/09/27/
// With some adjustments to fit MrBig.
// The arena can allocate from both ends, the start (left) points to a append-only
// string, i.e. cumulative output. Transient allocations are made from the end (right).
// Conceptually: clog_Arena.Start -> ['o','u','t','p','u','t',...
//               clog_Arena.End   ->  ...,32,'c','h', 4,'r',0]

// Prefer the macro 'clog_ArenaAlloc' from arena.h instead of 'clog__ArenaAllocate'
void *clog__ArenaAllocate(clog_Arena *a, size_t size, size_t align, size_t count) {
    BYTE *aStart = ((clog_ArenaState *)a->State)->CurrentStart;
    ptrdiff_t freeBytes = a->End - aStart;
    ptrdiff_t amountToAllocate = count * size;
    if (amountToAllocate > freeBytes) { // avoid overflow errors
        clog_ThrowError(a, 1);
    }

    BYTE *naiveEnd = a->End - amountToAllocate;
    BYTE *actualEnd = (BYTE *)((size_t)naiveEnd & -align); // n.b. given align = 2^n, then x & -align zeros out the n-1 least significant bits of x
    ptrdiff_t remaining = actualEnd - aStart;
    if (remaining < 0) {
        clog_ThrowError(a, 1);
    }

    a->End = actualEnd;
    return memset(actualEnd, 0, count * size);
}

void clog_ArenaAppend(clog_Arena *a, const char *format, ...) {
    BYTE *aStart = ((clog_ArenaState *)a->State)->CurrentStart;
    ptrdiff_t freeBytes = a->End - aStart;
    va_list vargs;
    va_start(vargs, format);
    int written = vsnprintf((char *)aStart, freeBytes, format, vargs);
    va_end(vargs);
    if (written > freeBytes) {
        clog_ThrowError(a, 1);
    }

    ((clog_ArenaState *)a->State)->CurrentStart += written;
}

clog_ArenaState *clog_ArenaMake(size_t capacity) {
    clog_ArenaState *pState = malloc(sizeof(clog_ArenaState));
    *pState = (clog_ArenaState){0};
    void *mem = malloc(capacity);
    pState->Start = mem;
    pState->CurrentStart = mem;
    pState->Memory = (clog_Arena){0};
    pState->Memory.State = pState;

    pState->End = mem ? mem + capacity : 0;
    pState->Memory.End = pState->End;
    return pState;
}

void clog_ArenaFreeAll(clog_ArenaState *a) {
    free(a->Start);
    free(a);
}

void clog_Defer(clog_Arena *a, void *handle, clog_CloseHandleReturnType type, void *freeFn) {
    clog_ArenaState *state = ((clog_ArenaState *)a->State);

    if (state->NumHandles >= ARENA_MAXNUM_HANDLES) {
        clog_ThrowError(a, 2);
    }

    clog_HandleWrapper *wrapper = &state->HandleStack[state->NumHandles++];
    wrapper->Handle = handle;

    switch (wrapper->ReturnType) {
    case RETURN_INT:
        wrapper->CloseHandleFn.Int = freeFn;
        break;
    case RETURN_LONG:
        wrapper->CloseHandleFn.Long = freeFn;
        break;
    case RETURN_VOID:
    default:
        wrapper->CloseHandleFn.Void = freeFn;
        break;
    }

    wrapper->ReturnType = type;
}

void clog_IgnorePopDefer(clog_Arena *a) {
    clog_ArenaState *state = (clog_ArenaState *)a->State;
    if (state->NumHandles > 0) {
        state->NumHandles--;
    }
}

clog_CloseHandleReturnValue clog_PopDefer(clog_Arena *a) {
    clog_ArenaState *state = (clog_ArenaState *)a->State;
    clog_HandleWrapper wrapper = state->HandleStack[--state->NumHandles];

    clog_CloseHandleReturnValue res;
    switch (wrapper.ReturnType) {
    case RETURN_INT:
        res.Int = (*wrapper.CloseHandleFn.Int)(wrapper.Handle);
        return res;
    case RETURN_LONG:
        res.Long = (*wrapper.CloseHandleFn.Long)(wrapper.Handle);
        return res;
    case RETURN_VOID:
        wrapper.CloseHandleFn.Void(wrapper.Handle);
        res.Void = 0;
        return res;
    default:
        res.Void = 0;
        return res;
    }
}

void clog_PopDeferAll(clog_Arena *a) {
    clog_ArenaState *state = (clog_ArenaState *)a->State;
    while (state->NumHandles > 0) {
        clog_PopDefer(a);
    }
}
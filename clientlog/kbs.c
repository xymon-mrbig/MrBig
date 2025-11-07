#include "clientlog.h"
#include <initguid.h>
#include <wuapi.h>
#include <wuerror.h>

typedef struct TreeSet {
    struct TreeSet *Left;
    struct TreeSet *Right;
    DWORD Value;
} TreeSet;

struct ComString {
    UINT32 len;
    wchar_t content[1];
};

DWORD kbs_ExtractKBNumber(wchar_t *title) {
    DWORD res = 0;
    BYTE *titleBytes = (BYTE *)title;
    int i = 0;
    while (titleBytes[i] != '\0') {
        i += 2;
        if (titleBytes[i - 2] != 'K') continue;
        i += 2;
        if (titleBytes[i - 2] != 'B') continue;

        if (('0' <= titleBytes[i] && titleBytes[i] <= '9')) {
            while ('0' <= titleBytes[i] && titleBytes[i] <= '9') {
                res *= 10;
                res += titleBytes[i] - '0';
                i += 2;
            }
            return res;
        }
    }
    return 0;
}

void kbs_InsertKB(TreeSet **root, TreeSet *node) {
    if (*root == NULL) {
        *root = node;
    } else {
        TreeSet *rootNode = *root;
        if (node->Value < rootNode->Value)
            kbs_InsertKB(&rootNode->Left, node);
        else if (node->Value > rootNode->Value)
            kbs_InsertKB(&rootNode->Right, node);
        else
            return;
    }
}

void kbs_appendKBs(clog_Arena *a, TreeSet *root) {
    LOG_DEBUG("\t\tkbs.c: Appending KB%d.", root->Value);
    if (root->Left) kbs_appendKBs(a, root->Left);
    clog_ArenaAppend(a, "\nKB%lu", root->Value);
    if (root->Right) kbs_appendKBs(a, root->Right);
}

void clog_kbs(clog_Arena scratch) {
    TreeSet *kbs = NULL;
    IUpdateSession *session = NULL;
    IUpdateSearcher *searcher = NULL;
    ISearchResult *searchResult = NULL;
    IUpdateCollection *updates = NULL;
    clog_ArenaAppend(&scratch, "[kbs]");



    LOG_DEBUG("\tkbs.c: Initializing COM library.");
    HRESULT status = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (status != S_OK) {
        LOG_DEBUG("\tkbs.c: COM library could not be initalized, error code %lu.", GetLastError());
        clog_ArenaAppend(&scratch, "(Unable to initialize COM)");
        return;
    }

#define HANDLE_COM_ALLOCATION(obj)                                                               \
    if (status != S_OK) {                                                                   \
        LOG_DEBUG("\tkbs.c: COM library " #obj " failed, error code %lu.", GetLastError()); \
        goto Cleanup;                                                                       \
    }                                                                                       \
    clog_Defer(&scratch, obj, RETURN_LONG, obj->lpVtbl->Release)

    LOG_DEBUG("\tkbs.c: Starting COM session.");
    status = CoCreateInstance(&CLSID_UpdateSession, NULL, CLSCTX_INPROC_SERVER, &IID_IUpdateSession, (LPVOID *)&session);
    HANDLE_COM_ALLOCATION(session);

    LOG_DEBUG("\tkbs.c: Creating searcher.");
    status = session->lpVtbl->CreateUpdateSearcher(session, &searcher);
    HANDLE_COM_ALLOCATION(searcher);

    wchar_t *queryChar = L"( IsInstalled = 1 and IsHidden = 0 )";
    BSTR query = SysAllocString(queryChar);
    clog_Defer(&scratch, query, RETURN_VOID, SysFreeString);
    // Very slow, see earlier commit in history for other version using another method
    status = searcher->lpVtbl->Search(searcher, query, &searchResult);
    HANDLE_COM_ALLOCATION(searchResult);

    OperationResultCode searchResultCode = 0;
    status = searchResult->lpVtbl->get_ResultCode(searchResult, &searchResultCode);
    if (status != S_OK || (searchResultCode != orcSucceeded && searchResultCode != orcSucceededWithErrors)) {
        LOG_DEBUG("\tkbs.c: Could not complete search, error code %lu.", GetLastError());
        goto Cleanup;
    }

    status = searchResult->lpVtbl->get_Updates(searchResult, &updates);
    HANDLE_COM_ALLOCATION(updates);

    LONG count = 0;
    status = updates->lpVtbl->get_Count(updates, &count);
    if (status != S_OK) {
        LOG_DEBUG("\tkbs.c: Could not get count, error code %lu.", GetLastError());
        goto Cleanup;
    }
    if (count == 0) {
        LOG_DEBUG("\tkbs.c: Searcher update count is zero.");
        goto Cleanup;
    }

    for (int i = 0; i < count; i++) {
        LOG_DEBUG("\tkbs.c: Item %d:", i);
        IUpdate *item;
        updates->lpVtbl->get_Item(updates, i, &item);
        if (status != S_OK) {
            LOG_DEBUG("\t\tkbs.c: Could not get item %d, error code %lu.", i, GetLastError());
            continue;
        }

        wchar_t *title = NULL;
        item->lpVtbl->get_Title(item, (short unsigned int **)&title);
        if (status != S_OK) {
            LOG_DEBUG("\t\tkbs.c: Could not get item title, error code %lu.", GetLastError());
            goto NextItem;
        }

        LOG_DEBUG("\t\tkbs.c: Extracting KB number from title.");
        DWORD kb = kbs_ExtractKBNumber(title);
        if (kb) {
            LOG_DEBUG("\t\tkbs.c: Extracted KB%lu. Adding to list.", kb);
            TreeSet *node = clog_ArenaAlloc(&scratch, TreeSet, 1);
            *node = (TreeSet){0};
            node->Value = kb;
            kbs_InsertKB(&kbs, node);
        } else
            LOG_DEBUG("\t\tkbs.c: Title did not contain KB number, skipping.");

    NextItem:
        LOG_DEBUG("\t\tkbs.c: Releasing item %d.", i);
        item->lpVtbl->Release(item);
    }

Cleanup:
    LOG_DEBUG("\tkbs.c: Performing cleanup.");
    clog_PopDeferAll(&scratch);
    CoUninitialize();

    LOG_DEBUG("\tkbs.c: Cleanup complete.");
    if (kbs) {
        LOG_DEBUG("\tkbs.c: Printing KBs to output.");
        kbs_appendKBs(&scratch, kbs);
    } else {
        LOG_DEBUG("\tkbs.c: No installed KBs found.");
        clog_ArenaAppend(&scratch, "(No KBs found)");
    }
}

#ifdef STANDALONE
int main(int argc, CHAR *argv[]) {
    clog_ArenaState *st = clog_ArenaMake(0x20000);
    clog_kbs(st->Memory);
    printf("%s", st->Start);
}
#endif
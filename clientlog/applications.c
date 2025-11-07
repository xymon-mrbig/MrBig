#include "clientlog.h"

#define MAX_APPLICATION_NAME (64)
#define MAX_APPLICATION_VERSION (64)
#define MAX_APPLICATION_PUBLISHER (64)
#define MAX_APPLICATIONS_ROW_SIZE (16 + MAX_APPLICATION_NAME + MAX_APPLICATION_VERSION + MAX_APPLICATION_PUBLISHER)

#define MAX_REG_KEY_NAME (255)

typedef enum {
    x86_32 = 0,
    x86_64,
} applications_Arch;

LPCSTR applications_PrettyArch(applications_Arch a) {
    return a == x86_32 ? "x86-32" : "x64";
}

typedef struct {
    CHAR Name[MAX_APPLICATION_NAME];
    CHAR Version[MAX_APPLICATION_VERSION];
    CHAR Publisher[MAX_APPLICATION_PUBLISHER];
    applications_Arch Architecture;
} applications_Application;

typedef struct _ApplicationTree {
    applications_Application Value;
    struct _ApplicationTree *Left;
    struct _ApplicationTree *Right;
} applications_ApplicationTree;

LPCSTR applications_PrettyApplication(applications_Application *a, LPSTR out) {
    snprintf(out, MAX_APPLICATIONS_ROW_SIZE, "%s\t%s\t%s\t%s", a->Name, a->Version, a->Publisher, applications_PrettyArch(a->Architecture));
    return out;
}

// Returns root, or t if root is NULL
applications_ApplicationTree *applications_TreeInsert(applications_ApplicationTree *root, applications_ApplicationTree *t) {
    if (root == NULL) {
        return t;
    }

    int tComp = strncmp(t->Value.Name, root->Value.Name, MAX_APPLICATION_NAME);
    tComp = tComp ? tComp : (int)(t->Value.Architecture - root->Value.Architecture);
    applications_ApplicationTree **direction;
    if (tComp < 0) {
        direction = &root->Left;
    } else if (tComp > 0) {
        direction = &root->Right;
    } else {
        if (root->Left == NULL) {
            direction = &root->Left;
        } else {
            direction = &root->Right;
        }
    }
    if (*direction == NULL) {
        *direction = t;
    } else {
        applications_TreeInsert(*direction, t);
    }
    return root;
}

void applications_TreeTraverseAppend(applications_ApplicationTree *t, clog_Arena *a) {
    if (t->Left != NULL) {
        applications_TreeTraverseAppend(t->Left, a);
    }
    CHAR applicationBuf[MAX_APPLICATIONS_ROW_SIZE];
    clog_ArenaAppend(a, "\n%s", applications_PrettyApplication(&t->Value, applicationBuf));
    if (t->Right != NULL) {
        applications_TreeTraverseAppend(t->Right, a);
    }
}

applications_ApplicationTree *applications_InsertApplications(applications_ApplicationTree *root, applications_Arch arch, clog_Arena *a) {
    HKEY hKey;
    DWORD openStatus;
    switch (arch) {
    case x86_32:
        openStatus = RegOpenKeyEx(HKEY_LOCAL_MACHINE, "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall", 0, KEY_READ /*| KEY_WOW64_32KEY*/, &hKey);
        break;
    case x86_64:
        openStatus = RegOpenKeyEx(HKEY_LOCAL_MACHINE, "Software\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall", 0, KEY_READ | KEY_WOW64_64KEY, &hKey);
        break;
    default:
        clog_ArenaAppend(a, "\nError: Unknown architecture (%d).", arch);
        return root;
    }

    if (openStatus != ERROR_SUCCESS) {
        return root;
    }

    clog_Defer(a, hKey, RETURN_LONG, &RegCloseKey);

    DWORD ix = 0;
    CHAR regKeyName[MAX_REG_KEY_NAME];
    DWORD regKeyNameLen = MAX_REG_KEY_NAME;
    DWORD enumStatus = RegEnumKeyEx(hKey, ix, regKeyName, &regKeyNameLen, 0, NULL, NULL, NULL);

    DWORD displayNameBufSize = MAX_PATH, displayVersionBufSize = 255, publisherBufSize = 255, bufsize;
    CHAR displayName[MAX_PATH], displayVersion[displayVersionBufSize], publisherName[publisherBufSize];
    while (enumStatus == ERROR_SUCCESS) {
        bufsize = displayNameBufSize;
        DWORD nameStatus = RegGetValueA(hKey, regKeyName, "DisplayName", RRF_RT_REG_SZ | RRF_RT_REG_MULTI_SZ | RRF_RT_REG_EXPAND_SZ, NULL, displayName, &bufsize);

        bufsize = displayVersionBufSize;
        DWORD versionStatus = RegGetValueA(hKey, regKeyName, "DisplayVersion", RRF_RT_REG_SZ | RRF_RT_REG_MULTI_SZ | RRF_RT_REG_EXPAND_SZ, NULL, displayVersion, &bufsize);

        bufsize = publisherBufSize;
        DWORD publisherStatus = RegGetValueA(hKey, regKeyName, "Publisher", RRF_RT_REG_SZ | RRF_RT_REG_MULTI_SZ | RRF_RT_REG_EXPAND_SZ, NULL, publisherName, &bufsize);

        if (versionStatus == ERROR_SUCCESS || publisherStatus == ERROR_SUCCESS) {
            applications_ApplicationTree *app = clog_ArenaAlloc(a, applications_ApplicationTree, 1);

            CHAR *name = nameStatus == ERROR_SUCCESS ? displayName : regKeyName;
            clog_utils_ClampString(name, app->Value.Name, MAX_APPLICATION_NAME);

            CHAR *version = versionStatus == ERROR_SUCCESS ? displayVersion : "-";
            clog_utils_ClampString(version, app->Value.Version, MAX_APPLICATION_VERSION);

            CHAR *publisher = publisherStatus == ERROR_SUCCESS ? publisherName : "-";
            clog_utils_ClampString(publisher, app->Value.Publisher, MAX_APPLICATION_PUBLISHER);

            app->Value.Architecture = arch;

            root = applications_TreeInsert(root, app);
        }
        regKeyNameLen = MAX_REG_KEY_NAME;
        enumStatus = RegEnumKeyEx(hKey, ++ix, regKeyName, &regKeyNameLen, 0, NULL, NULL, NULL);
    }

    return root;
}

void clog_applications(clog_Arena scratch) {
    applications_ApplicationTree *applications = NULL;
    applications = applications_InsertApplications(applications, x86_32, &scratch);
    clog_PopDeferAll(&scratch);
    applications = applications_InsertApplications(applications, x86_64, &scratch);
    clog_PopDeferAll(&scratch);

    clog_ArenaAppend(&scratch, "[applications]");
    if (applications != NULL) {
        applications_TreeTraverseAppend(applications, &scratch);
    } else {
        clog_ArenaAppend(&scratch, "\n(No applications found)");
    }
}

#ifdef STANDALONE
int main(int argc, char *argv[]) {
    clog_ArenaState *st = clog_ArenaMake(0x10000);
    clog_applications(st->Memory);
    printf((char *)st->Start);
    return 0;
}
#endif
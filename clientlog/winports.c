#include "clientlog.h"
#include <iphlpapi.h>

/* [winports] closely modelled after shell command 'netstat -ano'

    We need to link with Iphlpapi.lib in Makefile.
   This file also needs Ws2_32.lib, but that is a requirement for other files too */

#define MAX_PORT_ENTRIES 5000
#define MAX_PORT 0xFFFF
#define SIMILAR_ENTRIES_MAX_NUM 20

typedef enum {
    TCP = 1,
    UDP,
} winports_Protocol;

typedef enum {
    IPv4 = 2,
    IPv6 = 23,
} winports_IpVersion;

typedef struct {
    CHAR LocalAddress[64], RemoteAddress[64];
    DWORD LocalPort, RemotePort;
    DWORD State;
    DWORD PID;
} winports_Record;

typedef union {
    MIB_TCPTABLE_OWNER_PID TCP4;
    MIB_UDPTABLE_OWNER_PID UDP4;
    MIB_TCP6TABLE_OWNER_PID TCP6;
    MIB_UDP6TABLE_OWNER_PID UDP6;
} winports_ConnectionTable;

CHAR *winports_PrettyIPv6(BYTE *b, CHAR *out) {
    UINT16 group[8];
    for (DWORD i = 0; i < 8; i++) {
        UINT16 b1 = b[15 - 2 * i];
        UINT16 b2 = b[14 - 2 * i];
        group[i] = ((UINT16)b1 << 8) + b2;
    }

    DWORD largestZeroClusterIx = 0;
    DWORD largestZeroClusterLen = 0;
    DWORD curr, len;
    for (DWORD i = 0; i < 8; i++) {
        if (group[i] == 0) {
            curr = i;
            len = 1;
            while (++i < 8 && group[i] == 0) {
                len++;
            }
            if (len > largestZeroClusterLen) {
                largestZeroClusterIx = curr;
                largestZeroClusterLen = len;
            }
        }
    }

    if (largestZeroClusterLen == 0) {
        largestZeroClusterIx = 8;
    }

    CHAR *currOut = out;
    currOut += sprintf(currOut, "[");
    for (DWORD i = 0; i < largestZeroClusterIx; i++) {
        currOut += sprintf(currOut, "%x", group[i]);
        if (i != largestZeroClusterIx - 1)
            currOut += sprintf(currOut, ":");
    }
    if (largestZeroClusterLen > 0) {
        currOut += sprintf(currOut, "::");
    }
    for (DWORD i = largestZeroClusterIx + largestZeroClusterLen; i < 8; i++) {
        currOut += sprintf(currOut, "%x", group[i]);
        if (i != 7)
            currOut += sprintf(currOut, ":");
    }
    currOut += sprintf(currOut, "]");
    return out;
}

LPCSTR winports_PrettyPortState(DWORD state, CHAR *out) {
    switch (state) {
    case MIB_TCP_STATE_CLOSED:
        return "CLOSED";
    case MIB_TCP_STATE_LISTEN:
        return "LISTENING";
    case MIB_TCP_STATE_SYN_SENT:
        return "SYN_SENT";
    case MIB_TCP_STATE_SYN_RCVD:
        return "SYN_RECEIVED";
    case MIB_TCP_STATE_ESTAB:
        return "ESTABLISHED";
    case MIB_TCP_STATE_FIN_WAIT1:
        return "FIN_WAIT1";
    case MIB_TCP_STATE_FIN_WAIT2:
        return "FIN_WAIT2";
    case MIB_TCP_STATE_CLOSE_WAIT:
        return "CLOSE_WAIT";
    case MIB_TCP_STATE_CLOSING:
        return "CLOSING";
    case MIB_TCP_STATE_LAST_ACK:
        return "LAST_ACK";
    case MIB_TCP_STATE_TIME_WAIT:
        return "TIME_WAIT";
    case MIB_TCP_STATE_DELETE_TCB:
        return "DELETE_TCB";
    default:
        return "(Unknown)";
    }
}

void *winports_GetConnectionTable(const ULONG af, const winports_Protocol proto, clog_Arena *a) {
    // Real world use indicate that these tables can be enormous,
    // so we allocate space using malloc rather than arena. Remember to free!
    DWORD status = NO_ERROR, size = 0;
    void *result = NULL;
    if (TCP == proto) {
        status = GetExtendedTcpTable(NULL, &size, FALSE, af, TCP_TABLE_OWNER_PID_ALL, 0);
        if (status != ERROR_INSUFFICIENT_BUFFER) return NULL;
        result = malloc(size); // clog_ArenaAlloc(a, void, size);
        clog_Defer(a, result, RETURN_VOID, &free);
        if (result == NULL) return NULL;
        status = GetExtendedTcpTable(result, &size, TRUE, af, TCP_TABLE_OWNER_PID_ALL, 0);
    } else if (UDP == proto) {
        status = GetExtendedUdpTable(NULL, &size, FALSE, af, UDP_TABLE_OWNER_PID, 0);
        if (status != ERROR_INSUFFICIENT_BUFFER) return NULL;
        result = malloc(size); // clog_ArenaAlloc(a, void, size);
        clog_Defer(a, result, RETURN_VOID, &free);
        if (result == NULL) return NULL;
        status = GetExtendedUdpTable(result, &size, FALSE, af, UDP_TABLE_OWNER_PID, 0);
    }
    if (status != NO_ERROR) return NULL;

    return result;
}

void winports_GetRow(winports_ConnectionTable *connections, DWORD i, const ULONG af, const winports_Protocol proto, winports_Record *out) {
    struct in_addr ipAddress;
    if (IPv4 == af) {
        if (TCP == proto) {
            MIB_TCPROW_OWNER_PID *tcp4row = &connections->TCP4.table[i];
            ipAddress.S_un.S_addr = (u_long)tcp4row->dwLocalAddr;
            snprintf(out->LocalAddress, sizeof(out->LocalAddress), "%s", inet_ntoa(ipAddress));
            out->LocalPort = ntohs(tcp4row->dwLocalPort);
            ipAddress.S_un.S_addr = (u_long)tcp4row->dwRemoteAddr;
            snprintf(out->RemoteAddress, sizeof(out->RemoteAddress), "%s", inet_ntoa(ipAddress));
            out->RemotePort = ntohs(tcp4row->dwRemotePort);
            out->State = tcp4row->dwState;
            out->PID = tcp4row->dwOwningPid;
        } else if (UDP == proto) {
            MIB_UDPROW_OWNER_PID *udp4row = &connections->UDP4.table[i];
            ipAddress.S_un.S_addr = (u_long)udp4row->dwLocalAddr;
            snprintf(out->LocalAddress, sizeof(out->LocalAddress), "%s", inet_ntoa(ipAddress));
            out->LocalPort = ntohs(udp4row->dwLocalPort);
            memcpy(out->RemoteAddress, "*:*", 4); // same as netstat
            out->State = 99;
            out->PID = udp4row->dwOwningPid;
        }
    } else if (IPv6 == af) {
        if (TCP == proto) {
            MIB_TCP6ROW_OWNER_PID *tcp6row = &connections->TCP6.table[i];
            winports_PrettyIPv6(tcp6row->ucLocalAddr, out->LocalAddress);
            out->LocalPort = ntohs((u_short)tcp6row->dwLocalPort);
            winports_PrettyIPv6(tcp6row->ucRemoteAddr, out->RemoteAddress);
            out->RemotePort = ntohs((u_short)tcp6row->dwRemotePort);
            out->State = tcp6row->dwState;
            out->PID = tcp6row->dwOwningPid;
        } else if (UDP == proto) {
            MIB_UDP6ROW_OWNER_PID *udp6row = &connections->UDP6.table[i];
            winports_PrettyIPv6(udp6row->ucLocalAddr, out->LocalAddress);
            out->LocalPort = ntohs((u_short)udp6row->dwLocalPort);
            memcpy(out->RemoteAddress, "*:*", 4); // same as netstat
            out->State = 99;
            out->PID = udp6row->dwOwningPid;
        }
    }
}

// N.B. x ^ x = 0, x ^ y = y ^ x
// => b -> a ^ b ^ b = a,
// => a -> a ^ b ^ a = b.
#define PTR_SWAP(a, b)                       \
    a = (void *)((intptr_t)a ^ (intptr_t)b); \
    b = (void *)((intptr_t)a ^ (intptr_t)b); \
    a = (void *)((intptr_t)a ^ (intptr_t)b);

void winports_AppendConnections(winports_ConnectionTable *connections, const ULONG af, const winports_Protocol proto, clog_Arena *a) {
    winports_Record rData = {0}, rPrevData = {0};
    winports_Record *r = &rData, *rPrev = &rPrevData;

    CHAR portStateBuf[16], localAddressPortBuf[74], remoteAddressPortBuf[74];
    DWORD numEntries = min(connections->TCP4.dwNumEntries /* Well... If it works, it works */, MAX_PORT_ENTRIES);

    BOOL skipping = FALSE;
    DWORD range = 1;
    #define SKIPPING_FMT "\n\t...IP %s and PID %lu spans above %d entries, and %lu more with highest local port %u"
    for (DWORD i = 0; i < numEntries; i++) {
        PTR_SWAP(r, rPrev);

        winports_GetRow(connections, i, af, proto, r);
        if (r->PID == rPrev->PID && strcmp(r->LocalAddress, rPrev->LocalAddress) == 0) {
            if (++range > SIMILAR_ENTRIES_MAX_NUM) skipping = TRUE;
            if (skipping) continue;
        } else {
            if (skipping)
                clog_ArenaAppend(a, SKIPPING_FMT, rPrev->LocalAddress, rPrev->PID, SIMILAR_ENTRIES_MAX_NUM, range - SIMILAR_ENTRIES_MAX_NUM, rPrev->LocalPort);
            skipping = FALSE;
            range = 1;
        }

        snprintf(localAddressPortBuf, 74, "%s:%lu", r->LocalAddress, r->LocalPort);
        if (proto == TCP) {
            snprintf(remoteAddressPortBuf, 74, "%s:%lu", r->RemoteAddress, r->RemotePort);
        } else {
            snprintf(remoteAddressPortBuf, 64, "%s", r->RemoteAddress);
        }

        clog_ArenaAppend(a, "\n%-7s\t%-39s\t%-39s\t%-15s\t%7lu",
                         proto == TCP ? "TCP" : "UDP",
                         localAddressPortBuf,
                         remoteAddressPortBuf,
                         proto == TCP ? winports_PrettyPortState(r->State, portStateBuf) : "",
                         r->PID);
    }
    if (skipping) {
        clog_ArenaAppend(a, SKIPPING_FMT, rPrev->LocalAddress, rPrev->PID, SIMILAR_ENTRIES_MAX_NUM, range - SIMILAR_ENTRIES_MAX_NUM, rPrev->LocalPort);
    }
    #undef SKIPPING_FMT
    if (connections->TCP4.dwNumEntries > MAX_PORT_ENTRIES) {
        clog_ArenaAppend(a, "\n(+ %lu more %s %s entries)", connections->TCP4.dwNumEntries - MAX_PORT_ENTRIES,  proto == TCP ? "TCP" : "UDP", af == IPv4 ? "IPv4" : "IPv6");
    }
}

void winports_AppendPortStatistics(ULONG af, winports_Protocol proto, DWORD numEntries, clog_Arena *a) {
    LPCTSTR ipVersion = af == IPv4 ? "IPv4" : "IPv6";
    LPCTSTR protocol = proto == TCP ? "TCP" : "UDP";

    DOUBLE percentPortsBusy = 100 * ((DOUBLE)numEntries) / ((DOUBLE)MAX_PORT);
    clog_ArenaAppend(a, "\n%-15s\t%-15s\t%15lu\t%13.2lf",
                     ipVersion, protocol, numEntries, percentPortsBusy);
}

void clog_winports(clog_Arena scratch) {
    struct {
        winports_Protocol proto;
        winports_IpVersion version;
    } portGroups[] = {
        {TCP, IPv4},
        {TCP, IPv6},
        {UDP, IPv4},
        {UDP, IPv6},
    };

    winports_ConnectionTable *tables[lengthof(portGroups)];
    for (int i = 0; i < lengthof(portGroups); i++)
        tables[i] = winports_GetConnectionTable(portGroups[i].version, portGroups[i].proto, &scratch);

    clog_ArenaAppend(&scratch, "[winportsused]");
    clog_ArenaAppend(&scratch, "\n%-15s\t%-15s\t%15s\t%13s", "IP version", "Protocol", "Ports Used #", "Ports Used %");
    int errored = 0;
    for (int i = 0; i < lengthof(portGroups); i++) {
        if (tables[i] != NULL)
            winports_AppendPortStatistics(portGroups[i].version, portGroups[i].proto, tables[i]->TCP4.dwNumEntries, &scratch);
        else
            errored++;
    }
    if (errored == lengthof(portGroups))
        clog_ArenaAppend(&scratch, "\n(Unable to get networking statistics)");
    else if (errored > 0)
        clog_ArenaAppend(&scratch, "\n(Unable to get some of the networking statistics)");

    clog_ArenaAppend(&scratch, "\n[winports]");
    clog_ArenaAppend(&scratch, "\n%-7s\t%-39s\t%-39s\t%-15s\t%7s", "Proto", "Local Address", "Foreign Address", "State", "PID");
    errored = 0;
    for (int i = 0; i < lengthof(portGroups); i++) {
        if (tables[i] != NULL) {
            winports_AppendConnections(tables[i], portGroups[i].version, portGroups[i].proto, &scratch);
        } else
            errored++;
    }
    clog_PopDeferAll(&scratch); // free malloced memory in winports_GetConnectionTable
    if (errored == lengthof(portGroups))
        clog_ArenaAppend(&scratch, "\n(Unable to get networking statistics)");
    else if (errored > 0)
        clog_ArenaAppend(&scratch, "\n(Unable to get some of the networking statistics)");
}

#ifdef STANDALONE
int main(int argc, CHAR *argv[]) {
    clog_ArenaState *st = clog_ArenaMake(0x10000);
    clog_winports(st->Memory);
    printf("%s", st->Start);
}
#endif
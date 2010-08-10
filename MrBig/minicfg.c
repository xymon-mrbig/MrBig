#define _WIN32_WINNT 0x0500

#include <windows.h>
#include <winsock2.h>
#include <stdio.h>

#define BB_DISPLAY "127.0.0.1"
#define BB_PORT 1984
#define BB_HOSTS "./bb-hosts"

static WSADATA wsaData;
static int ws_started = 0;

int start_winsock(void)
{
	int n;

	if (!ws_started) {
		n = WSAStartup(MAKEWORD(2, 2), &wsaData);
		if (n != NO_ERROR) {
			printf("Error at WSAStartup()");
		} else {
			ws_started = 1;
		}
	}
	return ws_started;
}

void stop_winsock(void)
{
	if (ws_started) WSACleanup();
	ws_started = 0;
}

int main(void) {

    // Initialize Winsock.
    int n, m, x;
    FILE *fp;
    struct sockaddr_in cli_addr;
    int clilen;
    char *cli;
    char b[1024], ipaddr[1024], machine[1024];

    if (!start_winsock()) return 0;

    // Create a socket.
    SOCKET m_socket;
    m_socket = socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );

    if ( m_socket == INVALID_SOCKET ) {
        printf( "Error at socket(): %d\n", WSAGetLastError() );
	stop_winsock();
        return 0;
    }

    // Bind the socket.
    struct sockaddr_in service;

    service.sin_family = AF_INET;
    service.sin_addr.s_addr = inet_addr( "127.0.0.1" );
    service.sin_port = htons( 27016 );

    if ( bind( m_socket, (SOCKADDR*) &service, sizeof(service) ) == SOCKET_ERROR ) {
        printf( "bind() failed.\n" );
        closesocket(m_socket);
	stop_winsock();
        return 0;
    }
    
    // Listen on the socket.
    if ( listen( m_socket, 1 ) == SOCKET_ERROR )
        printf( "Error listening on socket.\n");

    // Accept connections.
    int s;

    for (;;) {
        printf( "Waiting for a client to connect...\n" );
        while (1) {
            s = SOCKET_ERROR;
            while ( s == SOCKET_ERROR ) {
		clilen = sizeof cli_addr;
                s = accept( m_socket, (struct sockaddr *)&cli_addr, &clilen );
            }
	    cli = inet_ntoa(cli_addr.sin_addr);
            printf( "Client Connected from %s.\n", cli);
	    fflush(stdout);
            break;
        }

        // Send and receive data.
        int bytesRecv = SOCKET_ERROR;
        char sendbuf[32000] = "Server: Sending Data.";
 
	sprintf(machine, "brokencfg-%s", cli);
	fp = fopen(BB_HOSTS, "r");
	if (fp) {
		while (fgets(b, sizeof b, fp)) {
			n = sscanf(b, "%s %s", ipaddr, machine);
			if (n != 3) continue;
			if (!strcmp(ipaddr, cli)) break;
		}
	}
	snprintf(sendbuf, sizeof sendbuf,
		"machine %s\n"
		"display %s\n"
		"port %d\n",
		machine, BB_DISPLAY, BB_PORT);
        n = strlen(sendbuf);
        m = 0;
        while ((m < n) && (x = send(s, sendbuf+m, n-m, 0)) > 0) {
	    m += x;
        }
        closesocket(s);
    }

    return 0;
}

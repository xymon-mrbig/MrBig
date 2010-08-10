#define _WIN32_WINNT 0x0500

#include <windows.h>
#include <winsock2.h>
#include <stdio.h>

int main(int argc, char **argv)
{
	char *addr;
	int port;

	char recvbuf[32000];
	int bytesRecv;
	int m;
	int s;
	struct sockaddr_in service;
	// Initialize Winsock.
	WSADATA wsaData;
	int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (iResult != NO_ERROR)
		printf("Error at WSAStartup()\n");

	if (argc > 2) port = atoi(argv[2]);
	else port = 1984;

	if (argc > 1) addr = argv[1];
	else addr = "0.0.0.0";

	// Create a socket.
	m = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	if (m == -1) {
		printf("Error at socket(): %d\n", WSAGetLastError());
		WSACleanup();
		return 0;
	}
	// Bind the socket.

	service.sin_family = AF_INET;
	service.sin_addr.s_addr = inet_addr(addr);
	service.sin_port = htons(port);

	if (bind(m, (struct sockaddr *)&service, sizeof(service)) == -1) {
		printf("bind() failed.\n");
		closesocket(m);
		return 0;
	}
	// Listen on the socket.
	if (listen(m, 1) == -1)
		printf("Error listening on socket.\n");

	// Accept connections.

	for (;;) {
		printf("Waiting for a client to connect...\n");
		while (1) {
			s = -1;
			while (s == -1) {
				s = accept(m, NULL, NULL);
			}
			printf("Client Connected.\n");
			break;
		}

		// Send and receive data.
		bytesRecv = recv(s, recvbuf, sizeof recvbuf, 0);
		printf("Bytes Recv: %d\n", bytesRecv);
		fwrite(recvbuf, 1, bytesRecv, stdout);
		fflush(stdout);
		closesocket(s);
	}

	return 0;
}

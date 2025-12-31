#include <stdio.h>
#include <WS2tcpip.h>
#include "client_init.h"
#include "shared_utils.h"

SOCKET connect_to_server(const char* addr, uint16_t port, const char* username)
{
	SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sock == INVALID_SOCKET)
	{
		fprintf(stderr, "Error: failed to create socket\n");
		return sock;
	}

	set_sock_timeout(sock, MSG_SND_TIMEOUT, MSG_RECV_TIMEOUT);
	struct sockaddr_in saddr = { .sin_family = AF_INET, .sin_addr.s_addr = inet_addr(addr), .sin_port = htons(port) };
	int32_t status = connect(sock, (struct sockaddr*)&saddr, sizeof(struct sockaddr_in));

	if (status == SOCKET_ERROR)
	{
		fprintf(stderr, "Error: failed to connect to server\n");
		closesocket(sock);
		return INVALID_SOCKET;
	}

	int32_t sent = send_all(sock, username, UNAME_MAXSIZE);
	if (sent != UNAME_MAXSIZE)
	{
		fprintf(stderr, "Error: handshake failed\n");
		closesocket(sock);
		return INVALID_SOCKET;
	}

	printf("Info: connected to the server\n");
	return sock;
}
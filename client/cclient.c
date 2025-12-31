#include <WinSock2.h>
#include <stdio.h>
#include <ws2tcpip.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include "shared_utils.h"
#include "client_init.h"
#include "msg_handling.h"
#include "client_utils.h"
#include "input_parsing.h"

#pragma comment(lib,"ws2_32.lib")
#define TIMEOUT_INF -1
#define DEFAULT_PORT 5234
#define MIN_RESERVED_FNAMELEN 10

int main(void)
{
	int err, devnull;
	if (err = ws2_init())
	{
		fprintf(stderr, "Error: %"PRId32", exitting program...\n", err);
		WSACleanup();
		return 1;
	}

	char server_addr[INET_ADDRSTRLEN];
	char username[UNAME_MAXSIZE];
	char buf[BUF_SIZE];
	char file_folder[MAX_PATH];

	uint16_t server_port = 0;
#ifndef NDEBUG
	strcpy(server_addr, "127.0.0.1");
	server_port = DEFAULT_PORT;
	strcpy(file_folder, "D:\\testfolder");
#else
	printf("Enter the server address: ");
	devnull = scanf("%15s", server_addr);
	printf("\nEnter the server port: ");
	devnull = scanf("%"PRIu16, &server_port);

	do
	{
		printf("\nEnter the path to save incoming files (249 chars max): ");
		devnull = scanf("%259s", file_folder);
	} 
	while (flush_stdin(), strlen(file_folder) + 1 >= MAX_PATH - MIN_RESERVED_FNAMELEN);
#endif
	set_file_folder(file_folder);
	printf("\nPlease enter a username: ");
	devnull = scanf("%31s", username);
	printf("\n");
	flush_stdin();

	SOCKET sock = connect_to_server(server_addr, server_port, username);
	if (sock == INVALID_SOCKET)
	{
		WSACleanup();
		return 1;
	}
	set_sock_timeout(sock, MSG_SND_TIMEOUT, MSG_RECV_TIMEOUT);
	printf("Waiting...\n");
	header hdr;
	WSAPOLLFD pollstruct = { .fd = sock, .events = POLLRDNORM };
	active_names* anames = NULL;
	int32_t status;
	u_clfunc_status ustatus = OK;
	uint8_t tokenflag = 1;

	while (1)
	{
		DEBUG_PRINT("Polling.");
		status = WSAPoll(&pollstruct, 1, TIMEOUT_INF);
		DEBUG_PRINT("Poll result: %"PRId16, pollstruct.revents);
		if (status < 0 || pollstruct.revents & (POLLHUP | POLLERR)) break;
		status = get_header(sock, &hdr);
		DEBUG_PRINT_HDR(&hdr, 'o');

		if (status < 0) 
		{
			DEBUG_PRINT("Failed to receive header.");
			break;
		}

		afptr action = cl_interpret_header(sock, &hdr, &tokenflag);

		if (action)
			ustatus = action(sock, &hdr, buf, &anames);
		else
		{
			DEBUG_PRINT("Something went wrong with interpretation.");
		}

		if (ustatus == EFATAL) break;
		else if (!tokenflag) continue;

	repeat_input:
		DEBUG_PRINT("Input.");
		printf("You: ");
		fgets(buf, BUF_SIZE, stdin);
		ustatus = interpret_input(&hdr, buf, anames, username, &action);
		DEBUG_PRINT_HDR(&hdr, 'c');

		if (ustatus == ENONFATAL)
		{
			DEBUG_PRINT("Repeating input.");
			goto repeat_input;
		}
		else if (ustatus == SIGQUIT)
		{
			DEBUG_PRINT("Quitting.");
			break;
		}

		if (action)
			ustatus = action(sock, &hdr, buf, &anames);

		if (ustatus == EFATAL) 
		{
			DEBUG_PRINT("Fatal error, qutting.");
			break;
		}
		status = 0, ustatus = OK;
	}
	
	printf("closing connection\n");
	release_anames(&anames);
	closesocket(sock);
	WSACleanup();
	return 0;
}
#include <inttypes.h>
#include <WinSock2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ws2tcpip.h>
#include <time.h>
#include "shared_utils.h"
#include "server_setup.h"
#include "client_handling.h"
#include "error_handling.h"
#include "server_utils.h"
#include "routing_ops.h"
#pragma comment(lib,"ws2_32.lib")
#ifndef NDEBUG
#define MSG_WAIT_TIMEOUT -1
#define DEF_CLIENT_NUM 3
#define DEF_PORT 5234
#define DEF_INT_OPTNUM 1
#else
#define MSG_WAIT_TIMEOUT 20000
#endif

int main(void)
{
	int32_t err = 0;
	int32_t dev_null;
	if (err = ws2_init())
	{
		fprintf(stderr, "Error: %"PRId32", exitting program...\n", err);
		WSACleanup();
		return 0;
	}

	SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (!sock)
	{
		printf("Error setting up socket, exiting program...\n");
		WSACleanup();
		return 0;
	}

	int8_t s_interface_optnum;
	char s_interface[INET_ADDRSTRLEN];
	uint8_t client_num = 0;
	uint8_t client_cnt = 0;
	uint16_t port = 0;

#ifdef NDEBUG
	printf("Please select the interface you wish the server to listen on:\n"
		"1 - localhost (default)\n"
		"2 - Device's main network adapter (local IPv4)\n"
		"3 - enter manually (IPv4 only)\n"
		"4 - all interfaces\n"
		"Enter the corresponding number here: "
		);
	dev_null = scanf("%1"PRId8, &s_interface_optnum);
	flush_stdin();
	s_set_interface(s_interface, s_interface_optnum);
	printf("\nThe selected interface is: %s\n", s_interface);
	printf("\nEnter the port to listen on: ");
	dev_null = scanf("%"PRIu16, &port);
	printf("\nEnter the expected number of clients (10 second timeout from last join): ");
	dev_null = scanf("%"PRIu8, &client_num);
#else
	s_interface_optnum = DEF_INT_OPTNUM;
	client_num = DEF_CLIENT_NUM;
	port = DEF_PORT;
	s_set_interface(s_interface, s_interface_optnum);
#endif

	uint32_t htsize = HT_SIZE_CONST * client_num;
	client** clients_ht = (client**)calloc(htsize, sizeof(client*));
	client** clients_arr = (client**)calloc(client_num, sizeof(client*));
	cl_param_struct cl_params = { clients_ht, clients_arr, &client_num, &client_cnt, &htsize };
	s_bind_listen(s_interface, sock, port);
	printf("\nWaiting for clients...");
	char recvbuf[BUF_SIZE];
	uint32_t recvtimeout = MSG_RECV_TIMEOUT;
	uint32_t sndtimeo = MSG_SND_TIMEOUT;
	set_sock_timeout(sock, sndtimeo, recvtimeout);
	header m_header;
	memset((void*)&m_header, 0, sizeof(header));
	uint32_t opt = 1;
	ioctlsocket(sock, FIONBIO, &opt);
	DEBUG_PRINT("Accepting and adding all clients.");
	s_accept_and_add_all(sock, &cl_params, &recvtimeout, &sndtimeo, recvbuf);
	DEBUG_PRINT("Finished adding clients.");
	opt = 0;
	ioctlsocket(sock, FIONBIO, &opt);
	status_context status = create_stcontext();
	uint16_t events = POLLWRNORM;
	client* snd = NULL;

	do
	{
		reset_status(&status, RST_ALL);
		snd = s_select_first_avail(&cl_params, &status, &events);
		if (!snd || status.status == NONE_AVAIL)
			break;
		s_prep_welcome(recvbuf, snd->clname, &m_header);
		status = s_send_welcome(&m_header, snd->clsocket, (const void*)recvbuf, status);
		if (status.status)
			s_set_disconnected(snd);
	} while (status.status);

	client* rcpt = NULL;
	if (!(client_cnt && !status.status && snd))
	{
		fprintf(stderr, "\nNo connected clients, shutting down server...");
		s_cleanup(&clients_ht, &clients_arr, client_num);
		return 0;
	}
	uint32_t i = 0;

	while (client_cnt > 0 && status.status != NONE_AVAIL)
	{
		DEBUG_PRINT("Main loop it count: %"PRIu32, i++);
		reset_status(&status, RST_ALL);
		s_remove_disconnected(&cl_params);
		events = POLLRDNORM;
		routing_fptr routing_function = NULL;
		status = conntest(snd->clsocket, MSG_WAIT_TIMEOUT, &events, status);
		status = s_get_header(snd->clsocket, &m_header, status);
		routing_function = s_interpret_header(&m_header, &cl_params, &snd, &rcpt, &status);
		if (routing_function)
			status = routing_function(&m_header, snd->clsocket, rcpt->clsocket, &cl_params, recvbuf, status);
		if (status.status)
			s_handle_error(&cl_params, &m_header, &snd, &rcpt, recvbuf, &status);
		snd = rcpt;
	}

	printf("\nNo available clients left, server shutting down...");
	s_cleanup(&clients_ht, &clients_arr, client_num);
	return 0;
}
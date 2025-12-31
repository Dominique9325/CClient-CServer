#include <stdint.h>
#include <WS2tcpip.h>
#include <stdio.h>
#include "server_setup.h"
#include "shared_utils.h"
#include "client_handling.h"

void s_set_interface(char* s_interface, int8_t s_interface_optnum)
{
	int32_t dev_null;
	SOCKET aux_sock;
	struct sockaddr_in google_dns;
	switch (s_interface_optnum)
	{
	default:
		printf("\nSelected option not found, reverting to default...\n");

	case 1:
		strcpy(s_interface, "127.0.0.1");
		break;

	case 2:
		aux_sock = socket(AF_INET, SOCK_STREAM, 0);
		google_dns.sin_addr.s_addr = inet_addr("8.8.8.8");
		google_dns.sin_family = AF_INET;
		google_dns.sin_port = htons(53);
		connect(aux_sock, (struct sockaddr*)&google_dns, sizeof(google_dns));
		struct sockaddr_in addr;
		uint32_t size = sizeof(addr);
		getsockname(aux_sock, (struct sockaddr*)&addr, &size);
		inet_ntop(AF_INET, (void*)&addr.sin_addr, s_interface, INET_ADDRSTRLEN);
		closesocket(aux_sock);
		break;

	case 3:
		printf("\nEnter the IPv4 interface here: ");
		dev_null = scanf("%21s", s_interface);
		flush_stdin();
		break;

	case 4:
		strcpy(s_interface, "0.0.0.0");
		break;

	}
}


void s_bind_listen(char* s_interface, SOCKET sock, uint16_t port)
{
	struct sockaddr_in saddr;
	saddr.sin_family = AF_INET;
	saddr.sin_port = htons(port);
	saddr.sin_addr.s_addr = inet_addr(s_interface);
	int32_t fd = bind(sock, (struct sockaddr*)&saddr, sizeof(saddr));
	listen(sock, MAX_LISTEN_QUEUELEN);
}

void s_cleanup(client*** htable, client*** cli_arr, uint8_t client_num)
{
	uint8_t i;
	for (i = 0; i < client_num; i++)
	{
		if ((*cli_arr)[i])
		{
			closesocket((*cli_arr)[i]->clsocket);
			free((*cli_arr)[i]);
			(*cli_arr)[i] = NULL;
		}
	}
	free(*cli_arr);
	free(*htable);
	*htable = NULL;
	*cli_arr = NULL;
	WSACleanup();
}
#include <stdio.h>
#include <WinSock2.h>
#include <stdint.h>
#include "client_handling.h"
#include "error_handling.h"
#include "shared_utils.h"
#include "server_utils.h"
#include "routing_ops.h"

static uint8_t g_ordnum = 0;
static uint8_t g_someone_hasdisconnected = 0;

uint64_t hash_name(const char* name)
{
	if (!name)
		return 0;
	uint8_t i;
	uint64_t hash = 0;
	for (i = 0; name[i] != '\0'; i++)
		hash = hash * HCONST + (unsigned char)name[i];
	return hash;
}

client* s_get_client(client** htable, const char* name, uint32_t htsize)
{
	if (!memchr(name, '\0', UNAME_MAXSIZE))
		return NULL;
	client* slot = htable[hash_name(name) % htsize];
	if (!slot)
		return NULL;
	while (slot->next && strcmp(slot->clname, name))
		slot = slot->next;
	if (strcmp(slot->clname, name))
		return NULL;
	return slot;
}

void s_set_disconnected(client* cl)
{
	if (!cl)
	{
		fprintf(stderr, "\n%s error: parameter is null.", __func__);
		return;
	}
	cl->disconnected = 1;
	g_someone_hasdisconnected = 1;
}

void s_ht_addclient(client** htable, uint32_t htsize, SOCKET clsocket, char* clname, client** clients_arr, uint8_t* client_cnt)
{
	uint64_t hash = hash_name(clname);
	client* tmp = (client*)malloc(sizeof(client));
	if (!tmp)
		return;
	strcpy(tmp->clname, clname);
	tmp->clsocket = clsocket;
	tmp->next = NULL;
	tmp->timedout = 0;
	tmp->disconnected = 0;
	tmp->ordinal_num = *client_cnt;
	clients_arr[*client_cnt] = tmp;

	if (!htable[hash % htsize])
		htable[hash % htsize] = tmp;
	else if (!htable[hash % htsize]->next)
		htable[hash % htsize]->next = tmp;
	else
	{
		tmp->next = htable[hash % htsize]->next;
		htable[hash % htsize]->next = tmp;
	}
	(*client_cnt)++;
	g_ordnum++;
}

SOCKET s_accpt_and_recv_init_msg(SOCKET sock, uint32_t* recvtimeout, uint32_t* sndtimeout, char* buf)
{
	WSAPOLLFD s_accpt_conn;
	s_accpt_conn.fd = sock;
	s_accpt_conn.events = POLLRDNORM;
	uint32_t opt = 0;
	uint8_t recv_offset = 0;
	DEBUG_PRINT("Polling server socket for connection.");
	WSAPoll(&s_accpt_conn, 1, ACCEPTWAITTIMEOUT);
	DEBUG_PRINT("Ended polling server socket for connection.");

	if (!(s_accpt_conn.revents & POLLRDNORM))
	{
		DEBUG_PRINT("No new connections left.");
		return INVALID_SOCKET;
	}
		

	SOCKET tmp_socket = accept(sock, NULL, NULL);
	if (tmp_socket == SOCKET_ERROR)
	{
		DEBUG_PRINT("Accept failed.");
		return tmp_socket;
	}
	ioctlsocket(tmp_socket, FIONBIO, &opt);
	int32_t res = set_sock_timeout(sock, *sndtimeout, *recvtimeout);
	if (res < 0)
		DEBUG_PRINT("set_sock_timeout::Error::%"PRId32, -res);
	status_context scon = create_stcontext();
	s_recv_all(tmp_socket, (void*)buf, UNAME_MAXSIZE, &scon);
	if (scon.status)
		DEBUG_PRINT("Non-ok: (status: %s, error: %s, code: %"PRId32")", status_enumval_tostr(scon), common_wserrno_tostr(scon.err), scon.err);
	return tmp_socket;
}

client* s_select_first_avail(cl_param_struct* clparams, status_context* scon, uint16_t* events)
{
	IMM_RET(scon);
	if (scon->status)
		return NULL;

	UNPACK_CL_PARAMS(clparams);
	client* cl = NULL;
	uint8_t i = 0;
	uint16_t event_backup = *events;

	for (; i < *client_cnt; i++)
	{
		cl = clients_arr[i];
		*events = event_backup;
		if (!cl || cl->timedout || cl->disconnected)
		{
			cl = NULL;
			continue;
		}

		reset_status(scon, RST_ERR);
		*scon = conntest(cl->clsocket, 300, events, *scon);

		if (!scon->status && (*events & event_backup))
			break;
		else
			s_set_disconnected(cl);

		cl = NULL;
	}

	scon->status = cl ? scon->status : NONE_AVAIL;
	return cl;
}

void s_accept_and_add_all(SOCKET ssock, cl_param_struct* clparams, uint32_t* recvtimeout, uint32_t* sndtimeo, char* recvbuf)
{
	UNPACK_CL_PARAMS(clparams);
	int32_t itcount = 0;
	while (*client_cnt < client_num)
	{
		DEBUG_PRINT("Iter: %"PRId32", client_cnt: %"PRIu8", client_num: %"PRIu8"", itcount, *client_cnt, client_num);

		printf("\nWaiting for %"PRIu8" more clients...", client_num - *client_cnt);
		SOCKET tmp_socket = s_accpt_and_recv_init_msg(ssock, recvtimeout, sndtimeo, recvbuf);
		if (tmp_socket == INVALID_SOCKET)
		{
			DEBUG_PRINT("INVALID_SOCKET, ended period for connections.");
			printf("\nPeriod for awaiting connections ended, %"PRIu8" / %"PRIu8" clients connected.", *client_cnt, client_num);
			break;
		}
		else if (s_get_client(clients_ht, recvbuf, htsize))
		{
			struct sockaddr_in peer_name;
			int32_t length = sizeof(peer_name);
			getpeername(tmp_socket, (struct sockaddr*)&peer_name, &length);
			printf("\nWarning: A username conflict has occured - Peer %s has attempted to join with the username %s, but it"
				"is already taken, as a result the connection will be refused.", inet_ntoa(peer_name.sin_addr), recvbuf);
			DEBUG_PRINT("Username conflict, %s", recvbuf);
			closesocket(tmp_socket);
			tmp_socket = INVALID_SOCKET;
			continue;
		}
		s_ht_addclient(clients_ht, htsize, tmp_socket, recvbuf, clients_arr, client_cnt);
		DEBUG_PRINT("Adding %s to clients hashtable.", recvbuf);
		printf("\n%s connected.", recvbuf);
	}
}

status_context s_send_welcome(const header* hdr, SOCKET sock, const void* buf, status_context scon)
{
	s_send_all(sock, (const void*)hdr, sizeof(header), &scon);
	IMM_RET(&scon);
	if (scon.status)
		return scon;
	s_send_all(sock, (const void*)buf, ntohl(hdr->size), &scon);
	return scon;
}

void s_prep_welcome(char* buf, char* name, header* m_header)
{
	sprintf(buf, "Welcome %s, you are the first person to join the server, it is your turn to send a message\n", name);
	prepare_header(m_header, 0, "server", name, NULL, (uint32_t)strlen(buf) + 1);
}

uint8_t s_remove_client(client** htable, client** cli_arr, char* name, uint32_t htsize, uint8_t* client_cnt)
{
	int32_t calc_index = -1;

	if (name && htsize > 0 && client_cnt)
	{
		calc_index = hash_name(name) % htsize;
	}
	else
		return BAD_CALL;

	client* del_ptr = NULL;
	client* aux_ptr = htable[calc_index];
	int32_t res = 0;

	if (aux_ptr && !(res = strcmp(aux_ptr->clname, name)))
	{
		closesocket(aux_ptr->clsocket);
		htable[calc_index] = aux_ptr->next;
		del_ptr = aux_ptr;
	}
	else
	{
		while (aux_ptr && aux_ptr->next && (res = strcmp(aux_ptr->next->clname, name)))
			aux_ptr = aux_ptr->next;

		if (!aux_ptr || !aux_ptr->next || res)
			return BAD_CALL;

		del_ptr = aux_ptr->next;
		aux_ptr->next = del_ptr->next;
		closesocket(del_ptr->clsocket);
		aux_ptr = del_ptr;
	}

	uint8_t i = 0;
	for (; i < *client_cnt && cli_arr[i] != aux_ptr; i++);

	if (cli_arr[i] == aux_ptr)
	{
		printf("\n%s has disconnected.", cli_arr[i]->clname);
		cli_arr[i] = NULL;
	}
	else
	{
		fprintf(stderr, "\nAn unexpected error has occured while removing a client.");
		return BAD_CALL;
	}
	free(del_ptr);
	return AOK;
}

void s_remove_disconnected(cl_param_struct* clparams)
{
	if (!g_someone_hasdisconnected)
		return;

	g_someone_hasdisconnected = 0;
	UNPACK_CL_PARAMS(clparams);
	uint8_t removed = 0;
	for (uint8_t i = 0; i < *client_cnt; i++)
	{
		if (clients_arr[i]->disconnected)
		{
			if (!s_remove_client(clients_ht, clients_arr, clients_arr[i]->clname, htsize, client_cnt));
				removed++;
		}
	}
	if (removed && *client_cnt > 1)
		qsort(clients_arr, *client_cnt, sizeof(client*), order_comp);

	*client_cnt -= removed;
}
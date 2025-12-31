#include <stdint.h>
#include "routing_ops.h"
#include "error_handling.h"
#include "shared_utils.h"
#include "client_handling.h"
#include "server_utils.h"

routing_fptr s_interpret_header(header* m_header, cl_param_struct* clparams, client** snd, client** rcpt, status_context* stc)
{
	IMM_RET(stc);
	if (stc->status)
		return NULL;
	UNPACK_CL_PARAMS(clparams);
	*rcpt = isrequest(m_header) ? *snd : s_get_client(clients_ht, m_header->recipient, htsize);
	DEBUG_PRINT("Sender: %s, Recipient: %s", *snd ? (*snd)->clname : "NONE", *rcpt ? (*rcpt)->clname : "NONE");

	if (!(*snd) || !(*rcpt) && !isrequest(m_header))
	{
		stc->status = CL_NOT_FOUND;
		DEBUG_PRINT("Client not found");
		return NULL;
	}
	uint8_t flags = m_header->msg_flags;
	if (rcpt && *rcpt && (*rcpt)->timedout)
	{
		DEBUG_PRINT("Un-timeouted %s", (*rcpt)->clname);
		flush_sock_buf((*rcpt)->clsocket);
		(*rcpt)->timedout = 0;
	}

	if (isrequest(m_header))
	{
		stc->status = is_valid_request(m_header) ? stc->status : MALFORMED_REQUEST;
		DEBUG_PRINT("Is a request.");
		return stc->status == MALFORMED_REQUEST ? NULL : s_handle_request;
	}
	else if (flags & FLG_FILE)
	{
		DEBUG_PRINT("Is a file.");
		stc->status = is_valid_file_message(m_header) ? stc->status : MALFORMED_HEADER;
		rmpath_filename(m_header->file_name);
		return stc->status == MALFORMED_HEADER ? NULL : s_route_file;
	}
	else
	{
		DEBUG_PRINT("Is a text message.");
		stc->status = is_valid_text_message(m_header) ? stc->status : MALFORMED_HEADER;
		return stc->status == MALFORMED_HEADER ? NULL : s_route_message;
	}
}

int32_t s_send_all(SOCKET sock, const void* buf, uint32_t len, status_context* scon)
{
	IMM_RET(scon);
	if (scon->status)
		return 0;

	DEBUG_PRINT("Sending.");
	scon->routing_mode = SEND;
	int32_t bytes_sent = 0;
	uint32_t offset = 0;
	while (offset < len && (bytes_sent = send(sock, (const char*)buf + offset, len - offset, 0)) > 0)
		offset += bytes_sent >= 0 ? bytes_sent : 0;
	if (bytes_sent < 0)
	{
		scon->err = WSAGetLastError();
		scon->status = wserror_to_status(scon->err);
		DEBUG_PRINT("Sending failed: (status: %s, error: %s, code: %"PRId32")", status_enumval_tostr(*scon), common_wserrno_tostr(scon->err), scon->err);
	}
	DEBUG_PRINT("Sent: %"PRIu32, offset);
	return offset;
}

int32_t s_recv_all(SOCKET sock, void* buf, uint32_t len, status_context* scon)
{
	IMM_RET(scon);
	if (scon->status)
		return 0;

	uint32_t itcount = 0;
	DEBUG_PRINT("Receiving.");
	scon->routing_mode = RECV;
	int32_t bytes_received = 0;
	uint32_t offset = 0;
	while (offset < len && (bytes_received = recv(sock, (char*)buf + offset, len - offset, 0)) > 0)
	{
		offset += bytes_received >= 0 ? bytes_received : 0;
		DEBUG_PRINT("Itcount: %"PRIu32, itcount++);
	}

	if (bytes_received < 0)
	{
		scon->err = WSAGetLastError();
		scon->status = wserror_to_status(scon->err);
		DEBUG_PRINT("Receiving failed: (status: %s, error: %s, code: %"PRId32")", status_enumval_tostr(*scon), common_wserrno_tostr(scon->err), scon->err);
	}
	else if (!bytes_received)
	{
		DEBUG_PRINT("Connection closed gracefully.");
		scon->status = CONN_CLOSED_GRACEFULLY;
	}
	DEBUG_PRINT("Received: %"PRIu32, offset);
	return offset;
}

status_context s_get_header(SOCKET snd, header* m_header, status_context status)
{
	IMM_RET(&status);
	if (status.status)
		return status;

	DEBUG_PRINT("Receiving header.");
	int32_t total_received = s_recv_all(snd, (void*)m_header, sizeof(header), &status);
	status = ltho_stcontext(status, INCOMPL_HEADER_RECV, T_MSG);
	DEBUG_PRINT("%s", status_enumval_tostr(status));
	DEBUG_PRINT_HDR(m_header, 'c');

	return status;
}

status_context s_route_file(header* m_header, SOCKET snd, SOCKET rcpt, cl_param_struct* clparams, char* buf, status_context stcon)
{
	IMM_RET(&stcon);
	if (stcon.status)
		return stcon;

	DEBUG_PRINT("Routing file.");
	int32_t bytes_received = 0;
	int32_t bytes_sent = 0;
	uint32_t total_bytes_received = 0;
	uint32_t total_bytes_expected = ntohl(m_header->size);
	bytes_sent = s_send_all(rcpt, (const void*)m_header, sizeof(header), &stcon);

	stcon = ltho_stcontext(stcon, INCOMPL_SEND, T_FILE);
	DEBUG_PRINT("%s", status_enumval_tostr(stcon));
	while (total_bytes_received < total_bytes_expected)
	{
		IMM_RET(&stcon);
		if (stcon.status) break;
		ch_header ctrl_hdr = create_ch_hdr(0, 0);
		ch_header chhdr;
		DEBUG_PRINT_CHHDR(&ctrl_hdr, 'c');
		s_send_all(snd, (const void*)&ctrl_hdr, sizeof(ch_header), &stcon);
		s_recv_all(snd, (void*)&chhdr, sizeof(ch_header), &stcon);
		DEBUG_PRINT_CHHDR(&chhdr, 'c');
		bytes_received = s_recv_all(snd, buf, ntohs(chhdr.size), &stcon);
		stcon = ltho_stcontext(stcon, INCOMPL_RECV, T_FILE);
		total_bytes_received += bytes_received;
		bytes_sent = s_send_all(rcpt, (const void*)&chhdr, sizeof(ch_header), &stcon);
		bytes_sent = s_send_all(rcpt, (const void*)buf, ntohs(chhdr.size), &stcon);
		stcon = ltho_stcontext(stcon, INCOMPL_SEND, T_FILE);
	}
	DEBUG_PRINT("Return: (status: %s, error: %s, code: %"PRId32")", status_enumval_tostr(stcon), common_wserrno_tostr(stcon.err), stcon.err);
	return stcon;
}

status_context s_route_message(header* m_header, SOCKET snd, SOCKET rcpt, cl_param_struct* clparams, char* buf, status_context stcon)
{
	IMM_RET(&stcon);
	if (stcon.status)
		return stcon;

	uint32_t bytes_rcv_or_sent = s_recv_all(snd, buf, ntohl(m_header->size), &stcon);
	stcon = ltho_stcontext(stcon, INCOMPL_RECV, T_MSG);
	DEBUG_PRINT_HDR(m_header, 'c');
	bytes_rcv_or_sent = s_send_all(rcpt, m_header, sizeof(header), &stcon);
	bytes_rcv_or_sent = s_send_all(rcpt, (const void*)buf, ntohl(m_header->size), &stcon);
	stcon = ltho_stcontext(stcon, INCOMPL_SEND, T_MSG);
	DEBUG_PRINT("Return: (status: %s, error: %s, code: %"PRId32")", status_enumval_tostr(stcon), common_wserrno_tostr(stcon.err), stcon.err);
	return stcon;
}

status_context s_handle_request(header* m_header, SOCKET snd, SOCKET rcpt, cl_param_struct* clparams, char* buf, status_context stcon)
{
	IMM_RET(&stcon);
	if (stcon.status)
		return stcon;

	UNPACK_CL_PARAMS(clparams);
	s_recv_all(snd, buf, ntohl(m_header->size), &stcon);
	if (m_header->msg_flags & FLG_LIST)
	{
		DEBUG_PRINT("List request.");
		uint32_t len = s_add_active_names(buf, clparams);
		len = len ? len : 1;
		char sender[32];
		strcpy(sender, m_header->sender);
		prepare_header(m_header, m_header->msg_flags | (len == 1 ? FLG_EMPTY : 0), "server", sender, NULL, len);
	}
	else if (m_header->msg_flags & FLG_DEST_CONN)
	{
		DEBUG_PRINT("Prod request");
		status_context tmp_scon = create_stcontext();
		uint8_t res = 0;
		client* dest = s_get_client(clients_ht, (const char*)buf, htsize);
		if (!dest)
			res = 0;
		else
		{
			uint16_t flags = POLLWRNORM;
			tmp_scon = conntest(dest->clsocket, 200, &flags, tmp_scon);
			res = tmp_scon.status ? 0 : FLG_DEST_CONN;
		}

		buf[0] = '\0';
		char tmp[32];
		strcpy(tmp, m_header->sender);
		prepare_header(m_header, res | FLG_REQ | FLG_EMPTY, "server", tmp, NULL, 1);
	}
	else
	{
		DEBUG_PRINT("Malformed request.");
		stcon.status = MALFORMED_REQUEST;
		return stcon;
	}
	DEBUG_PRINT_HDR(m_header, 'c');
	uint32_t bytes_sent = s_send_all(snd, (const void*)m_header, sizeof(header), &stcon);
	bytes_sent = s_send_all(snd, (const void*)buf, ntohl(m_header->size), &stcon);
	DEBUG_PRINT("Return: (status: %s, error: %s, code: %"PRId32")", status_enumval_tostr(stcon), common_wserrno_tostr(stcon.err), stcon.err);
	return stcon;
}

uint32_t s_add_active_names(char* buf, cl_param_struct* clparams)
{
	UNPACK_CL_PARAMS(clparams);
	int8_t i;
	uint16_t offset = 0;
	for (i = 0; i < *client_cnt; i++)
	{
		client* cl_curr = clients_arr[i];
		if (!cl_curr || cl_curr->disconnected)
			continue;
		SOCKET test_socket = cl_curr->clsocket;
		status_context sc = create_stcontext();
		sc = conntest(test_socket, 50, NULL, sc);

		if (sc.status == CONN_CLOSED || sc.status == CONN_CLOSED_GRACEFULLY)
		{
			DEBUG_PRINT("%s set as disconnected.", cl_curr->clname);
			s_set_disconnected(cl_curr);
			continue;
		}

		char* name = clients_arr[i]->clname;
		memcpy(buf + offset, name, UNAME_MAXSIZE);
		offset += UNAME_MAXSIZE;
	}
	DEBUG_PRINT("Returned: %"PRIu32, offset);
	return offset;
}
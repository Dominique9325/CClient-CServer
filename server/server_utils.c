#include <stdio.h>
#include "server_utils.h"
#include "shared_utils.h"
#include "client_handling.h"
#include "error_handling.h"

void uswap(void** a, void** b)
{
	void* tmp = *a;
	*a = *b;
	*b = tmp;
}

uint8_t is_present(char* buf)
{
	if (!buf || buf[0] == '\0')
		return 0;
	return 1;
}

uint8_t is_valid_request(header* m_header)
{
	uint8_t flags = m_header->msg_flags;
	return flags & FLG_REQ &&
		!(flags & FLG_FILE) &&
		!(flags & FLG_DEST_CONN && flags & FLG_LIST) &&
		!(flags & FLG_DEST_CONN && (flags & FLG_EMPTY || ntohl(m_header->size <= 1)));
}

uint8_t is_valid_file_message(header* m_header)
{
	uint8_t flags = m_header->msg_flags;
	return !(flags & FLG_REQ || flags & FLG_DEST_CONN || flags & FLG_LIST || flags & FLG_EMPTY) &&
		ntohl(m_header->size) > 1 &&
		is_present(m_header->file_name) &&
		memchr(m_header->file_name, '\0', MAX_FILENAME_LEN);
}

uint8_t is_valid_text_message(header* m_header)
{
	uint8_t flags = m_header->msg_flags;
	return !(flags & FLG_REQ || flags & FLG_DEST_CONN || flags & FLG_LIST || flags & FLG_EMPTY) &&
		ntohl(m_header->size > 1);
}

uint8_t isrequest(const header* hdr)
{
	return !strcmp(hdr->recipient, "server") && (hdr->msg_flags & FLG_REQ);
}

int32_t order_comp(const void* a, const void* b)
{
	client* cla = *(client**)a;
	client* clb = *(client**)b;

	if (!cla)
		return 1;
	else if (!clb)
		return -1;

	return cla->ordinal_num - clb->ordinal_num;
}

status_context conntest(SOCKET sock, int32_t timeout, uint16_t* flags, status_context opcontext)
{
	if (sock == INVALID_SOCKET)
	{
		DEBUG_PRINT("Invalid socket passed.");
		opcontext.status = INV_SOCKET;
		return opcontext;
	}
	opcontext.routing_mode = UNDEFINED;
	WSAPOLLFD pollfd;
	pollfd.fd = sock;
	pollfd.events = flags && *flags ? *flags : POLLRDNORM;
	int32_t socket_waspolled = WSAPoll(&pollfd, 1, timeout);
	DEBUG_PRINT("Poll res: %"PRId16, pollfd.revents);
	if (flags)
		*flags = pollfd.revents;
	if (!socket_waspolled)
	{
		opcontext.status = POLL_TIMED_OUT;
		DEBUG_PRINT("Poll timed out.");
		return opcontext;
	}
	if (pollfd.revents & POLLHUP)
		opcontext.status = CONN_CLOSED_GRACEFULLY;
	else if (pollfd.revents & POLLERR)
	{
		opcontext.err = WSAGetLastError();
		if (opcontext.err == WSAECONNRESET)
			opcontext.status = CONN_CLOSED;
		else
			opcontext.status = OTHER_ERR;
	}
	DEBUG_PRINT("Status: %s, Error: %s, Code: %"PRId32"", status_enumval_tostr(opcontext), common_wserrno_tostr(opcontext.err), opcontext.err);
	return opcontext;
}

char* status_enumval_tostr(status_context stcon)
{
	switch (stcon.status)
	{
	case CONN_OK: return "Connection/Message OK";

	case INV_SOCKET: return "Invalid socket";

	case CONN_CLOSED_GRACEFULLY: return "Connection closed gracefully";

	case CONN_CLOSED: return "Connection reset";

	case TIMED_OUT: return "Timed out";

	case POLL_TIMED_OUT: return "Poll timed out";

	case OTHER_ERR: return "Other/unknown error";

	case NONE_AVAIL: return "None available";

	case INCOMPL_RECV: return "Incomplete receive";

	case INCOMPL_HEADER_RECV: return "Incomplete header receive";

	case INCOMPL_SEND: return "Incomplete send";

	case CL_NOT_FOUND: return "Client not found";

	case MALFORMED_HEADER: return "Malformed header";

	case MALFORMED_REQUEST: return "Malformed request";

	case BAD_CALL: return "Invalid function call";

	default: return "Unkown status";
	}
}

char* routingmode_enumval_tostr(status_context stcon)
{
	switch (stcon.routing_mode)
	{
	case UNDEFINED: return "Routing mode undefined";

	case SEND: return "Send";

	case RECV: return "Recv";
		
	default: return "Unknown routing mode";
	}
}

char* packagetype_enumval_tostr(status_context stcon)
{
	return stcon.package_type ? "File" : "Message";
}
#include <stdio.h>
#include "shared_utils.h"

int32_t ws2_init()
{
	WORD w2_version = MAKEWORD(2, 2);
	WSADATA wsa_data;
	return WSAStartup(w2_version, &wsa_data);
}

int32_t send_all(SOCKET sock, const void* buf, int32_t len)
{
	int32_t bytes_sent = 0;
	int32_t offset = 0;
	while (offset < len && (bytes_sent = send(sock, (const char*)buf + offset, len - offset, 0)) > 0)
		offset += bytes_sent;
	return offset >= 0 ? offset : -WSAGetLastError();
}

int32_t recv_all(SOCKET sock, void* buf, int32_t len)
{
	int32_t bytes_received = 0;
	int32_t offset = 0;
	while (offset < len && (bytes_received = recv(sock, (char*)buf + offset, len - offset, 0)) > 0)
		offset += bytes_received;
	return offset >= 0 ? offset : -WSAGetLastError();
}

uint8_t is_txtfile(const char* filename)
{
	return !strcmp(strrchr(filename, '.') + 1, "txt") ? 1 : 0;
}

void prepare_header(header* hdr, uint8_t msgtype, char* sender, char* recipient, char* file_name, uint32_t size)
{
	memset((void*)hdr, 0, sizeof(header));
	hdr->msg_flags = msgtype;
	if (sender)
		strcpy(hdr->sender, (const char*)sender);
	else
		hdr->sender[0] = '\0';

	if (recipient)
		strcpy(hdr->recipient, (const char*)recipient);
	else
		hdr->recipient[0] = '\0';

	if (file_name)
		strcpy(hdr->file_name, (const char*)file_name);
	else
		hdr->file_name[0] = '\0';

	hdr->size = htonl(size);
}

void setblocking(SOCKET sock)
{
	uint32_t arg = 0;
	ioctlsocket(sock, FIONBIO, &arg);
}

int32_t get_header(SOCKET snd, header* m_header)
{
	int32_t total_received = recv_all(snd, (void*)m_header, sizeof(header));
	m_header->size = ntohl(m_header->size);
	return total_received == sizeof(header) ? total_received : -WSAGetLastError();
}

void flush_stdin()
{
	char c;
	while ((c = getchar()) != '\n' && c != EOF);
}

ch_header create_ch_hdr(uint8_t flags, uint16_t size)
{
	ch_header chhdr = { .flags = flags, .size = htons(size) };
	return chhdr;
}

void flush_sock_buf(SOCKET sock)
{
	uint32_t bytes;
	ioctlsocket(sock, FIONREAD, &bytes);
	if (bytes > 0)
	{
		char* buf = (char*)malloc(bytes);
		recv_all(sock, (void*)buf, bytes);
		free(buf);
	}
}

void rmpath_filename(char* filename)
{
	if (!filename)
		return;
	char* finslash = NULL;
	char* finbslash = NULL;
	if (!(finslash = strrchr(filename, '/')) &&
		!(finbslash = strrchr(filename, '\\')))
		return;
	finslash = finslash > finbslash ? finslash : finbslash;
	memmove(filename, finslash + 1, (filename + strlen(filename) - finslash + 1));
}

uint8_t file_exists(const char* filename)
{
	FILE* fp = fopen(filename, "r");
	if (!fp)
		return 0;

	uint8_t exists = fp ? 1 : 0;
	if (fp) fclose(fp);
	return exists;
}

int32_t set_sock_timeout(SOCKET sock, uint32_t send, uint32_t recv)
{
	DEBUG_PRINT("Setting socket timeout to (send: %"PRIu32", recv: %"PRIu32")", send, recv);
	uint32_t sendtimeout = send;
	uint32_t recvtimeout = recv;
	int32_t val = 0;
	val = setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&recvtimeout, sizeof(recvtimeout));
	if (val == SOCKET_ERROR)
	{
		int32_t err = WSAGetLastError();
		DEBUG_PRINT("Error setting recv timeout: (Error: %s, Code: %"PRId32")", common_wserrno_tostr(err), err);
		return -err;
	}
	val = setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&sendtimeout, sizeof(sendtimeout));
	if (val == SOCKET_ERROR)
	{
		int32_t err = WSAGetLastError();
		DEBUG_PRINT("Error setting send timeout: (Error: %s, Code: %"PRId32")", common_wserrno_tostr(err), err);
		return -err;
	}

	return 0;
}

char* common_wserrno_tostr(int32_t wserr)
{
	switch (wserr)
	{
	case 0: return "No error";

	case WSAECONNABORTED: return "Connection closed by local host";

	case WSAECONNRESET: return "Connection closed by remote peer";

	case WSAEWOULDBLOCK: return "Blocking socket timeout";

	case WSAETIMEDOUT: return "Connection timed out";

	default: return "Uncommon error code";
	}
}
#include <WinSock2.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "error_handling.h"
#include "client_handling.h"
#include "shared_utils.h"
#include "routing_ops.h"
#include "server_utils.h"

uint8_t wserror_to_status(int32_t err)
{
	return err == WSAECONNRESET ? CONN_CLOSED : (err == WSAETIMEDOUT ? TIMED_OUT : OTHER_ERR);
}

status_context ltho_stcontext(status_context stcon, uint8_t hstatus, uint8_t pkgtype)
{
	if (stcon.conv_mode)
		return stcon;
	if (stcon.status)
	{
		stcon.status = hstatus;
		stcon.conv_mode = CONV;
		stcon.package_type = pkgtype;
	}
	return stcon;
}

status_context create_stcontext()
{
	status_context scon = { .conv_mode = UNCONV, .err = 0, .package_type = T_MSG, .routing_mode = UNDEFINED, .status = AOK };
	return scon;
}

void reset_status(status_context* scon, uint8_t reset_type)
{
	if (reset_type == RST_ALL)
	{
		*scon = create_stcontext();
		return;
	}
	scon->status = AOK;
	scon->err = 0;
	scon->conv_mode = UNCONV;
	scon->package_type = T_MSG;
}

//Sends a single control chunk header to the recipient socket, telling said client to abolish the file transfer.
//If the recipient is the file recipient, then they will stop expecting any further data, if they're the file sender,
//then they will stop sending any further data.
static void _s_abolish_filetr(SOCKET rcpt, header* hdr, status_context* scon)
{
	ch_header noop_chhdr = create_ch_hdr(FLG_DCON | (is_txtfile(hdr->file_name) ? 0 : FLG_DISCARD_MSG), 1);
	s_send_all(rcpt, (const void*)&noop_chhdr, sizeof(ch_header), scon);
	DEBUG_PRINT("(status: %s, error: %s, code: %"PRId32")", status_enumval_tostr(*scon), common_wserrno_tostr(scon->err), scon->err);
}


//Handling a message or file being directed to a non-existent client.
static void _s_handle_unknownrecipient(header* hdr, client** snd_violator, client** rcpt, char* buf, status_context* scon)
{
	DEBUG_PRINT("Handling unknown recipient.");
	reset_status(scon, RST_ERR);
	printf("\n%s attempted to send a message to an invalid recipient.", (*snd_violator)->clname);
	if (scon->package_type == T_FILE)
	{
		DEBUG_PRINT("Abolishing file transfer.");
		_s_abolish_filetr((*snd_violator)->clsocket, hdr, scon);
	}
	sprintf((char* const)buf, "A user with the name '%s' could not be found, for a list of active users, please use /list.", hdr->recipient);
	prepare_header(hdr, 0, "server", (*snd_violator)->clname, NULL, (uint32_t)strlen(buf) + 1);
	*rcpt = *snd_violator;
	*snd_violator = NULL;
	DEBUG_PRINT("Sender: %s, Recipient %s", CLI_NULLABLE(*rcpt), CLI_NULLABLE(*snd_violator));
}

//Handling a malformed server request (illegal header or request format).
static void _s_handle_malreq(header* hdr, client** snd_violator, client** rcpt, char* buf, status_context* scon)
{
	DEBUG_PRINT("Handling malformed request.");
	reset_status(scon, RST_ERR);
	printf("\n%s issued an illegal request.", (*snd_violator)->clname);
	sprintf((char* const)buf, "The request performed was illegal,"
		"type /help for a list of valid commands and instructions on how to use them."
	);
	prepare_header(hdr, FLG_INV_REQ, "server", (*snd_violator)->clname, NULL, (uint32_t)strlen(buf) + 1);
	*rcpt = *snd_violator;
	*snd_violator = NULL;
	DEBUG_PRINT("Sender: %s, Recipient %s", CLI_NULLABLE(*rcpt), CLI_NULLABLE(*snd_violator));
}

//Handling a message wait timeout for a given client (MAC token relocation).
static void _s_handle_msgwaittimeout(cl_param_struct* clparams, header* hdr, client** snd, client** rcpt, char* buf, status_context* scon)
{
	DEBUG_PRINT("Handling message timeout.");
	reset_status(scon, RST_ERR);
	(*snd)->timedout = 1;
	sprintf((char* const)buf,
		"%s, you have exceeded the time window for sending a message,"
		"if you are reading this and have attempted to send a message,"
		"your message was not received by the recipient.",
		(*snd)->clname);
	prepare_header(hdr, FLG_NO_TOKEN, "server", (*snd)->clname, NULL, (uint32_t)strlen(buf) + 1);
	uint32_t sent = s_send_all((*snd)->clsocket, (const void*)hdr, sizeof(header), scon);
	sent = s_send_all((*snd)->clsocket, (const void*)buf, ntohl(hdr->size), scon);
	if (scon->status)
	{
		DEBUG_PRINT("%s set to disconnected.", (*snd)->clname);
		s_set_disconnected(*snd);
	}
	reset_status(scon, RST_ERR);
	uint16_t events = POLLWRNORM;
	*rcpt = s_select_first_avail(clparams, scon, &events);
	if (scon->status == NONE_AVAIL)
	{
		DEBUG_PRINT("No clients available.");
		return;
	}	
	sprintf((char* const)buf, "Hello %s, due to unforeseen circumstances"
		" it is now your turn to send the message.\n",
		(*rcpt)->clname);
	prepare_header(hdr, 0, "server", (*rcpt)->clname, NULL, (uint32_t)strlen(buf) + 1);
	*snd = NULL;
	DEBUG_PRINT("Sender: %s, Recipient %s", CLI_NULLABLE(*rcpt), CLI_NULLABLE(*snd));
}

//Handling a malformed header (illegal combination of flags or header field values).
static void _s_handle_malhdr(header* hdr, client** snd_violator, client** rcpt, char* buf, status_context* scon)
{
	DEBUG_PRINT("Handling malformed header.");
	reset_status(scon, RST_ERR);
	*rcpt = *snd_violator;
	*snd_violator = NULL;
	sprintf((char* const)buf, "Error: Malformed headers, message has been rejected.");
	prepare_header(hdr, 0, "server", (*rcpt)->clname, NULL, (uint32_t)strlen(buf) + 1);
}

//Handling a partial receive, indicating the sender has disconnected or timed out (less data received than expected
//before connectivity was lost or timeout was reached).
static void _s_handle_partialrecv(header* hdr, client** snd, client** rcpt, char* buf, status_context* scon)
{
	DEBUG_PRINT("Handling partial recv (mode: %s)", routingmode_enumval_tostr(*scon));
	reset_status(scon, RST_ERR);
	s_set_disconnected(*snd);
	if (scon->package_type == T_FILE)
	{
		DEBUG_PRINT("Abolishing file transfer.");
		_s_abolish_filetr((*rcpt)->clsocket, hdr, scon);
	}	

	sprintf((char* const)buf, "%s has disconnected in the middle of sending a message to you."
		"It is now your turn to send a message.\n"
		, (*snd)->clname
	);
	prepare_header(hdr, 0, "server", (*rcpt)->clname, NULL, (uint32_t)strlen(buf) + 1);
	*snd = NULL;
	DEBUG_PRINT("Sender: %s, Recipient %s", CLI_NULLABLE(*rcpt), CLI_NULLABLE(*snd));
}

//Handling a partial send, indicating the recipient has disconnected or timed out (less data sent than expected
//before connectivity was lost or timedout was reached).
static void _s_handle_partialsend(header* hdr, client** snd, client** rcpt, char* buf, status_context* scon)
{
	DEBUG_PRINT("Handling partial send.");
	reset_status(scon, RST_ERR);
	s_set_disconnected(*rcpt);
	DEBUG_PRINT("%s set to disconnected.", (*rcpt)->clname);
	if (scon->package_type == T_FILE)
	{
		DEBUG_PRINT("Abolishing file transfer.");
		_s_abolish_filetr((*snd)->clsocket, hdr, scon);
	}
	sprintf((char* const)buf, "%s has disconnected in the process of receiving your message."
		"It is now your turn to send a message. \n", (*rcpt)->clname);
	*rcpt = *snd;
	*snd = NULL;
	prepare_header(hdr, 0, "server", (*rcpt)->clname, NULL, (uint32_t)strlen(buf) + 1);
	DEBUG_PRINT("Sender: %s, Recipient %s", CLI_NULLABLE(*rcpt), CLI_NULLABLE(*snd));
}

//Handling a partial header receive, indicating that the sender has disconnected or timed out, triggered when less data is received
//than the size of the header.
static void _s_handle_partialhdrrecv(header* hdr, char* buf, cl_param_struct* clparams, status_context* scon, client** rcpt, client** snd)
{
	DEBUG_PRINT("Handling partial header recv.");
	reset_status(scon, RST_ERR);
	s_set_disconnected(*snd);
	DEBUG_PRINT("%s set to disconnected.", (*snd)->clname);
	uint16_t events = POLLWRNORM;
	client* tmp = s_select_first_avail(clparams, scon, &events);
	if (!tmp)
	{
		DEBUG_PRINT("No client available.");
		return;
	}
	*snd = NULL;
	*rcpt = tmp;
	sprintf((char* const)buf, "Hello %s, due to unforeseen circumstances it is now your turn to send a message.", (*rcpt)->clname);
	prepare_header(hdr, 0, "server", (*rcpt)->clname, NULL, (uint32_t)strlen(buf + 1));
	DEBUG_PRINT("Sender: %s, Recipient %s", CLI_NULLABLE(*rcpt), CLI_NULLABLE(*snd));
}

//Handling a disconnection, only related to low-level functions as well as recurring/additional error handling.
static const char* _s_handle_disconnection(cl_param_struct* clparams, header* hdr, client** snd, client** rcpt, char* buf, status_context* scon)
{
	DEBUG_PRINT("Handling disconnection.");
	UNPACK_CL_PARAMS(clparams);
	reset_status(scon, RST_ERR);
	char* dcname;
	if (scon->routing_mode == UNDEFINED)
	{
		DEBUG_PRINT("Routing mode is undefined (means poll).");
		s_set_disconnected(*snd);
		DEBUG_PRINT("%s set to disconnected.", (*snd)->clname);
		dcname = (*snd)->clname;
	}
	else
	{
		s_set_disconnected(*rcpt);
		DEBUG_PRINT("%s set to disconnected.", (*snd)->clname);
		dcname = (*rcpt)->clname;
	}
	uint16_t events = POLLWRNORM;
	*rcpt = s_select_first_avail(clparams, scon, &events);
	if (!(*rcpt))
	{
		DEBUG_PRINT("None available.");
		return dcname;
	}

	sprintf((char* const)buf, "Due to unforeseen circumstances it is now your turn to send a message.");
	prepare_header(hdr, 0, "server", (*rcpt)->clname, NULL, (uint32_t)strlen(buf) + 1);
	return dcname;
}

void s_handle_error(cl_param_struct* clparams, header* hdr, client** snd, client** rcpt, char* buf, status_context* scon)
{
	DEBUG_PRINT("Handling error.");
	UNPACK_CL_PARAMS(clparams);
	if (*snd)
		flush_sock_buf((*snd)->clsocket);
	if (*rcpt)
		flush_sock_buf((*rcpt)->clsocket);
	client* tmp_cl = NULL;
	uint32_t numof_bytes = 0;
	const char* dcname = NULL;
additional_err:
	switch (scon->status)
	{
	case MALFORMED_HEADER:
		printf("\nError: Malformed header from %s's message.", (*snd)->clname);
		_s_handle_malhdr(hdr, snd, rcpt, buf, scon);
		break;

	case MALFORMED_REQUEST:
		printf("\nError: %s sent a malformed request.", (*snd)->clname);
		_s_handle_malreq(hdr, snd, rcpt, buf, scon);
		break;

	case POLL_TIMED_OUT:
		printf("\nError: %s has exceeded the time window for sending a message, relocating MAC token.",
			(*snd)->clname);
		_s_handle_msgwaittimeout(clparams, hdr, snd, rcpt, buf, scon);
		break;

	case INCOMPL_SEND:
		if (*rcpt)
			printf("\nError: %s failed to fully receive the message", (*rcpt)->clname);
		_s_handle_partialsend(hdr, snd, rcpt, buf, scon);
		break;

	case CL_NOT_FOUND:
		printf("\nError: client '%s' not found.", *rcpt ? (*rcpt)->clname : "nil");
		_s_handle_unknownrecipient(hdr, snd, rcpt, buf, scon);
		break;

	case INCOMPL_HEADER_RECV:
		printf("\nError: Headers partially received from %s", (*snd)->clname);
		_s_handle_partialhdrrecv(hdr, buf, clparams, scon, rcpt, snd);
		break;

	case INCOMPL_RECV:
		printf("\nError: %s failed to fully send the message", (*snd)->clname);
		_s_handle_partialrecv(hdr, snd, rcpt, buf, scon);
		break;

	case CONN_CLOSED_GRACEFULLY:
		DEBUG_PRINT("Connection was closed gracefully.");
		reset_status(scon, RST_ERR);
		tmp_cl = *snd;
		ioctlsocket(tmp_cl->clsocket, FIONREAD, &numof_bytes);
		numof_bytes = numof_bytes > BUF_SIZE ? BUF_SIZE : numof_bytes;
		if (numof_bytes > 0)
		{
			s_recv_all((*snd)->clsocket, buf, numof_bytes, scon);
			buf[numof_bytes] = '\0';
			printf("\nRemainder data from %s: %s", tmp_cl->clname, buf);
		}

	case CONN_CLOSED:
		dcname = _s_handle_disconnection(clparams, hdr, snd, rcpt, buf, scon);
		printf("Info: %s disconnected", dcname);
		break;

	case NONE_AVAIL:
		break;

	case OTHER_ERR:

	default:
		DEBUG_PRINT("Unknown error.");
		fprintf(stderr, "\nError: An unknown error without a proper handler has occured. Error code: %d", scon->err);
		break;
	}

	DEBUG_PRINT_HDR(hdr, 'c');
	if (*rcpt)
	{
		s_send_all((*rcpt)->clsocket, (const void*)hdr, sizeof(header), scon);
		s_send_all((*rcpt)->clsocket, (const void*)buf, ntohl(hdr->size), scon);
	}
	if (scon->status && scon->status != NONE_AVAIL)
	{
		DEBUG_PRINT("Additional error.");
		goto additional_err;
	}
}


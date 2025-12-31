#include <stdio.h>
#include "msg_handling.h"
#include "shared_utils.h"
#include "client_utils.h"

afptr cl_interpret_header(SOCKET sock, header* hdr, uint8_t* tokenflag)
{
	*tokenflag = hdr->msg_flags & FLG_NO_TOKEN ? 0 : 1;

	if (hdr->msg_flags & FLG_FILE)
	{
		rmpath_filename(hdr->file_name);
		return recv_file;
	}

	else if (hdr->msg_flags & FLG_REQ)
		return recv_s_resp;

	else if (hdr->msg_flags & FLG_INV_REQ)
	{
		fprintf(stderr, "Error: server returned invalid request error.\n");
		char* tmpbuf = (char*)malloc(BUF_SIZE);

		if (!tmpbuf)
			return NULL;

		recv_all(sock, tmpbuf, hdr->size);

		if (!(hdr->msg_flags & FLG_EMPTY))
			printf("%s\n", tmpbuf);

		free(tmpbuf);
		return NULL;
	}

	return recv_msg;
}

u_clfunc_status recv_file(SOCKET sock, header* hdr, char* buf, active_names** _unused)
{
	(void)_unused;
	char* mode = "wbx";
	char dest_path[MAX_PATH];
	strcpy(dest_path, hdr->file_name);
	DEBUG_PRINT("Prepending destination pathname.");
	if (prep_ffolder_name(dest_path))
		goto toolong;

	while (file_exists(dest_path))
	{
		DEBUG_PRINT("File already exists at path, appending (1).");

		if (app_num_to_filename(dest_path, 1))
		{
			DEBUG_PRINT("Too long pathname");
			toolong:
			fprintf(stderr, "Error: File name is too long.\n");
			return ENONFATAL;
		}
	}

	DEBUG_PRINT("Creating file.");
	FILE* fp = fopen(dest_path, mode);
	if (!fp)
	{
		DEBUG_PRINT("Couldn't create file.");
		fprintf(stderr, "Error: Couldn't create file.\n");
		return ENONFATAL;
	}
	uint32_t total_recv = 0;
	int32_t recved = 0;
	uint8_t snd_conn_active = 1;
	ch_header chhdr = { 0,0 };

	printf("Info: Downloading file %s...\n", hdr->file_name);

	while (total_recv < hdr->size && snd_conn_active)
	{
		if (recv_all(sock, &chhdr, sizeof(ch_header)) < sizeof(ch_header))
		{
			fclose(fp);
			fprintf(stderr, "\nError: Corrupted chunk header.\n");
			return EFATAL;
		}

		DEBUG_PRINT_CHHDR(&chhdr, 'c');

		if (chhdr.flags & FLG_DCON)
		{
			DEBUG_PRINT("DCON flag tripped.");
			fprintf(stderr, "\nError: Remote sender disconnected.\n");
			snd_conn_active = 0;
		}

		if (chhdr.flags & FLG_DISCARD_MSG)
		{
			DEBUG_PRINT("Msg to be discarded.");
			fclose(fp);
			remove(dest_path);
			fprintf(stderr, "\nError: The binary file has been discarded because"
				"the recipient has disconnected, therefore the file is incomplete\n");
			return ENONFATAL;
		}

		int32_t recved = recv_all(sock, buf, ntohs(chhdr.size));
		DEBUG_PRINT("Received: %"PRId32, recved);

		if (recved < 0)
		{
			DEBUG_PRINT("Error has occured: (Code: %"PRId32", Meaning: %s)", recved, common_wserrno_tostr(recved));
			fclose(fp);
			fprintf(stderr, "\nError: Connection likely lost or timed out.\n");
			return EFATAL;
		}

		total_recv += recved;
		fwrite(buf, 1, recved, fp);
		displ_progress_bar( (uint8_t) ( (double)total_recv / hdr->size * 100.0f ));
		printf(" %"PRIu32" / %"PRIu32" B", total_recv, hdr->size);
	}

	u_clfunc_status retval = OK;

	if (total_recv != hdr->size)
	{
		fprintf(stderr, "Error: Download failed.\n");
		retval = ENONFATAL;
	}
	else
		printf("\nInfo: File downloaded successfully.\n");

	fclose(fp);
	return retval;
}

u_clfunc_status recv_msg(SOCKET sock, header* hdr, char* buf, active_names** _unused)
{
	(void)_unused;
	DEBUG_PRINT("Receiving message.");
	int32_t recved = recv_all(sock, buf, hdr->size);
	if (recved < 0)
	{
		fprintf(stderr, "Error: Failed to receive message.\n");
		return EFATAL;
	}
	printf("%s: %s\n", hdr->sender, buf);
	return OK;
}

u_clfunc_status recv_s_resp(SOCKET sock, header* hdr, char* buf, active_names** anames)
{
	DEBUG_PRINT("Receiving response.");
	int32_t recved = recv_all(sock, buf, hdr->size);
	if (recved < (int32_t)hdr->size)
	{
		DEBUG_PRINT("Incompl resp recv.");
		fprintf(stderr, "Error: Failed to receive response from server.\n");
		return EFATAL;
	}

	if (hdr->msg_flags & FLG_DEST_CONN)
		printf("Info: The selected user is connected and reachable.\n");
	else if (hdr->msg_flags & FLG_LIST)
	{
		if (hdr->size % UNAME_MAXSIZE)
		{
			DEBUG_PRINT("Size is not a multiple of UNAME_MAXSIZE(32).");
			fprintf(stderr, "Error: Malformed response body.\n");
			return OK;
		}

		DEBUG_PRINT("Working with active names.");
		copy_anames(anames, buf, hdr->size / UNAME_MAXSIZE);
		print_anames(*anames);
	}
	else
		printf("Info: The selected user is either disconnected or unreachable.\n");

	return OK;
}

u_clfunc_status send_msg(SOCKET sock, header* hdr, char* buf, active_names** _unused)
{
	(void)_unused;
	DEBUG_PRINT("Sending message.");
	DEBUG_PRINT_HDR(hdr, 'c');
	int32_t sent = send_all(sock, hdr, sizeof(header));

	if (sent < sizeof(header))
	{
		DEBUG_PRINT("Incompl hdr send.");
		return EFATAL;
	}

	sent = send_all(sock, buf, ntohl(hdr->size));
	if (sent < (int32_t)ntohl(hdr->size))
	{
		DEBUG_PRINT("Incompl msg send.");
		return EFATAL;
	}

	return OK;
}

u_clfunc_status send_file(SOCKET sock, header* hdr, char* buf, active_names** _unused)
{
	(void)_unused;
	DEBUG_PRINT("Opening file for reading.");
	FILE* fp = fopen(hdr->file_name, "rb");
	if (!fp)
	{
		fprintf(stderr, "Error: File %s couldn't be opened.\n", hdr->file_name);
		return ENONFATAL;
	}

	rmpath_filename(hdr->file_name);
	int32_t sent = send_all(sock, hdr, sizeof(header));
	if (sent < sizeof(header))
	{
		fclose(fp);
		return EFATAL;
	}

	uint32_t data_sent = 0;
	uint32_t total_data = ntohl(hdr->size);
	ch_header ctrl_hdr;
	printf("Info: Sending file %s...\n", hdr->file_name);
	while (data_sent < total_data)
	{
		displ_progress_bar((uint8_t)((double)data_sent / total_data * 100.0f));
		printf("%"PRIu32" / %"PRIu32" B", data_sent, total_data);
		int32_t recved = recv_all(sock, &ctrl_hdr, sizeof(ch_header));
		if (recved < sizeof(ch_header))
		{
			fclose(fp);
			return EFATAL;
		}

		if (ctrl_hdr.flags & FLG_DCON)
		{
			fclose(fp);
			fprintf(stderr, "\nError: Recipient disconnected\n");
			return ENONFATAL;
		}

		uint16_t copied = (uint16_t)fread(buf, 1, BUF_SIZE, fp);
		if (ferror(fp))
		{
			fprintf(stderr, "\nError: Error while reading file.\n");
			fclose(fp);
			return ENONFATAL;
		}

		ch_header chhdr = create_ch_hdr(0, copied);

		int32_t sent = send_all(sock, &chhdr, sizeof(ch_header));
		if (sent < 0)
		{
			fclose(fp);
			return EFATAL;
		}

		sent = send_all(sock, buf, ntohs(chhdr.size));
		if (sent < 0)
		{
			fclose(fp);
			return EFATAL;
		}

		data_sent += sent;
	}

	u_clfunc_status retval = OK;
	if (data_sent == total_data)
	{
		displ_progress_bar(100);
		printf("\nInfo: File transferred successfully.\n");
		printf("%"PRIu32" / %"PRIu32" B", data_sent, total_data);
	}
	else
	{
		fprintf(stderr, "\nError: File transfer failed.\n");
		retval = ENONFATAL;
	}

	fclose(fp);
	return retval;
}
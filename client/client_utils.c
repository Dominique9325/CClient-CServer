#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "client_utils.h"
#include "shared_utils.h"

/*
* Struct meant to hold usernames, primarily usernames of active users.
* 
* Members:
* @names -> Pointer to an array of 32-byte buffers. (each holds a single username)
* 
* @count -> The number of usernames stored in the struct.
*/
typedef struct active_names
{
	char (*names)[UNAME_MAXSIZE];
	uint8_t count;
}active_names;

//Designated destination folder for all incoming files (by default D:\testfolder, may not necessarily be a valid
//path on your system.
static char ffolder[MAX_PATH] = "D:\\testfolder";

void set_file_folder(const char* pathname)
{
	if (!pathname || strlen(pathname) > MAX_PATH)
		return;

	strcpy(ffolder, pathname);
}

void release_anames(active_names** anames)
{
	if (!(*anames))
		return;

	free((*anames)->names);
	free(*anames);
	*anames = NULL;
}

void copy_anames(active_names** anames, const char* buf, uint8_t nnames)
{
	release_anames(anames);

	*anames = (active_names*)malloc(sizeof(active_names));
	if (!(*anames))
		return;

	(*anames)->names = malloc(nnames * UNAME_MAXSIZE);
	(*anames)->count = nnames;

	if (!(*anames)->names)
	{
		free(*anames);
		*anames = NULL;
		return;
	}

	memcpy((*anames)->names, buf, nnames * UNAME_MAXSIZE);
}

void print_anames(const active_names* anames)
{
	if (!anames)
	{
		fprintf(stderr, "Error: The list is empty, to fill it request a new one from"
			"the server (hint: /list renew.)\n");
		return;
	}

	for (uint8_t i = 0; i < anames->count; i++)
		printf("%s\n", anames->names[i]);
}

void displ_progress_bar(uint8_t p_progress)
{
	if (p_progress < 0 || p_progress > 100)
		return;

	char prcntg[5];
	itoa(p_progress, prcntg, 10);
	uint8_t nlen = (uint8_t)strlen(prcntg);
	prcntg[nlen] = '%';

	char bar[104] = "[";
	memset(bar + 1, (int)' ', 101);
	bar[102] = ']';
	bar[103] = '\0';

	memset(bar + 1, (int)'=', p_progress);
	bar[p_progress + 1] = '>';
	memcpy(bar + 49, prcntg, nlen + 1);


	printf("\r%s", bar);
}

uint8_t app_num_to_filename(char* filename, int32_t num)
{
	if (!filename || (strlen(filename) + 14) > MAX_FILENAME_LEN)
		return 1;

	char nstr[11];
	char with_parentheses[13];

	sprintf(with_parentheses, "(%s)", itoa(num, nstr, 10));
	char* dot = strrchr(filename, '.');
	uint8_t lenafterdot = (uint8_t)strlen(dot) + 1;

	if (!dot)
	{
		strcat(filename, with_parentheses);
	}
	else
	{
		memmove(dot + strlen(with_parentheses), dot, lenafterdot);
		memcpy(dot, with_parentheses, strlen(with_parentheses));
	}

	return 0;
}

int64_t file_size(const char* filename)
{
	FILE* fp = fopen(filename, "rb");
	if (!fp)
		return -1;

	int32_t res = _fseeki64(fp, 0LL, SEEK_END);
	if (res)
	{
		fclose(fp);
		return -1;
	}

	int64_t size = _ftelli64(fp);
	fclose(fp);
	return size;
}

uint8_t prep_ffolder_name(char* filename)
{
	if (!filename || (strlen(filename) + strlen(ffolder) + 1) > MAX_PATH)
		return 1;

	uint16_t len = (uint16_t)strlen(ffolder);
	char endingchar = ffolder[len - 1];

	if (endingchar != '\\' && endingchar != '/')
	{
		if (len + 1 >= MAX_PATH)
		{
			fprintf(stderr, "Error: Bad path name format.\n");
			return 1;
		}

		ffolder[len] = '\\';
		ffolder[len + 1] = '\0';
	}

	memmove(filename + strlen(ffolder), filename, strlen(filename) + 1);
	memcpy(filename, ffolder, strlen(ffolder));
	return 0;
}
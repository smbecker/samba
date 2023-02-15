#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <inttypes.h>

#include <popt.h>
#include <netapi.h>

#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#ifndef MIN
#define MIN(a,b) ((a)<(b)?(a):(b))
#endif

#include "common.h"

void popt_common_callback(poptContext con,
			 enum poptCallbackReason reason,
			 const struct poptOption *opt,
			 const char *arg, const void *data)
{
	struct libnetapi_ctx *ctx = NULL;

	libnetapi_getctx(&ctx);

	if (reason == POPT_CALLBACK_REASON_PRE) {
	}

	if (reason == POPT_CALLBACK_REASON_POST) {
	}

	if (!opt) {
		return;
	}
	switch (opt->val) {
		case 'U': {
			char *puser = strdup(arg);
			char *p = NULL;

			if ((p = strchr(puser,'%'))) {
				size_t len;
				*p = 0;
				libnetapi_set_username(ctx, puser);
				libnetapi_set_password(ctx, p+1);
				len = strlen(p+1);
				memset(strchr(arg,'%')+1,'X',len);
			} else {
				libnetapi_set_username(ctx, puser);
			}
			free(puser);
			break;
		}
		case 'd':
			libnetapi_set_debuglevel(ctx, arg);
			break;
		case 'p':
			libnetapi_set_password(ctx, arg);
			break;
		case 'k':
			libnetapi_set_use_kerberos(ctx);
			break;
	}
}

struct poptOption popt_common_netapi_examples[] = {
	{
		.argInfo = POPT_ARG_CALLBACK|POPT_CBFLAG_PRE|POPT_CBFLAG_POST,
		.arg = (void *)popt_common_callback,
	},
	{
		.longName   = "user",
		.shortName  = 'U',
		.argInfo    = POPT_ARG_STRING,
		.val        = 'U',
		.descrip    = "Username used for connection",
		.argDescrip = "USERNAME",
	},
	{
		.longName   = "password",
		.shortName  = 'p',
		.argInfo    = POPT_ARG_STRING,
		.val        = 'p',
		.descrip    = "Password used for connection",
		.argDescrip = "PASSWORD",
	},
	{
		.longName   = "debuglevel",
		.shortName  = 'd',
		.argInfo    = POPT_ARG_STRING,
		.val        = 'd',
		.descrip    = "Debuglevel",
		.argDescrip = "DEBUGLEVEL",
	},
	{
		.longName   = "kerberos",
		.shortName  = 'k',
		.argInfo    = POPT_ARG_NONE,
		.val        = 'k',
		.descrip    = "Use Kerberos",
	},
	POPT_TABLEEND
};

char *netapi_read_file(const char *filename, uint32_t *psize)
{
	int fd;
	FILE *file = NULL;
	char *p = NULL;
	size_t size = 0;
	size_t chunk = 1024;
	size_t maxsize = SIZE_MAX;
	int err;

	fd = open(filename, O_RDONLY);
	if (fd == -1) {
		goto fail;
	}

	file = fdopen(fd, "r");
	if (file == NULL) {
		goto fail;
	}

	while (size < maxsize) {
		size_t newbufsize;
		size_t nread;

		chunk = MIN(chunk, (maxsize - size));

		newbufsize = size + (chunk+1); /* chunk+1 can't overflow */
		if (newbufsize < size) {
			goto fail; /* overflow */
		}

		p = realloc(p, sizeof(char)*newbufsize);
		if (p == NULL) {
			goto fail;
		}

		nread = fread(p+size, 1, chunk, file);
		size += nread;

		if (nread != chunk) {
			break;
		}
	}

	err = ferror(file);
	if (err != 0) {
		goto fail;
	}

	p[size] = '\0';

	if (psize != NULL) {
		*psize = size;
	}
 fail:
	if (file != NULL) {
		fclose(file);
	}
	close(fd);

	return p;
}

int netapi_save_file(const char *fname, void *ppacket, size_t length)
{
	int fd;
	fd = open(fname, O_WRONLY|O_CREAT|O_TRUNC, 0644);
	if (fd == -1) {
		perror(fname);
		return -1;
	}
	if (write(fd, ppacket, length) != length) {
		fprintf(stderr,"Failed to write %s\n", fname);
		close(fd);
		return -1;
	}
	close(fd);
	return 0;
}

#include <stdio.h>
#include <stdlib.h>

#include "pspretty.h"

static void
out_of_memory()
{
	fprintf(stderr, "out of memory\n");
	exit(1);
}

static char *
readall(FILE *ifile)
{
	char   *buffer;
	size_t	bufsize;
	size_t	used;
	size_t	readc;

	used = 0;
	bufsize = 10 * 1024;
	buffer = malloc(bufsize);
	if (!buffer)
		out_of_memory();

	do
	{
		if (bufsize - used < 1024)
		{
			if (bufsize < (1 * 1024 * 1024))
				bufsize += bufsize;
			else
				bufsize += 1 * 1024 * 1024; /* 1MB */

			buffer = realloc(buffer, bufsize);
			if (!buffer)
				out_of_memory();
		}

		readc = fread(buffer + used, 1, bufsize - used, ifile);

		if (ferror(ifile))
		{
			fprintf(stderr, "cannot read\n");
			exit(1);
		}

		used += readc;
	}
	while (readc > 0);

	buffer[used] = '\0';

	return buffer;
}


int
main(int argc, char *argv[])
{
	char   *str;

	str = readall(stdin);
	(void) parser(str, false);

	return 0;
}

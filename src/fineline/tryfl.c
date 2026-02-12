#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include "fineline.h"

int main(int argc, char **argv)
{
	char *line;

	printf("pid=%d\n", getpid());
	while ((line = fineline("Try:")) != NULL) {
		printf("\n\"%s\"\n", line);
		free(line);
	}
	return 0;
}

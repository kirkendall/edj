#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "fineline.h"

int main(int argc, char **argv)
{
	char *prompt, *line;

	prompt = "Try:";
	fprintf(stderr, "pid: %d    promptwidth: %d\r\n", (int)getpid(), fineline_char_column_number(prompt, strlen(prompt)));
	while ((line = fineline("Try:")) != NULL) {
		printf("\n\"%s\"\n", line);
		free(line);
	}
	return 0;
}

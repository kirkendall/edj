#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <locale.h>
#include <unistd.h>
#include "fineline.h"

int main(int argc, char **argv)
{
	char *prompt, *line;

	setlocale(LC_ALL, "");
	prompt = "Try\xe2\x96\xb6";
	fprintf(stderr, "pid: %d    promptwidth: %d   prompt=\"%s\"\r\n", (int)getpid(), fineline_char_column_number(prompt, strlen(prompt)), prompt);
	while ((line = fineline(prompt)) != NULL) {
		printf("\"%s\"\n", line);
		free(line);
	}
	return 0;
}

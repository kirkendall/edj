#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <edj.h>
#include "edjprog.h"

/* This indicates whether we're running something or reading a command line */
static int running;

/* This catches the SIGINT signal, and uses it to abort an ongoing computation
 * by setting the edj_interrupt flag.
 */
static void catchinterrupt(int signo)
{
	if (running) {
		edj_interrupt = 1;
		fprintf(stderr, "Stopping...\n");
	}
}


static char *edjreadline(const char *prompt)
{
	char	*buf;
	size_t	bufsize, buflen;
	int	ch;

	/* Output the prompt */
	edj_user_write(NULL, "prompt", prompt, strlen(prompt));

	/* Read characters up to the next newline.  The tty is in "cooked"
	 * mode, so the operating system itself will allow basic editing.
	 */
	bufsize = buflen = 0;
	buf = NULL;
	while ((ch = getchar()) != EOF && ch != '\n') {
		if (buflen + 1 >= bufsize) {
			bufsize += 100;
			buf = realloc(buf, bufsize);
		}
		buf[buflen++] = (char)ch;
	}

	/* Return the line */
	return buf;
}

void interact(edjcontext_t **contextref, edjcmd_t *initcmds)
{
	char	*expr;
	edjcmd_t *jc;
	edjcmdout_t *result;

	/* Run the initcmds once.  (Not once for each file.) */
	result = edj_cmd_run(initcmds, contextref);
	free(result);

	/* Maybe run a "hint" command too */
	if (edj_is_true(edj_by_key(edj_config, "firsthint"))) {
		jc = edj_cmd_parse_string("hint");
		result = edj_cmd_run(jc, contextref);
		free(result);
	}

	/* If there's a custom REPL function, use it */
	if (edj_plugin_repl(context))
		return;

	/* Catch SIGINT (usually <Ctrl-C>) and use it to stop computation.
	 * If <Ctrl-C> occurs while entering a line, it'll discard the line
	 * and give you a new prompt.
	 */
	signal(SIGINT, catchinterrupt);

	/* Read an expression */
	for (running = 0;
	     (expr = edjreadline("edj:")) != NULL;
	     running = 0) {
		/* Ignore empty lines */
		if (!expr[0]) {
			free(expr);
			continue;
		}

		/* Treat all processing of the line as "running */
		running = 1;

		/* Compile */
		jc = edj_cmd_parse_string(expr);
		free(expr);
		if (jc != EDJ_CMD_ERROR) {
			/* Execute */
			edj_interrupt = 0;
			run(jc, contextref);

			/* Clean up */
			edj_cmd_free(jc);

			/* If the last line was incomplete, then output a
			 * newline.
			 */
			if (edj_print_incomplete_line) {
				putchar('\n');
				edj_print_incomplete_line = 0;
			}
		}
	}

	/* Leave the cursor on the line after the last, unused prompt */
	putchar('\n');
}

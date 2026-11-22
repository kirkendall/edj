#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <edj.h>
#include "rlplugin.h"

extern void run(edjcmd_t *jc, edjcontext_t **refcontext);

/* This indicates whether we're running something or reading a command line */
static int running;

edjcontext_t *readline_context;

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

/* This is called by readline() when it receives a ^C */
static void catchRLinterrupt()
{
	/* Wipe out the partially-entered line */
	rl_free_line_state();
	rl_cleanup_after_signal();
	rl_replace_line("", 0);

	/* Start a new line */
	puts("");
	rl_on_new_line();
	rl_redisplay();
}


/* Return the full pathname of the history file */
static char *historyfile(void)
{
	static char	*buf = NULL;

	/* First time, generate it */
	if (!buf) {
		/* Get the config directory */
		char *dir = edj_file_path(NULL, NULL, NULL);
		if (!dir)
			return ".edj.history";

		/* Append "/history" to it */
		buf = (char *)malloc(strlen(dir) + 9);
		strcpy(buf, dir);
		free(dir);
		strcat(buf, "/history");
	}

	return buf;
}



/* Interactively read a line from stdin.  */
static char *edjreadline(const char *prompt)
{
	char	*expr;
	size_t	i;

	/* Read a line into a dynamic buffer; */
	expr = readline(prompt);
	if (!expr)
		return NULL;
	i = strlen(expr);

	/* Trim trailing whitespace, possibly including a newline */
	for (; i > 0 && isspace(expr[i - 1]); i--) {
	}
	expr[i] = '\0';

	/* Add to history */
	add_history(expr);
	write_history(historyfile());

	/* Return it */
	return expr;
}

/* Built a colorized prompt string.  This is basically the given prompt with
 * a couple of escape sequences added to start and stop the prompt colors,
 * and also a couple of special characters that ReadLine uses to indicate
 * certain parts of the prompt (namely those escape sequences) are zero-width.
 */
static char *edjprompt(const char *prompt)
{
	static char	buf[50];

	sprintf(buf, "%c%s%c%s%c%s%c ",
		RL_PROMPT_START_IGNORE,
		"\033[32;1m",
		RL_PROMPT_END_IGNORE,
		prompt,
		RL_PROMPT_START_IGNORE,
		"\033[m",
		RL_PROMPT_END_IGNORE);

	return buf;
}

static void readline_repl(edjcontext_t *context)
{
	char	*expr;
	edjcmd_t *jc;
	edjcmdout_t *result;

	/* Save the context where the name completion code can find it */
	readline_context = context;

	/* Enable the use of history and name completion while
	 * inputting expressions.
	 */
	using_history();
	read_history(historyfile());
	rl_attempted_completion_function = edj_completion;
	rl_basic_word_break_characters = " \t\n\"\\'$><=;|&{}()[]#%^*+-:,/?~@";

	/* Catch SIGINT (usually <Ctrl-C>) and use it to stop computation.
	 * If <Ctrl-C> occurs while entering a line, it'll discard the line
	 * and give you a new prompt.
	 */
	signal(SIGINT, catchinterrupt);
	rl_catch_signals = 1;
	rl_signal_event_hook = (rl_hook_func_t *)catchRLinterrupt;

	/* Read an expression */
	for (running = 0;
	     (expr = edjreadline(edjprompt("edj:"))) != NULL;
	     running = 0) {
		/* Ignore empty lines */
		if (!expr[0])
			continue;

		/* Treat all processing of the line as "running */
		running = 1;

		/* Compile */
		jc = edj_cmd_parse_string(expr);
		free(expr);
		if (jc != EDJ_CMD_ERROR) {
			/* Execute */
			edj_interrupt = 0;
			result = edj_cmd_run(jc, &context);
			edj_cmd_free(jc);

			/* Report it */
			if (result) {
				if (result->ret == &edj_cmd_break)
					edj_user_printf(NULL, "debug", "RETURNED A \"BREAK\"\n");
				else if (result->ret == &edj_cmd_continue)
					edj_user_printf(NULL, "debug", "RETURNED A \"CONTINUE\"\n");
				else if (result->ret) {
					edj_user_printf(NULL, "debug", "RETURNED A VALUE: ");
					edj_print(result->ret, NULL);
					edj_free(result->ret);
				} else {
					if (result->where) {
						int lineno;
						edjfile_t *jf = edj_file_containing(result->where, &lineno);
						if (jf)
							edj_user_printf(NULL, "error", "%s:%d: ", jf->filename, lineno);
					}
					edj_user_printf(NULL, "error", "%s", result->text);
					edj_user_printf(NULL, "normal", "\n");
				}
				free(result);
			}

			/* If the last line was incomplete, then output a
			 * newline.  The readline() library depends on this.
			 */
			if (edj_print_incomplete_line) {
				putchar('\n');
				edj_print_incomplete_line = 0;
			}
		}
	}

	/* Leave the cursor on the line after the last, unused prompt */
	putchar('\n');

	/* Clean up the history file */
	history_truncate_file(historyfile(), 100);
}

/*----------------------------------------------------------------------------*/

/* This is the init function. */
char *pluginreadline()
{
	edj_t	*section, *settings;

#if 0
	/* Add the default settings.  The values here may be overridden
	 * when the user's preferences are loaded.
	 */
	settings = edj_parse_string(SETTINGS);
	section = edj_by_key(edj_config, "plugin");
	edj_append(section, edj_key("line", settings));
	add_styles();
#endif

	/* Register an alternative line reader */
	edj_plugin_repl_hook("readline", 1, readline_repl);

	/* Success */
	return NULL;
}

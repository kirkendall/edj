#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#undef _XOPEN_SOURCE
#undef __USE_XOPEN
#include <wchar.h>
#include <edj.h>
#include <fineline.h>
#include "pluginline.h"

/* These are defined in the "edj" program, not the library */
extern void edjprog_repl_hook(const char *plugin, int priority, void (*repl)(edjcontext_t *));

/* This plugin uses the "fineline" line input library to implement a fancy
 * command line interface for edj.
 */

/* The default settings */
#define SETTINGS "{"\
	"\"prompt\":\"edj: \","\
	"\"tabstop\":4"\
	"}"

fineline_t *line_fine; /* the main REPL processor */
fineline_t *line_edit_fine; /* an editor processor */

/* Add syntax coloring styles to config.styles, if not there already */
static void add_styles(void)
{
	static struct {
		char *style;	/* style name */
		char *attr;	/* default attributes, in "set" syntax */
	} fine_styles[] = {
		{"comment", "italic cyan"},
		{"command", "bold cyan"},
		{"function", "yellow"},
		{"const", "bold underlined blue"},
		{"var", "bold blue"},
		{"key", "green"},
		{"string", "yellow"},
		{"number", "yellow"},
		{"regex", "yellow"},
		{"symbol", "yellow"},
		{"type", "brown"},
		{"mismatch", "normal on red"},
		{"search", "bold yellow on green"},
		{"hint", "dim green"},
		{"complete", "dim white"},
		{"select", "on blue"},
		{NULL}
	}, *each;

	/* For each syntax style type... */
	for (each = fine_styles; each->style; each++)
		/* Add the style and set its attributes */
		edj_config_parse(edj_config_style(each->style, NULL), each->attr, NULL);
}

/* Output a chunk of fineline text in a given style */
static void line_text(fineline_t *fine, const char *style, const char *text, size_t len)
{
	static FILE *log;
	if (!log)
		log = fopen("linetext.log", "w");
	if (log) {
		fprintf(log, "%10s %.*s\n", style, len, text);
		fflush(log);
	}
	edj_user_write(NULL, style, text, len);
	fflush(stdout);
}

/*----------------------------------------------------------------------------*/

/* Repeatedly read and evaluate lines */
static void line_repl(edjcontext_t *context)
{
	char	*line;
	char	*historyfile;
	edj_t	*setting;
	edjcmdout_t *result;

	/* Get the settings */
	edj_t	*settings = edj_by_expr(edj_config, "plugin.line", NULL, NULL, NULL);

	/* Allocate a TTY version of fineline */
	line_fine = fineline_tty_alloc();
	line_fine->context = (void *)context;

	/* Load the settings */

	/* Load the history */
	setting = edj_by_key(edj_system, "configdir");
	if (setting) {
		historyfile = malloc(strlen(setting->text) + 8);
		strcpy(historyfile, setting->text);
		strcat(historyfile, "history");
	} else {
		historyfile = strdup("history");
	}
	fineline_history_lines(line_fine, 50);
	fineline_history_load(line_fine, historyfile);

	/* Add the completion and syntax coloring hooks */
	line_fine->style_hook = line_syntax;

	/* Make it use a different output function, so style attributes will
	 * come from edj's config instead of fineline's hardcoded styles.
	 */
	line_fine->text = line_text;

	/* Read command lines, one at a time */
	while ((line = fineline_tty(line_fine, "edj:")) != NULL) {
		/* Ignore empty lines */
		if (!line[0]) {
			free(line);
			continue;
		}

		/* Parse it */
		edjcmd_t *jc = edj_cmd_parse_string(line);
		free(line);
		if (jc == EDJ_CMD_ERROR)
			/* Error message already output */
			continue;

		/* Run it */
		edj_interrupt = 0;
		result = edj_cmd_run(jc, &context);
		edj_cmd_free(jc);

		/* Report it. */
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

		/* If there's another fineline_t then let it run */
		while (line_edit_fine) {
			if (fineline_tty(line_edit_fine, "  1|")) {
			}
		}
	}

	/* Save the history */
	fineline_history_save(line_fine, historyfile);
}

/*----------------------------------------------------------------------------*/

/* This is the init function. */
char *pluginline()
{
	edj_t	*section, *settings;

	/* Add the default settings.  The values here may be overridden
	 * when the user's preferences are loaded.
	 */
	settings = edj_parse_string(SETTINGS);
	section = edj_by_key(edj_config, "plugin");
	edj_append(section, edj_key("line", settings));
	add_styles();

	/* Add new commands */
	line_add_commands();

	/* Register an alternative line reader */
	edj_plugin_repl_hook("line", 1, line_repl);

	/* Success */
	return NULL;
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <edj.h>
#include "edjprog.h"
#include "version.h"

/* This file implements commands.  Thesearen't in the -ledj library because
 * they're closely tied to the functionality of the "edj" program.
 */

/******************************************************************************/
static edjcmd_t    *version_parse(edjsrc_t *src, edjcmdout_t **referr);
static edjcmdout_t *version_run(edjcmd_t *cmd, edjcontext_t **refcontext);
static edjcmdname_t *version_jcn;

/* Parser for the "version" command -- easy since it takes no parameters */
static edjcmd_t    *version_parse(edjsrc_t *src, edjcmdout_t **referr)
{
	return edj_cmd(src, version_jcn);
}

/* Runner for the "version" command */
static edjcmdout_t *version_run(edjcmd_t *cmd, edjcontext_t **refcontext)
{
	edjformat_t tweaked;

	/* Share the version and copyright */
	tweaked = edj_format_default;
	tweaked.fp = NULL;
	edj_user_printf(&tweaked, "normal", "edj %s copyright 2027 by Steve Kirkendall.  Freely redistributable\n", EDJ_VERSION);
	edj_user_printf(&tweaked, "normal", "under the terms of the GNU General Public License version 3.0 or later.\n");
	return NULL;
}


/******************************************************************************/
static edjcmd_t    *hint_parse(edjsrc_t *src, edjcmdout_t **referr);
static edjcmdout_t *hint_run(edjcmd_t *cmd, edjcontext_t **refcontext);
static edjcmdname_t *hint_jcn;

/* Parser for the "hint" command -- easy since it takes no parameters */
static edjcmd_t    *hint_parse(edjsrc_t *src, edjcmdout_t **referr)
{
	return edj_cmd(src, hint_jcn);
}

/* Runner for the "hint" command */
static edjcmdout_t *hint_run(edjcmd_t *cmd, edjcontext_t **refcontext)
{
	edjformat_t tweaked;
	int	help, line, xml, curl;
	edj_t	*scan, *name;

	/* Detect whether popular plugins are loaded */
	help = line = xml = curl = 0;
	for (scan = edj_plugins->first; scan; scan = scan->next) { /* undeferred */
		name = edj_by_key(scan, "name");
		help |= !strcmp(name->text, "help");
		line |= !strcmp(name->text, "line");
		xml |= !strcmp(name->text, "xml");
		curl |= !strcmp(name->text, "curl");
	}

	/* Share the hints */
	tweaked = edj_format_default;
	tweaked.fp = NULL;
	edj_user_printf(&tweaked, "normal", "edj is a JSON tool with a query language that resembles JavaScript and SQL.\n");
	edj_user_printf(&tweaked, "normal", "\nSome good commands: %s%s%s\n", help ? "help, " : "", "set, explain, file, function, plugin, import", line ? ", edit":"");
	edj_user_printf(&tweaked, "normal", "Some good functions: %s%s%s\n", "common(), diff(), gap(), grep(), find()", xml ? ", toXML()" : "", curl ? ", curlGet()":"");
	edj_user_printf(&tweaked, "normal", "Some good operators: %s\n", "in, like, select, values, #, #=, @");
	edj_user_printf(&tweaked, "normal", "%s exits. To disable this message, invoke with %s\n", "<Ctrl-D>", "-Snofirsthint");
	if (help && line)
		edj_user_printf(&tweaked, "normal", "  Run \"%s\" for more keystrokes.", "help fineline");
	edj_user_ch('\n');
	return NULL;
}


/******************************************************************************/
static edjcmd_t    *getopt_parse(edjsrc_t *src, edjcmdout_t **referr);
static edjcmdout_t *getopt_run(edjcmd_t *cmd, edjcontext_t **refcontext);
static edjcmdname_t *getopt_jcn;

/* Parse a flag and hints, and add them to a list */
static edjcmd_t    *getopt_parse(edjsrc_t *src, edjcmdout_t **referr)
{
	size_t	len;
	char	*tmp;

	/* Skip whitespace */
	edj_cmd_parse_whitespace(src);

	/* Find the end of the command */
	for (len = 0; len < src->size && !strchr("\n;}", src->str[len]); len++){
	}

	/* Make a temporary copy of it, so we can stick "\0" on the end */
	tmp = malloc(len + 1);
	strncpy(tmp, src->str, len);
	tmp[len] = '\0';

	/* Process it */
	edjprog_getopt_def(tmp);

	/* Clean up */
	free(tmp);
	src->str += len;
	if (len < src->size)
		src->str++;

	/* No run-time processing is needed */
	return NULL;
}

/* Nothing to do at run time */
static edjcmdout_t *getopt_run(edjcmd_t *cmd, edjcontext_t **refcontext)
{
	return NULL;
}

/******************************************************************************/
/* Register the above commands with the library's command parser. */
void edjcmds(void)
{
	version_jcn = edj_cmd_hook("edj", "version", version_parse, version_run);
	hint_jcn = edj_cmd_hook("edj", "hint", hint_parse, hint_run);
	getopt_jcn = edj_cmd_hook("edj", "getopt", getopt_parse, getopt_run);
}

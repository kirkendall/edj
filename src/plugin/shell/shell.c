#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#undef _XOPEN_SOURCE
#undef __USE_XOPEN
#include <wchar.h>
#include <edj.h>

/* This plugin allows you to run other commands */

/*----------------------------------------------------------------------------*/

/* Run an external program and read its output.  */
static edj_t *jfn_readShell(edj_t *args, void *agdata)
{
	FILE *fp;
	char	*buf;
	size_t	size, used, chunk;
	edj_t	*result;

	/* We expect a command string as the only argument */
	if (args->first->type != EDJ_STRING || args->first->next)
		return edj_error_null(NULL, "shellarg:The %s function uses a single string as its argument", "readShell");

	/* Run the command, read its output */
	fp = popen(args->first->text, "r");
	if (!fp)
		return edj_error_null(NULL, "shellpipe:Could not open pipe for %s()", "readShell");
	size = 1024;
	buf = (char *)malloc(size);
	used = 0;
	while ((chunk = fread(buf + used, 1, size - used, fp)) > 0) {
		used += chunk;
		if (used + 100 > size) {
			size  += 1024;
			buf = (char *)realloc(buf, size);
		}
	}
	pclose(fp);

	/* Strip trailing newlines */
	while (used > 1 && (buf[used - 1] == '\n' || buf[used -1] == '\r'))
		used--;

	/* Store it in a string */
	result = edj_string(buf, used);

	/* Clean up and return the string */
	free(buf);
	return result;
}

static edj_t *jfn_getcwd(edj_t *args, void *agdata)
{
	char	buf[4096];
	if (!getcwd(buf, sizeof buf)) {
		if (errno == ERANGE || errno == ENAMETOOLONG)
			return edj_error_null(NULL, "longcwd:Path is too long");
		else
			return edj_error_null(NULL, "badcwd:Could not fetch working directory");
	}
	return edj_string(buf, -1);
}

/*----------------------------------------------------------------------------*/

/* This is a simple "shell" command, for running an external program */

static edjcmdname_t *jcn_shell;
static edjcmdname_t *jcn_cd;
static edjcmdname_t *jcn_pwd;


/* Parse a shell command.  This is UNQUOTED text. */
static edjcmd_t *shell_parse(edjsrc_t *src, edjcmdout_t **referr)
{
	char	*text;
	size_t	len;
	edjcmd_t *cmd;

	/* We'll use either an expression in parentheses, or literal text. */
	text = NULL;
	edj_cmd_parse_whitespace(src);

	/* collect chars up to the end of the command */
	for (len = 0; &src->str[len] < &src->buf[src->size] && !strchr("\n;{}", src->str[len]); len++) {
	}
	text = (char *)malloc(len + 1);
	strncpy(text, src->str, len);
	text[len] = '\0';
	src->str += len;
	if (*src->str == ';')
		src->str++;

	/* Build a command containing the text */
	cmd = edj_cmd(src, jcn_shell);
	cmd->key = text;
	return cmd;
}

/* Run a "shell" command. */
static edjcmdout_t *shell_run(edjcmd_t *cmd, edjcontext_t **refcontext)
{
	char	buf[100];
	size_t	chunk;
	FILE	*fp;

	/* Run the command and read its output.  Copy the output to the user */
	fp = popen(cmd->key, "r");
	if (!fp)
		return edj_cmd_error(cmd->where, "shellpipe:Could not open pipe for %s()", "readShell");
	while ((chunk = fread(buf, 1, sizeof buf, fp)) > 0)
		edj_user_printf(NULL, "result", "%.*s", (int)chunk, buf);
	pclose(fp);

	/* Return NULL to continue to next command */
	return NULL;
}

/* Parse a cd command.  This is UNQUOTED text. */
static edjcmd_t *cd_parse(edjsrc_t *src, edjcmdout_t **referr)
{
	char	*text;
	size_t	len;
	edjcmd_t *cmd;

	/* We'll use either an expression in parentheses, or literal text. */
	text = NULL;
	edj_cmd_parse_whitespace(src);

	/* collect chars up to the end of the command */
	for (len = 0; &src->str[len] < &src->buf[src->size] && !strchr("\n;{}", src->str[len]); len++) {
	}
	text = (char *)malloc(len + 1);
	strncpy(text, src->str, len);
	text[len] = '\0';
	src->str += len;
	if (*src->str == ';')
		src->str++;

	/* Build a command containing the text */
	cmd = edj_cmd(src, jcn_cd);
	cmd->key = text;
	return cmd;
}

/* Run a "cd" command. */
static edjcmdout_t *cd_run(edjcmd_t *cmd, edjcontext_t **refcontext)
{
	char	buf[100];
	size_t	chunk;
	FILE	*fp;

	/* Run the command and read its output.  Copy the output to the user */
	if (chdir(cmd->key) < 0)
		return edj_cmd_error(cmd->where, "cd:Could not switch to directory %s", cmd->key);

	/* Return NULL to continue to next command */
	return NULL;
}

/* Parse a pwd command, no arguments */
static edjcmd_t *pwd_parse(edjsrc_t *src, edjcmdout_t **referr)
{
	edjcmd_t *cmd = edj_cmd(src, jcn_pwd);

	/* No arguments or other components, but we still need to skip ";" */
	edj_cmd_parse_whitespace(src);
	if (*src->str == ';')
		src->str++;
	return cmd;
}

/* Run a "pwd" command. */
static edjcmdout_t *pwd_run(edjcmd_t *cmd, edjcontext_t **refcontext)
{
	char buf[4096];
	if (!getcwd(buf, sizeof buf))
		return edj_cmd_error(cmd->where, "badcwd:Could not fetch working directory");
	edj_user_printf(NULL, "result", "%s\n", buf);
	return NULL;
}


/*----------------------------------------------------------------------------*/

/* This is the init function.  It registers all of the above functions and
 * commands, and adjusts the settings.
 */
char *pluginshell()
{
	edj_t	*section, *settings;

	/* Register the functions */
	edj_calc_function_hook("readShell",  "cmdline:string", "string", jfn_readShell);
	edj_calc_function_hook("getcwd",  "", "string", jfn_getcwd);

	/* Register the commands.  The first arg is the plugin name. */
	jcn_shell = edj_cmd_hook("shell", "shell", shell_parse, shell_run);
	jcn_cd = edj_cmd_hook("shell", "cd", cd_parse, cd_run);
	jcn_pwd = edj_cmd_hook("shell", "pwd", pwd_parse, pwd_run);

	/* Success */
	return NULL;
}

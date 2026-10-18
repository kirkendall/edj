/* src/plugin/line/command.c
 *
 * This file implements a few new commands.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <fcntl.h>
#include <unistd.h>
#include <edj.h>
#include <fineline.h>
#include <ctype.h>
#include "pluginline.h"

/******************************************************************************/
/* These implement the "edit" command */

/* Identifies the "edit" command.  The parser adds this to its result */
static edjcmdname_t *jcn_edit;

/* Stores the argument of the previous invocation, which is used when "edit"
 * is invoked without any arguments.
 */
static char *edit_previous;

/* Parse the "edit" command's arguments.  Multiple ways to invoke it:
 *   edit	Repeat previous edit
 *   edit word	Edit a function or file
 *   edit (expr)Evaluate expr and use the result as a function or file name
 */
static edjcmd_t *edit_parse(edjsrc_t *src, edjcmdout_t **referr)
{
	const char	*end, *err;
	edjcmd_t *cmd;

	/* Allocate a cmd */
	cmd = edj_cmd(src, jcn_edit);

	/* Check the arguments */
	edj_cmd_parse_whitespace(src);
	if (!*src->str || *src->str == ';' || *src->str == '}') {
		/* "edit" with no arguments */
	} else if (*src->str == '(') {
		/* Get the expression */
		char *str = edj_cmd_parse_paren(src);
		if (!str) {
			*referr = edj_cmd_error(src->str, "Missing ) in \"%s\" expression", "edit");
			return cmd;
		}

		/* Parse the expression */
		cmd->calc = edj_calc_parse(str, &end, &err, 0);
		if (err || *end || !cmd->calc) {
			free(str);
			if (err)
				*referr = edj_cmd_error(src->str, "%s", err);
			else
				*referr = edj_cmd_error(src->str, "Syntax error in \"%s\" expression", "edit");
			return cmd;
		}
		free(str);
	} else {
		/* Assume it is a file name or function name.  Find the end of
		 * it, and copy it into the cmd->key.
		 */
		for (end = src->str; *end && *end != ';' && *end != '}'; end++){
		}
		while (end > src->str && end[-1] == ' ')
			end--;
		cmd->key = malloc(end - src->str + 1);
		strncpy(cmd->key, src->str, end - src->str);
		cmd->key[end - src->str] = '\0';

		/* Move past the end of the name */
		src->str = end;
		edj_cmd_parse_whitespace(src);
	}

	/* Move past the ';', if there is one */
	if (*src->str == ';')
		src->str++;
	edj_cmd_parse_whitespace(src);

	/* Return the command */
	return cmd;
}

/* This is a helper function for edit_run(), which invokes the fineline editor */
static edjcmdout_t *edit_file(const char *where, const char *filename, int line)
{
	edjfile_t *f;
	fineline_t *fed;

	/* If it doesn't exist, then create an empty file */
	int fd = open(filename, O_RDWR|O_CREAT|O_EXCL, 0666);
	if (fd >= 0) {
		write(fd, "\n", 1);
		close(fd);
	}

	/* Load the file */
	f = edj_file_load(filename);
	if (!f)
		return edj_cmd_error(where, "editLoad:Can't load file \"%s\"", filename);

	/* Save it */

	/* Import it */
}


static edjcmdout_t *edit_run(edjcmd_t *cmd, edjcontext_t **refcontext)
{
	edjfunc_t *func;
	edjcmdout_t *result;

	/* Determine what type of "file" invocation this is.  Store the file
	 * or function name in "previous".
	 */
	if (cmd->calc) {
		/* "file (calc) -- Evaluate the expression. */
		edj_t *result = edj_calc(cmd->calc, *refcontext, NULL);

		/* If we got an error, then return the error */
		if (edj_is_error(result)) {
			edjcmdout_t *err = edj_cmd_error(cmd->where, "%s", result->text);
			edj_free(result);
			return err;
		}

		if (result->type == EDJ_STRING) {
			if (edit_previous)
				free(edit_previous);
			edit_previous = strdup(result->text);
			edj_free(result);
		} else {
			edj_free(result);
			return edj_cmd_error(cmd->where, "editExpr:%s expressions should return a string.", "edit");
		}
	} else if (!cmd->key) {
		/* "edit" with no args -- reuse previous */
		if (!edit_previous)
			return edj_cmd_error(cmd->where, "editPrevious:No previous %s value to reuse.", "edit");
	} else {
		/* "edit file name or "function name" */
			if (edit_previous)
				free(edit_previous);
			edit_previous = strdup(cmd->key);
	}

	/* At this point, edit_previous is either a file name or a function
	 * name.  Try function names first, then the filename as given, and
	 * lastly a filename with a ".edj" suffix appended.
	 */
	if ((func = edj_calc_function_by_name(edit_previous)) != NULL) {
		/* !!! Here we want to check whether it comes from a file.
		 * If so, then we want to edit the file, with the cursor
		 * starting at the file's definition.  If not, then we want
		 * to copy the function's source code into an buffer, edit
		 * that, and reparse it.  BUT I NEED TO MODIFY edjfunc_t FIRST!
		 */
		result = edj_cmd_error(cmd->where, "editFunc:Not implemented yet");
	} else if (access(edit_previous, F_OK) >= 0) {
		/* Function name as given */
		result = edit_file(cmd->where, edit_previous, 1);
	} else {
		char *dotedj = malloc(strlen(edit_previous) + 4);
		strcpy(dotedj, edit_previous);
		strcat(dotedj, ".edj");
		result = edit_file(cmd->where, dotedj, 1); /* even if doesn't exist yet */
		free(dotedj);
	}

	/* Return the result of parsing the edited text */
	return result;
}


/******************************************************************************/
/* This registers the commands with the edj library */
void line_add_commands(void)
{
	jcn_edit = edj_cmd_hook("line", "edit", edit_parse, edit_run);
}

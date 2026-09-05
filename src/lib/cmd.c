#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <ctype.h>
#include <string.h>
#include <locale.h>
#include <errno.h>
#include <regex.h>
#include <assert.h>
#include <edj.h>

/* This handles commands.  Each script is a series of commands, so this is
 * pretty central.  While expressions use a decent LALR parser with operator
 * precedence (implemented in calcparse.c), commands use a simple recursive
 * descent parser.  This is because recursive descent parsers are more modular
 * and therefor easier to extend.  A plugin can register a new command by
 * calling edj_cmd_hook() with the name, and pointers to the argument parsing
 * function and run function.
 *
 * A command's parsers can use the edj_cmd_parse_whitespace(),
 * edj_cmd_parse_key(), edj_cmd_parse_paren(), edj_cmd_parse_curly()
 * functions.  Also edj_calc_parse() of course.
 */

/* This array doesn't actually store anything; it just provides a distinct
 * value that can be used to recognize when edj_cmd_parse() and
 * edj_cmd_parse_string() detect an error.
 */
edjcmd_t EDJ_CMD_ERROR[1];

/* Forward declarations for functions that implement the built-in commands */
static edjcmd_t    *if_parse(edjsrc_t *src, edjcmdout_t **referr);
static edjcmdout_t *if_run(edjcmd_t *cmd, edjcontext_t **refcontext);
static edjcmd_t    *while_parse(edjsrc_t *src, edjcmdout_t **referr);
static edjcmdout_t *while_run(edjcmd_t *cmd, edjcontext_t **refcontext);
static edjcmd_t    *for_parse(edjsrc_t *src, edjcmdout_t **referr);
static edjcmdout_t *for_run(edjcmd_t *cmd, edjcontext_t **refcontext);
static edjcmd_t    *break_parse(edjsrc_t *src, edjcmdout_t **referr);
static edjcmdout_t *break_run(edjcmd_t *cmd, edjcontext_t **refcontext);
static edjcmd_t    *continue_parse(edjsrc_t *src, edjcmdout_t **referr);
static edjcmdout_t *continue_run(edjcmd_t *cmd, edjcontext_t **refcontext);
static edjcmd_t    *switch_parse(edjsrc_t *src, edjcmdout_t **referr);
static edjcmdout_t *switch_run(edjcmd_t *cmd, edjcontext_t **refcontext);
static edjcmd_t    *case_parse(edjsrc_t *src, edjcmdout_t **referr);
static edjcmdout_t *case_run(edjcmd_t *cmd, edjcontext_t **refcontext);
static edjcmd_t    *default_parse(edjsrc_t *src, edjcmdout_t **referr);
static edjcmdout_t *default_run(edjcmd_t *cmd, edjcontext_t **refcontext);
static edjcmd_t    *try_parse(edjsrc_t *src, edjcmdout_t **referr);
static edjcmdout_t *try_run(edjcmd_t *cmd, edjcontext_t **refcontext);
static edjcmd_t    *throw_parse(edjsrc_t *src, edjcmdout_t **referr);
static edjcmdout_t *throw_run(edjcmd_t *cmd, edjcontext_t **refcontext);
static edjcmd_t    *var_parse(edjsrc_t *src, edjcmdout_t **referr);
static edjcmdout_t *var_run(edjcmd_t *cmd, edjcontext_t **refcontext);
static edjcmd_t    *const_parse(edjsrc_t *src, edjcmdout_t **referr);
static edjcmdout_t *const_run(edjcmd_t *cmd, edjcontext_t **refcontext);
static edjcmd_t    *function_parse(edjsrc_t *src, edjcmdout_t **referr);
static edjcmdout_t *function_run(edjcmd_t *cmd, edjcontext_t **refcontext);
static edjcmd_t    *return_parse(edjsrc_t *src, edjcmdout_t **referr);
static edjcmdout_t *return_run(edjcmd_t *cmd, edjcontext_t **refcontext);
static edjcmd_t    *void_parse(edjsrc_t *src, edjcmdout_t **referr);
static edjcmdout_t *void_run(edjcmd_t *cmd, edjcontext_t **refcontext);
static edjcmd_t    *explain_parse(edjsrc_t *src, edjcmdout_t **referr);
static edjcmdout_t *explain_run(edjcmd_t *cmd, edjcontext_t **refcontext);
static edjcmd_t    *file_parse(edjsrc_t *src, edjcmdout_t **referr);
static edjcmdout_t *file_run(edjcmd_t *cmd, edjcontext_t **refcontext);
static edjcmd_t    *import_parse(edjsrc_t *src, edjcmdout_t **referr);
static edjcmdout_t *import_run(edjcmd_t *cmd, edjcontext_t **refcontext);
static edjcmd_t    *plugin_parse(edjsrc_t *src, edjcmdout_t **referr);
static edjcmdout_t *plugin_run(edjcmd_t *cmd, edjcontext_t **refcontext);
static edjcmd_t    *print_parse(edjsrc_t *src, edjcmdout_t **referr);
static edjcmdout_t *print_run(edjcmd_t *cmd, edjcontext_t **refcontext);
static edjcmd_t    *set_parse(edjsrc_t *src, edjcmdout_t **referr);
static edjcmdout_t *set_run(edjcmd_t *cmd, edjcontext_t **refcontext);
static edjcmd_t    *delete_parse(edjsrc_t *src, edjcmdout_t **referr);
static edjcmdout_t *delete_run(edjcmd_t *cmd, edjcontext_t **refcontext);
static edjcmdout_t *calc_run(edjcmd_t *cmd, edjcontext_t **refcontext);

/* Linked list of command names */
static edjcmdname_t jcn_if =       {NULL,	"if",		if_parse,	if_run};
static edjcmdname_t jcn_while =    {&jcn_if,	"while",	while_parse,	while_run};
static edjcmdname_t jcn_for =      {&jcn_while,	"for",		for_parse,	for_run};
static edjcmdname_t jcn_break =    {&jcn_for,	"break",	break_parse,	break_run};
static edjcmdname_t jcn_continue = {&jcn_break,	"continue",	continue_parse,	continue_run};
static edjcmdname_t jcn_switch =   {&jcn_continue,"switch",	switch_parse,	switch_run};
static edjcmdname_t jcn_case =     {&jcn_switch,	"case",		case_parse,	case_run};
static edjcmdname_t jcn_default =  {&jcn_case,	"default",	default_parse,	default_run};
static edjcmdname_t jcn_try =      {&jcn_default,"try",		try_parse,	try_run};
static edjcmdname_t jcn_throw =    {&jcn_try,	"throw",	throw_parse,	throw_run};
static edjcmdname_t jcn_var =      {&jcn_throw,	"var",		var_parse,	var_run};
static edjcmdname_t jcn_const =    {&jcn_var,	"const",	const_parse,	const_run};
static edjcmdname_t jcn_function = {&jcn_const,	"function",	function_parse,	function_run};
static edjcmdname_t jcn_return =   {&jcn_function,"return",	return_parse,	return_run};
static edjcmdname_t jcn_void =     {&jcn_return,	"void",		void_parse,	void_run};
static edjcmdname_t jcn_explain =  {&jcn_void,	"explain",	explain_parse,	explain_run};
static edjcmdname_t jcn_file =     {&jcn_explain,"file",		file_parse,	file_run};
static edjcmdname_t jcn_import =   {&jcn_file,	"import",	import_parse,	import_run};
static edjcmdname_t jcn_plugin =   {&jcn_import,	"plugin",	plugin_parse,	plugin_run};
static edjcmdname_t jcn_print =    {&jcn_plugin,	"print",	print_parse,	print_run};
static edjcmdname_t jcn_set =	  {&jcn_print,	"set",		set_parse,	set_run};
static edjcmdname_t jcn_delete =	  {&jcn_set,	"delete",	delete_parse,	delete_run};
static edjcmdname_t *names = &jcn_delete;

/* A command name struct for assignment/output.  This isn't part of the "names"
 * list because assignment/output has no name -- you just give the expression.
 */
static edjcmdname_t jcn_calc = {NULL, "<<calc>>", NULL, calc_run};

/* These are used to indicate special results from a series of commands.
 * Their values are irrelevant; their unique addresses are what matters.
 */
edj_t edj_cmd_break;		/* "break" statement */
edj_t edj_cmd_continue;	/* "continue" statement */
edj_t edj_cmd_case_mismatch;	/* "case" that doesn't match switchcase */

/* Add a new statement name, and its argument parser and runner. */
edjcmdname_t *edj_cmd_hook(char *pluginname, char *cmdname, edjcmd_t *(*argparser)(edjsrc_t *src, edjcmdout_t **referr), edjcmdout_t *(*run)(edjcmd_t *cmd, edjcontext_t **refcontext))
{
	/* Allocate an edjcmdname_t for it */
	edjcmdname_t *sn = malloc(sizeof(edjcmdname_t));

	/* Fill it */
	sn->pluginname = pluginname;
	sn->name = cmdname;
	sn->argparser = argparser;
	sn->run = run;

	/* Add it to the list */
	sn->other = names;
	names = sn;

	/* Return it.  The command parser will likely need to know it. */
	return sn;
}

/* Generate an error message */
edjcmdout_t *edj_cmd_error(const char *where, const char *fmt, ...)
{
	va_list	ap;
	size_t	size;
	edjcmdout_t *result;

	/* !!!Translate the message via catalog using "code".  EXCEPT if the
	 * format is "%s" then assume it has already been translated.
	 */

	/* Figure out how long the message will be */
	va_start(ap, fmt);
	size = vsnprintf(NULL, 0, fmt, ap);
	va_end(ap);

	/* Allocate the error structure with enough space for the message */
	result = malloc(sizeof(edjcmdout_t) + size);

	/* Fill the error structure */
	memset(result, 0, sizeof(edjcmdout_t) + size);
	result->where = where;
	va_start(ap, fmt);
	vsnprintf(result->text, size + 1, fmt, ap);
	va_end(ap);

	/* Return it */
	return result;
}


/*****************************************************************************
 * The following functions parse parts of an expression.  The various
 * {cmdname}_parse functions can call these as they see fit.
 *****************************************************************************/

/* Skip past whitespace and comments.  This may include newlines. */
void edj_cmd_parse_whitespace(edjsrc_t *src)
{
	do {
		/* Actual whitespace */
		while (isspace(*src->str))
			src->str++;

		/* Comments */
		if (src->str[0] == '/' && src->str[1] == '/') {
			while (*src->str && *src->str != '\n')
				src->str++;
		}
	} while (isspace(*src->str));
}

/* Skip past whitespace, comments, and an optional type declaration */
void edj_cmd_parse_whitespace_or_type(edjsrc_t *src, char **refstr)
{
	int	nest;
	int	quote;
	int	afterop;
	const char	*start;
	int	len;

	/* Skip whitespace and some comments */
	edj_cmd_parse_whitespace(src);

	/* If no "?:" or ":" then no type.  We're done. */
	if (*src->str == '?')
		src->str++;
	if (*src->str != ':') {
		if (refstr)
			*refstr = NULL;
		return;
	}

	/* Skip past the ":" */
	src->str++;
	start = src->str;

	/* Skip past the type.  Types consist of names, literals (including
	 * quoted strings), "|" operators, curly braces, square brackets, and
	 * maybe commas within the  brackets/braces.
	 */
	nest = quote = 0;
	afterop = 1;
	for (; *src->str; src->str++) {
		/* Newline ends comments */
		if (quote == '/' && *src->str == '\n')
			quote = '\0';

		/* Letters/numbers always allowed, and reset afterop */
		if (isalnum(*src->str)) {
			afterop = 0;
			continue;
		}

		/* Whitespace always allowed */
		if (isspace(*src->str))
			continue;

		/* A few other characters have special meaning */
		switch (*src->str) {
		case '/': /* Comment, if doubled and not in quoted string */
			if (quote)
				continue;
			if (src->str[1] == '/') {
				quote = '/';
				src->str++;
				afterop = 0;
				continue;
			}
			break;

		case '"':
		case '\'':
		case '`': /* Start of quote, unless we're already quoting */
			if (!quote)
				quote = *src->str;
			else if (quote == *src->str)
				quote = 0;
			afterop = 0;
			continue;

		case '\\': /* Escape within a quoted string */
			if (quote == '"' || quote == '\'' ) {
				if (src->str[1])
					src->str++;
				continue;
			}
			break;

		case '-':
		case '+':
		case '.': /* Parts of numbers, always allowed */
			continue;

		case '|': /* | operator is allowed */
			if (nest == 0)
				afterop = 1;
			continue;

		case '{':
		case '[': /* Start a brace/bracket, but only after operator */
			if (quote)
				continue;
			if (!afterop)
				break;
			nest++;
			continue;

		case '}':
		case ']': /* end a brace/bracket */
			if (!quote && nest > 0)
				nest--;
			continue;

		case ',': /* comma only allowed within brace/bracket */
			if (nest > 0 || quote)
				continue;
			break;

		default:
			/* Other chars only allowed in strings */
			if (quote)
				continue;
		}

		/* If we get here, then we hit the end of the type */
		break;
	}

	/* If we have a refstr, then store a copy of the text there, with the
	 * colon and trailing whitespace removed.
	 */
	if (refstr) {
		for (len = src->str - start; len > 0 && isspace(start[len - 1]); len--) {
		}
		if (len <= 0)
			*refstr = NULL;
		else {
			*refstr = malloc(len + 1);
			strncpy(*refstr, start, len);
			(*refstr)[len] = '\0';
		}
	}
}

/* Parse a key (name) and return it as a dynamically-allocated string.
 * Advance src->str past the name and any trailing whitespace.  Returns NULL
 * if not a name.  The "quoteable" parameter should be 1 to allow strings
 * quoted with " or ' to be considered keys, or 0 to only allow alphanumeric
 * keys or `-quoted keys.
 */
char *edj_cmd_parse_key(edjsrc_t *src, int quotable)
{
	size_t	len, unescapedlen;
	char	*key;

	/* Skip leading whitespace */
	edj_cmd_parse_whitespace(src);

	if (isalpha(*src->str) || *src->str == '_') {
		/* Unquoted alphanumeric name */
		for (len = 1; isalnum(src->str[len]) || src->str[len] == '_'; len++){
		}
		key = malloc(len + 1);
		strncpy(key, src->str, len);
		key[len] = '\0';
		src->str += len;
	} else if (*src->str == '`') {
		/* Backtick quoted name */
		src->str++;
		for (len = 0; src->str[len] && src->str[len] != '`'; len++){
		}
		key = malloc(len + 1);
		strncpy(key, src->str, len);
		key[len] = '\0';
		src->str += len + 1;
	} else if (quotable && (*src->str == '"' || *src->str == '\'')) {
		/* String, acting as a name */
		for (len = 1; src->str[len] && src->str[len] == *src->str; len++) {
			if (src->str[len] == '\\' && src->str[len + 1])
				len++;
		}
		unescapedlen = edj_mbs_unescape(NULL, src->str+1, len - 1);
		key = malloc(unescapedlen + 1);
		edj_mbs_unescape(key, src->str+1, len - 1);
		key[unescapedlen] = '\0';
		src->str += len + 1;
	} else
		return NULL;

	/* Skip trailing whitespace */
	edj_cmd_parse_whitespace(src);

	/* Return the key */
	return key;
}

/* Parse a parenthesized expression, possibly with some other syntax elements
 * mixed in.  This is smart enough to handle nested parentheses, and
 * parentheses in strings.  Returns the contents of the parentheses (without
 * the parentheses themselves) as a dynamically-allocated string, or NULL if
 * not a valid parenthesized expression.
 */
char *edj_cmd_parse_paren(edjsrc_t *src)
{
	int	nest;
	char	quote;
	const char	*scan;
	size_t	len;
	char	*paren;

	/* Skip leading whitespace */
	edj_cmd_parse_whitespace(src);

	/* If not a parentheses, fail */
	if (*src->str != '(')
		return NULL;

	/* Find the extent of the parenthesized expression */
	for (scan = src->str + 1, nest = 1, quote = '\0'; nest > 0; scan++) {
		if (!*scan)
			return NULL; /* Hit end of input without ')' */
		else if (*scan == '\\' && (quote == '"' || quote == '\'') && scan[1])
			scan++;
		else if (*scan == quote)
			quote = '\0';
		else if (!quote && (*scan == '`' || *scan == '"' || *scan == '\''))
			quote = *scan;
		else if (!quote && *scan == '(')
			nest++;
		else if (!quote && *scan == ')')
			nest--;
	}

	/* Copy it into a dynamic string */
	len = (size_t)(scan - src->str) - 2;
	paren = malloc(len + 1);
	strncpy(paren, src->str + 1, len);
	paren[len] = '\0';
	src->str = scan;

	/* Skip trailing whitespace */
	edj_cmd_parse_whitespace(src);

	/* Return the contents of the parentheses */
	return paren;
}

/* Allocate a statement, and initialize it */
edjcmd_t *edj_cmd(edjsrc_t *src, edjcmdname_t *name)
{
	edjcmd_t *cmd = malloc(sizeof(edjcmd_t));
	memset(cmd, 0, sizeof(edjcmd_t));
	cmd->where = src->str;
	cmd->name = name;
	return cmd;
}

/* Free a statement, and any related statements or data */
void edj_cmd_free(edjcmd_t *cmd)
{
	/* Defend against NULL and EDJ_CMD_ERROR */
	if (!cmd || cmd == EDJ_CMD_ERROR)
		return;

	/* Free related data */
	if (cmd->key)
		free(cmd->key);
	if (cmd->calc)
		edj_calc_free(cmd->calc);
	edj_cmd_free(cmd->sub);
	edj_cmd_free(cmd->more);
	edj_cmd_free(cmd->nextcmd);

	/* Free the cmd itself */
	free(cmd);
}

/* Parse a single statement and return it.  If it can't be parsed, then issue
 * an error message and return NULL.  If it is a function definition, return
 * it instead of processing it immediately.
 */
edjcmd_t *edj_cmd_parse_single(edjsrc_t *src, edjcmdout_t **referr)
{
	edjcmdname_t	*sn;
	size_t 		len;
	edjcalc_t	*calc;
	const char	*where, *end, *err;
	edjcmd_t	*cmd;

	/* Skip leading whitespace */
	edj_cmd_parse_whitespace(src);
	where = src->str;

	/* If it's an empty command, then return NULL */
	if (*src->str == ';') {
		src->str++;
		return NULL;
	} else if (*src->str == '}')
		return NULL;

	/* All statements begin with a command name, except for assignments
	 * and output expressions.  Start by comparing the start of this
	 * command to all known command names.
	 */
	for (sn = names; sn; sn = sn->other) {
		len = strlen(sn->name);
		end = src->str + len;
		if (!edj_mbs_ncasecmp(sn->name, src->str, len)
		 && (!isalnum(*end) && *end != '_'))
			break;
	}

	/* If followed immediately by a "(" then check to see if its a function.
	 * Sometimes functions and commands have the same name, and this helps
	 * us keep them separate.  If it looks like a function call then ignore
	 * the command.
	 */
	if (sn && *end == '(' && edj_calc_function_by_name(sn->name))
		sn = NULL;

	/* If it's a statement, use the statement's parser */
	if (sn) {
		src->str += len;
		return sn->argparser(src, referr);
	}

	/* Hopefully it is an assignment or an output expression.  Parse it. */
	end = err = NULL;
	calc = edj_calc_parse(src->str, &end, &err, 1);
	if (!calc || err || (*end && *end != ';' && *end != '}')) {
		if (calc)
			edj_calc_free(calc);
		if (!err) {
			/* Parsing ended prematurely, but without an error
			 * message.  We need to figure out why it ended
			 * prematurely.  Start by looking for an initial name.
			 */
			char *vagueerr = NULL, afterch = '\0';
			if (isalpha(*where)) {
				/* It started with a name.  Parse the name,
				 * so we can report it as an unknown command.
				 */
				src->str = where;
				vagueerr = edj_cmd_parse_key(src, 0);
				if (src->str < src->buf + src->size)
					afterch = *src->str;
			}

			/* If no name, or a function name, then assume we got
			 * an expression error.  (We'd like to check vars and
			 * consts, but we don't have a context yet.) Otherwise,
			 * treat it as an unknown command.
			 */
			if (vagueerr && afterch == '(' && !edj_calc_function_by_name(vagueerr))
				*referr = edj_cmd_error(where, "unkFunc:Unknown function %s()", vagueerr);
			else if (vagueerr && afterch != '.' && afterch != '[')
				*referr = edj_cmd_error(where, "unkCmd:Unknown command \"%s\"", vagueerr);
			else
				*referr = edj_cmd_error(where, "syntax:Expression syntax error");
			if (vagueerr)
				free(vagueerr);
		} else {
			*referr = edj_cmd_error(where, "%s", err);
		}
		return NULL;
	}

	/* Stuff it into an edjcmd_t */
	cmd = edj_cmd(src, &jcn_calc);
	cmd->calc = calc;

	/* Move past the end of the statement */
	src->str = end;
	if (*src->str == ';')
		src->str++;
	edj_cmd_parse_whitespace(src);

	/* Return it */
	return cmd;
}

/* Parse a statement block, and return it.  If can't be parsed, then store an
 * error message at *referr and return NULL.  Function declarations are not
 * allowed, and should generate an error message.  An empty set of curly braces
 * is allowed, though, and should return a "NO OP" statement.
 */
edjcmd_t *edj_cmd_parse_curly(edjsrc_t *src, edjcmdout_t **referr)
{
	edjcmd_t *cmd, *current;

	/* Skip whitespace */
	edj_cmd_parse_whitespace(src);

	/* Expect a '{'.  For anything else, assume it's a single statement. */
	if (*src->str == '{') {
		src->str++;
		cmd = current = edj_cmd_parse_single(src, referr);
		while (*referr == NULL && *src->str != '}') {
			current->nextcmd = edj_cmd_parse_single(src, referr);
			edj_cmd_parse_whitespace(src);
			if (current->nextcmd)
				current = current->nextcmd;
			if (*referr)
				break;
		}
		if (*src->str == '}')
			src->str++;
	} else {
		cmd = edj_cmd_parse_single(src, referr);
	}

	/* Skip trailing whitespace */
	edj_cmd_parse_whitespace(src);

	/* Return it */
	return cmd;
}

edjcmd_t *edj_cmd_parse(edjsrc_t *src)
{
	edjcmdout_t *result = NULL;
	edjcmd_t *cmd, *firstcmd, *nextcmd;
	edjfile_t *jf;
	int	lineno;

	/* If first line starts with "#!" then skip to second line */
	if (src->str[0] == '#' && src->str[1] == '!') {
		while (*src->str && *src->str != '\n')
			src->str++;
	}

	/* For each statement... */
	edj_cmd_parse_whitespace(src);
	firstcmd = cmd = NULL;
	while (src->str < src->buf + src->size && *src->str) {
		/* Parse it */
		nextcmd = edj_cmd_parse_single(src, &result);

		/* If error then report it and quit */
		if (result) {
			jf = edj_file_containing(result->where, &lineno);
			if (jf)
				edj_user_printf(NULL, "error", "%s:%d: ", jf->filename, lineno);
			edj_user_printf(NULL, "error", "%s\n", result->text);
			free(result);
			edj_cmd_free(firstcmd);
			return EDJ_CMD_ERROR;
		}

		/* It could be NULL, which is *NOT* an error.  That would be
		 * for things like function definitions, which are processed
		 * by the parser and not at run-time.  Skip NULL */
		if (!nextcmd)
			continue;

		/* Anything else gets added to the statement chain */
		if (cmd)
			cmd->nextcmd = nextcmd;
		else
			firstcmd = nextcmd;
		cmd = nextcmd;

		/* Also, store the filename and line number of this command */
		cmd->where = src->str;

		/* Skip whitespace */
		edj_cmd_parse_whitespace(src);
	}

	/* Return the commands.  Might be NULL. */
	return firstcmd;
}

/* Parse a string as edjcalc commands.  If an error is detected then an
 * error message will be output and this will return NULL.  However, NULL
 * can also be returned if the text is empty, or only contains function
 * definitions, so NULL is *not* an error indication; it just means there's
 * nothing to execute or free.
 */
edjcmd_t *edj_cmd_parse_string(char *text)
{
	edjsrc_t srcbuf;

	/* Fill the src buffer */
	srcbuf.buf = text;
	srcbuf.str = text;
	srcbuf.size = strlen(text);

	/* Parse it */
	return edj_cmd_parse(&srcbuf);
}


/* Parse a file, and return any commands from it. If an error is detected
 * then an error message will be output and this will return NULL.  However,
 * NULL can also be returned if the file is empty, or only contains function
 * definitions, so NULL is *not* an error indication; it just means there's
 * nothing to execute or free.
 */
edjcmd_t *edj_cmd_parse_file(const char *filename) 
{
	edjfile_t *jf;
	edjcmd_t *cmd;
	edjsrc_t srcbuf;

	/* Load the file into memory.  We'll keep it loaded forever, so we can
	 * use it to report error locations and maybe do other debugging.
	 */
	jf = edj_file_load(filename);
	if (!jf) {
		perror(filename);
		return NULL;
	}

	/* Fill in the srcbuf */
	srcbuf.buf = jf->base;
	srcbuf.str = jf->base;
	srcbuf.size = jf->size;

	/* Parse it */
	cmd = edj_cmd_parse(&srcbuf);

	/* Return it */
	return cmd;
}

/* Run a series of statements, and return the result */
edjcmdout_t *edj_cmd_run(edjcmd_t *cmd, edjcontext_t **refcontext)
{
	edjcmdout_t *result = NULL;

	while (cmd && !result) {
		assert(cmd != EDJ_CMD_ERROR);

		/* Maybe output trace info */
		if (edj_debug_flags.trace) {
			int lineno;
			edjfile_t *jf = edj_file_containing(cmd->where, &lineno);
			if (jf)
				edj_user_printf(NULL, "debug", "%s:%d: ", jf->filename, lineno);
			if (cmd->key)
				edj_user_printf(NULL, "debug", "%s %s\n", cmd->name->name, cmd->key);
			else
				edj_user_printf(NULL, "debug", "%s\n", cmd->name->name);
		}

		/* Run the command */
		result = (*cmd->name->run)(cmd, refcontext);

		/* If mismatched "case", then skip ahead to the next case */
		if (result && result->ret == &edj_cmd_case_mismatch) {
			/* We're handling this result here.  Free it */
			free(result);
			result = NULL;

			/* Skip to the next "case" or "default" statement */
			while ((cmd = cmd->nextcmd) != NULL
			    && cmd->name != &jcn_case 
			    && cmd->name != &jcn_default) {
			}
		} else {
			/* For NULL, just go to the next command.  If it's
			 * some other value, such as a "return", then we'll
			 * exit the loop so changing "cmd" here is harmless.
			 */
			cmd = cmd->nextcmd;
		}
	}
	return result;
}

/* Invoke a user-defined function, and return its value */
edj_t *edj_cmd_fncall(edj_t *args, edjfunc_t *fn, edjcontext_t *context)
{
	edjcmdout_t *result;
	edj_t	*out;

	assert(fn->user);

	/* Add the call frame to the context stack */
	context = edj_context_func(context, fn, args);

	/* Run the body of the function */
	result = edj_cmd_run(fn->user, &context);

	/* Decode the "result" response */
	if (!result) /* Function terminated without "return" -- use null */
		out = edj_null();
	else if (!result->ret)
		out = edj_error_null(result->where, "%s", result->text);
	else if (result->ret == &edj_cmd_break) /* "break" */
		out = edj_error_null(result->where, "break:Misuse of \"break\"");
	else if (result->ret == &edj_cmd_continue) /* "continue" */
		out = edj_error_null(result->where, "continue:Misuse of \"continue\"");
	else /* "return" */
		out = result->ret;

	/* Free "result" but not "result->ret" */
	if (result)
		free(result);

	/* Clean up the context, possibly including local vars and consts */
	while ((context->flags & EDJ_CONTEXT_ARGS) == 0)
		context = edj_context_free(context);
	context = edj_context_free(context);

	/* Return the result */
	return out;

}

/* Append any commands from "added" to the end of "existing".  Either of those
 * can be NULL to represent an empty list.  If context is non-NULL then
 * evaluate any "var" or "const" commands instead of appending them.
 * Either way, the commands from "added" are no longer valid when this
 * function returns; you don't need to store it or free it.
 */
edjcmd_t *edj_cmd_append(edjcmd_t *existing, edjcmd_t *added, edjcontext_t *context)
{
	edjcmd_t *nextcmd, *end;
#if 0
	edjcmdout_t *result;
#endif

	/* If "existing" is EDJ_CMD_ERROR then just return it unchanged. */
	if (existing == EDJ_CMD_ERROR)
		return existing;

	/* If "added" is NULL, do nothing */
	if (!added)
		return existing;

	/* If "added" is EDJ_CMD_ERROR then free the "existing" list (if any)
	 * and return EDJ_CMD_ERROR.
	 */
	if (added == EDJ_CMD_ERROR) {
		edj_cmd_free(existing);
		return EDJ_CMD_ERROR;
	}

	/* If "existing" is non-NULL then move to the end of the list */
	if (existing) {
		end = existing;
		while (end->nextcmd)
			end = end->nextcmd;
	}

	/* For each command from "added"... */
	for (; added; added = nextcmd) {
		nextcmd = added->nextcmd;
		added->nextcmd = NULL;

#if 0
		/* Maybe execute "const" and "var" now */
		if (context && (added->name->run == var_run || added->name->run == const_run)) {
			result = edj_cmd_run(added, &context);
			free(result);
			edj_cmd_free(added);
			continue;
		}
#endif

		/* Append this command to "existing" */
		if (existing)
			end->nextcmd = added;
		else
			existing = added;
		end = added;
	}

	/* Return the combined list */
	return existing;
}



/****************************************************************************/
/* Everything after this is for parsing and running built-in commands.      */
/****************************************************************************/

static edjcmd_t *if_parse(edjsrc_t *src, edjcmdout_t **referr)
{
	edjcmd_t	*parsed;
	char	*str;
	const char	*end, *err = NULL;

	/* Skip leading whitespace */
	edj_cmd_parse_whitespace(src);

	/* Allocate the edjcmd_t for it */
	parsed = edj_cmd(src, &jcn_if);

	/* Get the condition */
	str = edj_cmd_parse_paren(src);
	if (!str) {
		*referr = edj_cmd_error(src->str, "Missing or malformed \"%s\" condition", "if");
		return parsed;
	}

	/* Parse the condition */
	parsed->calc = edj_calc_parse(str, &end, &err, 0);
	if (err || *end || !parsed->calc) {
		free(str);
		if (err)
			*referr = edj_cmd_error(src->str, "%s", err);
		else
			*referr = edj_cmd_error(src->str, "Syntax error in \"%s\" condition", "if");
		return parsed;
	}
	free(str);

	/* Get the "then" statements */
	parsed->sub = edj_cmd_parse_curly(src, referr);
	if (*referr)
		return parsed;

	/* If followed by "else" then parse the "else" statements */
	if (!strncmp(src->str, "else", 4) && !isalnum((src->str)[4])) {
		src->str += 4;
		parsed->more = edj_cmd_parse_curly(src, referr);
	}

	/* Return it */
	return parsed;
}

static edjcmdout_t *if_run(edjcmd_t *cmd, edjcontext_t **refcontext)
{
	edj_t *jsbool = edj_calc(cmd->calc, *refcontext, NULL);
	int	bool = edj_is_true(jsbool);
	edj_free(jsbool);
	if (bool)
		return edj_cmd_run(cmd->sub, refcontext);
	else
		return edj_cmd_run(cmd->more, refcontext);
}

static edjcmd_t *while_parse(edjsrc_t *src, edjcmdout_t **referr)
{
	edjcmd_t	*parsed;
	char	*str;
	const char *end, *err = NULL;

	/* Skip leading whitespace */
	edj_cmd_parse_whitespace(src);

	/* Allocate the edjcmd_t for it */
	parsed = edj_cmd(src, &jcn_while);

	/* Get the condition */
	str = edj_cmd_parse_paren(src);
	if (!str) {
		*referr = edj_cmd_error(src->str, "Missing or malformed \"%s\" condition", "while");
		return parsed;
	}

	/* Parse the condition */
	parsed->calc = edj_calc_parse(str, &end, &err, 0);
	if (err || *end || !parsed->calc) {
		free(str);
		if (err)
			*referr = edj_cmd_error(src->str, "%s", err);
		else
			*referr = edj_cmd_error(src->str, "Syntax error in \"while\" condition");
		return parsed;
	}
	free(str);

	/* Get the "loop" statements */
	parsed->sub = edj_cmd_parse_curly(src, referr);
	if (*referr)
		return parsed;

	/* Return it */
	return parsed;
}

static edjcmdout_t *while_run(edjcmd_t *cmd, edjcontext_t **refcontext)
{
	for (;;) {
		/* Evaluate the condition */
		edj_t *jsbool = edj_calc(cmd->calc, *refcontext, NULL);
		int	bool = edj_is_true(jsbool);
		edj_free(jsbool);

		/* If the condition is false, then terminate the loop */
		if (!bool)
			return NULL;

		/* Run the loop body.  If it has an error, then return the
		 * error; otherwise continue to loop.
		 */
		edjcmdout_t *result = edj_cmd_run(cmd->sub, refcontext);

		/* If we got a "continue" then ignore it and stay in the loop */
		if (result && result->ret == &edj_cmd_continue) {
			free(result);
			result = NULL;
		}

		/* If "breaK', "return", or error then exit the loop */
		if (result) {
			/* If we got a "break", ignore it.  Otherwise ("return"
			 * or an error) return it.
			 */
			if (result && result->ret == &edj_cmd_break) {
				free(result);
				result = NULL;
			}
			return result;
		}
	}
}

static edjcmd_t *for_parse(edjsrc_t *src, edjcmdout_t **referr)
{
	edjcmd_t	*parsed;
	char	*str = NULL;
	const char *end, *err = NULL;
	edjsrc_t	parensrc;

	/* Skip leading whitespace */
	edj_cmd_parse_whitespace(src);

	/* Allocate the edjcmd_t for it */
	parsed = edj_cmd(src, &jcn_for);

	/* Get the loop attributes */
	str = edj_cmd_parse_paren(src);
	if (!str) {
		*referr = edj_cmd_error(src->str, "Missing or malformed \"%s\" loop attributes", "for");
		goto CleanUpAfterError;
	}

	/* Parse the attributes: (var key of expr), (key of expr), or (expr) */
	parensrc.str = str;
	if (!strncasecmp(parensrc.str, "var", 3) && isspace(parensrc.str[3])) {
		parsed->var = 1;
		parensrc.str += 3;
		edj_cmd_parse_whitespace(&parensrc);
	}
	else if (!strncasecmp(parensrc.str, "const", 5) && isspace(parensrc.str[5])) {
		parsed->var = 1;
		parensrc.str += 5;
		edj_cmd_parse_whitespace(&parensrc);
	}
	parsed->key = edj_cmd_parse_key(&parensrc, 1);
	if (parsed->key && parensrc.str[0] == '=') {
		parensrc.str++;
		edj_cmd_parse_whitespace(&parensrc);
	} else if (parsed->key && !strncasecmp(parensrc.str, "of", 2) && !isalnum(parensrc.str[2]) && parensrc.str[2] != '_') {
		parensrc.str += 2;
		edj_cmd_parse_whitespace(&parensrc);
	} else {
		/* If we parsed a key, it wasn't part of "key =/of expr",
		 * it was the first word of "expr".  Clean up!
		 */
		if (parsed->key) {
			free(parsed->key);
			parsed->key = NULL;
		}
		parsed->var = 0;
		parensrc.str = str;
	}
	parsed->calc = edj_calc_parse(parensrc.str, &end, &err, 0);
	if (err || *end || !parsed->calc) {
		if (err)
			*referr = edj_cmd_error(src->str, "%s", err);
		else
			*referr = edj_cmd_error(src->str, "Syntax error in \"\" expression", "for");
		goto CleanUpAfterError;
	}

	/* Get the "loop" statements */
	parsed->sub = edj_cmd_parse_curly(src, referr);
	if (*referr)
		goto CleanUpAfterError;

	/* Return it */
	if (str)
		free(str);
	return parsed;

CleanUpAfterError:
	if (str)
		free(str);
	edj_cmd_free(parsed);
	return NULL;
}

static edjcmdout_t *for_run(edjcmd_t *cmd, edjcontext_t **refcontext)
{
	edj_t	*array, *scan;
	edjcontext_t *layer;
	edjcmdout_t *result = NULL;

	/* Evaluate the for-loop's array expression */
	array = edj_calc(cmd->calc, *refcontext, NULL);
	if (!array || array->type != EDJ_ARRAY) {
		if (edj_is_error(array))
			result = edj_cmd_error(cmd->where, "%s", array->text);
		else
			result = edj_cmd_error(cmd->where, "forNotArray:\"%s\" expression is not an array", "for");
		edj_free(array);
		return result;
	}

	/* Without "var", look for an existing variable to use for the loop. */
	if (!cmd->var && cmd->key && edj_context_by_key(*refcontext, cmd->key, &layer) != NULL) {
		/* Make sure the variable isn't a "const" */
		if (layer->flags & EDJ_CONTEXT_CONST) {
			edj_free(array);
			return edj_cmd_error(cmd->where, "forConst:\"%s\" variable \"%s\" is a %s", "for", cmd->key, "const");
		}

		/* Okay, we have an existing variable! */
		for (scan = edj_first(array); scan; scan = edj_next(scan)) {
			/* Store the value in the variable */
			edj_append(layer->data, edj_key(cmd->key, edj_copy(scan)));

			/* Execute the body of the loop */
			result = edj_cmd_run(cmd->sub, refcontext);

			/* Ignore "continue" and stay in loop.  For anything
			 * else other than NULL, exit the loop.
			 */
			if (result && result->ret == &edj_cmd_continue) {
				free(result);
				result = NULL;
			}
			if (result) {
				edj_break(scan);
				break;
			}
		}
	} else if (cmd->key) {
		/* Add a context for store the variable */
		layer = edj_context(*refcontext, edj_object(), 0);

		/* Loop over the elements */
		for (scan = edj_first(array); scan; scan = edj_next(scan)) {
			/* Store the value in the variable */
			edj_append(layer->data, edj_key(cmd->key, edj_copy(scan)));

			/* Execute the body of the loop */
			result = edj_cmd_run(cmd->sub, &layer);

			/* Ignore "continue" and stay in loop.  For anything
			 * else other than NULL, exit the loop.
			 */
			if (result && result->ret == &edj_cmd_continue) {
				free(result);
				result = NULL;
			}
			if (result) {
				edj_break(scan);
				break;
			}
		}

		/* Clean up */
		edj_context_free(layer);

	} else { /* Anonymous loop */
		/* Loop over the elements */
		for (scan = edj_first(array); scan; scan = edj_next(scan)) {
			/* Add a "this" layer */
			layer = edj_context(*refcontext, scan, EDJ_CONTEXT_THIS | EDJ_CONTEXT_NOFREE);

			/* Run the body of the loop */
			result = edj_cmd_run(cmd->sub, &layer);

			/* Ignore "continue" and stay in loop.  For anything
			 * else other than NULL, exit the loop.
			 */
			if (result && result->ret == &edj_cmd_continue) {
				free(result);
				result = NULL;
			}
			if (result) {
				edj_break(scan);
				break;
			}

			/* Remove the "this" layer */
			edj_context_free(layer);
		}
	}

	/* Free the array */
	edj_free(array);

	/* If we got a "break" pseudo-error, ignore it.  Otherwise (real error
	 * or "return" pseudo-error) return it.
	 */
	if (result && result->ret == &edj_cmd_break) {
		free(result);
		result = NULL;
	}
	return result;
}

static edjcmd_t *try_parse(edjsrc_t *src, edjcmdout_t **referr)
{
	edjcmd_t	*parsed;
	char		*str = NULL;
	edjsrc_t	parensrc;

	/* Allocate the edjcmd_t for it */
	parsed = edj_cmd(src, &jcn_try);

	/* Get the "try" statements */
	parsed->sub = edj_cmd_parse_curly(src, referr);
	if (*referr)
		goto CleanUpAfterError;

	/* Expect "catch" */
	if (strncasecmp(src->str, "catch", 5) || !strchr(" \t\n\r({", src->str[5])) {
		*referr = edj_cmd_error(src->str, "Missing \"%s\"", "catch");
		goto CleanUpAfterError;
	}
	src->str += 5;
	edj_cmd_parse_whitespace(src);

	/* Optional name within parentheses */
	if (*src->str == '(') {
		/* Get the parenthesized expression */
		str = edj_cmd_parse_paren(src);

		/* It should be a single name */
		parensrc.str = str;
		parsed->key = edj_cmd_parse_key(&parensrc, 1);
		if (*parensrc.str) {
			*referr = edj_cmd_error(src->str, "The argument to \"%s\" should be a single name", "catch");
			goto CleanUpAfterError;
		}

		/* Free the string */
		free(str);
		str = NULL;
	}

	/* Get the "catch" statements */
	parsed->more = edj_cmd_parse_curly(src, referr);
	if (*referr)
		goto CleanUpAfterError;

	/* !!! I supposed I could test for a "finally" statement */

	/* Return it */
	return parsed;

CleanUpAfterError:
	if (str)
		free(str);
	edj_cmd_free(parsed);
	return NULL;
}

static edjcmdout_t *try_run(edjcmd_t *cmd, edjcontext_t **refcontext)
{
	edjcmdout_t *result;
	edjcontext_t *caught;
	edj_t *obj, *contextobj;
	edjfile_t *jf;
	int lineno;
	char *scan;

	/* Run the "try" statements.  For any result other than an error,
	 * just return it.
	 */
	result = edj_cmd_run(cmd->sub, refcontext);
	if (!result || result->ret)
		return result;

	/* If no "catch" block, we're done */
	if (!cmd->more)
		return NULL;

	/* If there's a key (a name in parentheses before "catch"), then we
	 * need to add a context where that name is an object describing the
	 * error.
	 */
	if (cmd->key) {

		/* Build the object describing the error */
		obj = edj_object();
		jf = edj_file_containing(result->where, &lineno);
		if (jf) {
			edj_append(obj, edj_key("filename", edj_string(jf->filename, -1)));
			edj_append(obj, edj_key("line", edj_from_int(lineno)));
		}
		for (scan = result->text; isalnum(*scan); scan++) {
		}
		if (*scan == ':') {
			edj_append(obj, edj_key("key", edj_string(result->text, (scan - result->text))));
			scan++;
		} else {
			scan = result->text;
		}
		edj_append(obj, edj_key("message", edj_string(scan, -1)));

		/* Make that object be inside another object, using key as the
		 * the member name.
		 */
		contextobj = edj_object();
		edj_append(contextobj, edj_key(cmd->key, obj));

		/* Stuff it in a context, using the key as the name */
		caught = edj_context(*refcontext, contextobj, 0);

		/* Run the "catch" block with this context */
		result = edj_cmd_run(cmd->more, &caught);

		/* Free the context. This also frees the data allocated above.*/
		edj_context_free(caught);
	} else {
		/* Just run the "catch" block with the same context */
		result = edj_cmd_run(cmd->more, &caught);
	}

	return result;
}


static edjcmd_t *throw_parse(edjsrc_t *src, edjcmdout_t **referr)
{
	edjcmd_t	*parsed;
	const char	*end, *err, *pct;
	edjcalc_t	*jc;

	/* Allocate the edjcmd_t for it */
	parsed = edj_cmd(src, &jcn_throw);

	/* Parse the first (only?) argument.  It should be a string literal. */
	jc = NULL;
	end = err = NULL;
	if (*src->str && *src->str != ';' && *src->str != '}') {
		jc = edj_calc_parse(src->str, &end, &err, 0);
		if (!jc || jc->op != EDJOP_LITERAL)
			goto BadArgs;
		src->str = end;
	}

	/* Allow error text (a string literal).  If none, then use "throw" */
	if (!jc)
		parsed->key = strdup("throw");
	else if (jc->u.literal->type != EDJ_STRING)
		goto BadArgs;
	else {
		/* Store the string in 'key' */
		parsed->key = strdup(jc->u.literal->text);

		/* Don't need this expression anymore */
		edj_calc_free(jc);
		jc = NULL;
	}

	/* The message is allowed to contain one %s.  If it does, then we
	 * expect exactly one additional argument.  If it doesn't then we
	 * don't allow any more arguments.
	 */
	pct = strchr(parsed->key, '%');
	if (pct) {
		/* Only allow one %s formatter -- no second %s, no %d, etc */
		if (pct[1] != 's' || strchr(pct + 1, '%'))
			goto BadArgs;

		/* Must be followed by another expression */
		if (*src->str != ',')
			goto BadArgs;

		/* Parse it */
		src->str++;
		jc = edj_calc_parse(src->str, &end, &err, 0);
		if (!jc || err)
			goto BadArgs;
		src->str = end;

		/* Store it as 'calc' */
		parsed->calc = jc;
		jc = NULL;
	}

	/* Must not be any more arguments */
	if (*src->str && *src->str != ';' && *src->str != '}')
		goto BadArgs;

	/* Skip over ';' at end of cmd */
	if (*src->str == ';')
		src->str++;

	/* Return it */
	return parsed;

BadArgs:
	if (jc)
		edj_calc_free(jc);
	edj_cmd_free(parsed);
	*referr = edj_cmd_error(src->str, "Bad parameters to %s", "throw");
	return NULL;
}

static edjcmdout_t *throw_run(edjcmd_t *cmd, edjcontext_t **refcontext)
{
	edjcmdout_t *result;
	edj_t	*arg;

	/* If there's an argument, evaluate it. */
	arg = NULL;
	if (cmd->calc) {
		arg = edj_calc(cmd->calc, *refcontext, NULL);
	}

	/* Always return an error -- maybe with an argument */
	result = edj_cmd_error(cmd->where, cmd->key, arg ? arg->text : "");

	/* Clean up */
	if (arg)
		edj_free(arg);

	return result;
}

/* This is a helper function for global/local var/const declarations */
static edjcmd_t *gvc_parse(edjsrc_t *src, edjcmdout_t **referr, edjcmd_t *cmd)
{
	edjcmd_t *first = cmd;
	const char	*end, *err;

	/* Expect a name possibly followed by ":type" and/or "=expr" */
	for (;;) {
		cmd->key = edj_cmd_parse_key(src, 1);
		if (!cmd->key) {
			*referr = edj_cmd_error(src->str, "Name expected after %s", cmd->name->name);
			edj_cmd_free(first);
			return NULL;
		}
		edj_cmd_parse_whitespace_or_type(src, NULL);
		if (*src->str == '=') {
			err = NULL;
			src->str++;
			cmd->calc = edj_calc_parse(src->str, &end, &err, 0);
			src->str = end;
			if (err) {
				*referr = edj_cmd_error(src->str, "%s", err);
				edj_cmd_free(first);
				return NULL;
			}
		}

		/* That may be followed by a comma and another declaration */
		if (*src->str == ',') {
			src->str++;
			edj_cmd_parse_whitespace(src);
			cmd->more = edj_cmd(src, first->name);
			cmd = cmd->more;
			cmd->flags = first->flags;
		}
		else
			break;

	}

	/* Probably followed by a ';' */
	if (*src->str == ';')
		src->str++;

	return first;
}

static edjcmdout_t *gvc_run(edjcmd_t *cmd, edjcontext_t **refcontext)
{
	edj_t	*value, *error;
	edjcmd_t *each;

	/* A single statement can declare multiple vars/consts */
	error = NULL;
	for (each = cmd; each; each = each->more) {
		/* Evaluate the value. If error, remember it */
		value = NULL;
		if (each->calc) {
			value = edj_calc(each->calc, *refcontext, NULL);
			if (edj_is_error(value)) {
				if (error)
					free(value);
				else
					error = value;
				value = NULL;
			}
		}
		if (!value)
			value = edj_null();

		/* Add it to the context */
		if (!edj_context_declare(refcontext, each->key, value, each->flags)) {
			/* Duplicate! */
			edj_free(value);
			return edj_cmd_error(each->where, "redeclare:Duplicate %s \"%s\"",
				(each->flags & EDJ_CONTEXT_CONST) ? "const" : "var",
				each->key);
		}
	}

	/* If we encountered an error in an initializer, return it */
	if (error) {
		edjcmdout_t *result;
		result = edj_cmd_error(cmd->where, "%s", error->text);
		edj_free(error);
		return result;
	}

	/* Success! */
	return NULL;
}

static edjcmd_t *break_parse(edjsrc_t *src, edjcmdout_t **referr)
{
	edjcmd_t *cmd = edj_cmd(src, &jcn_break);

	/* No arguments or other components, but we still need to skip ";" */
	edj_cmd_parse_whitespace(src);
	if (*src->str == ';')
		src->str++;
	return cmd;
}

static edjcmdout_t *break_run(edjcmd_t *cmd, edjcontext_t **refcontext)
{
	/* Return a "break" pseudo-error */
	edjcmdout_t *result = edj_cmd_error(cmd->where, "");
	result->ret = &edj_cmd_break;
	return result;
}

static edjcmd_t *continue_parse(edjsrc_t *src, edjcmdout_t **referr)
{
	edjcmd_t *cmd = edj_cmd(src, &jcn_continue);

	/* No arguments or other components, but we still need to skip ";" */
	edj_cmd_parse_whitespace(src);
	if (*src->str == ';')
		src->str++;
	return cmd;
}

static edjcmdout_t *continue_run(edjcmd_t *cmd, edjcontext_t **refcontext)
{
	/* Return a "continue" pseudo-error */
	edjcmdout_t *result = edj_cmd_error(cmd->where, "");
	result->ret = &edj_cmd_continue;
	return result;
}

static edjcmd_t *var_parse(edjsrc_t *src, edjcmdout_t **referr)
{
	edjcmd_t *cmd = edj_cmd(src, &jcn_var);
	cmd->flags = EDJ_CONTEXT_VAR;
	return gvc_parse(src, referr, cmd);
}

static edjcmdout_t *var_run(edjcmd_t *cmd, edjcontext_t **refcontext)
{
	return gvc_run(cmd, refcontext);
}

static edjcmd_t *const_parse(edjsrc_t *src, edjcmdout_t **referr)
{
	edjcmd_t *cmd = edj_cmd(src, &jcn_var);
	cmd->flags = EDJ_CONTEXT_CONST;
	return gvc_parse(src, referr, cmd);
}

static edjcmdout_t *const_run(edjcmd_t *cmd, edjcontext_t **refcontext)
{
	return gvc_run(cmd, refcontext);
}

/* Output a description of a function */
static void describefn(edjfunc_t *f)
{
	edj_t	*params = NULL;

	if (f->fn)
		edj_user_printf(NULL, "normal", "builtin ");
	if (f->agfn)
		edj_user_printf(NULL, "normal", "aggregate ");
	edj_user_printf(NULL, "normal", "function %s", f->name);
	if (f->args)
		edj_user_printf(NULL, "normal", "(%s)", f->args);
	else {
		edj_user_ch('(');
		for (params = f->userparams->first; params; params = params->next) /* undeferred */
			edj_user_printf(NULL, "normal", "%s%s", params->text, params->next ? ", " : ""); /* undeferred */
		edj_user_ch(')');
	}
	if (f->returntype)
		edj_user_printf(NULL, "normal", ":%s", f->returntype);
	edj_user_ch('\n');
}

static edjcmd_t *function_parse(edjsrc_t *src, edjcmdout_t **referr)
{
	char	*fname;
	edjsrc_t paren; /* Used for scanning parameter source */
	edj_t	*params = NULL;
	edjcmd_t *body = NULL;
	char	*returntype = NULL;;

	paren.buf = NULL;

	/* Function name */
	fname = edj_cmd_parse_key(src, 1);
	if (!fname) {
		/* Describe all user-defined functions */
		edjfunc_t *f = edj_calc_function_first();
		for (; f; f = f->other) {
			if (!f->fn)
				describefn(f);
		}
		return NULL;
	}

	/* Parameter list (the parenthesized text) */
	paren.buf = paren.str = edj_cmd_parse_paren(src);
	if (!paren.buf) {
		/* No parameter list, so just describe the named function and
		 * return NULL.
		 */
		edjfunc_t *f = edj_calc_function_by_name(fname);
		if (!f) {
			*referr = edj_cmd_error(src->str, "Unknown function \"%s\"", fname);
			goto Error;
		}

		/* Output a description of the function */
		describefn(f);

		free(fname);
		return NULL;
	}
	paren.size = strlen(paren.buf);

	/* Parameters within the parenthesized text */
	params = edj_object();
	edj_cmd_parse_whitespace(&paren);
	while (*paren.str) {
		char	*pname;
		edj_t	*defvalue;

		/* Parameter name */
		pname = edj_cmd_parse_key(&paren, 0);
		if (!pname) {
			*referr = edj_cmd_error(src->str, "Missing parameter name");
			goto Error;
		}

		/* Possibly a type declaration */
		edj_cmd_parse_whitespace_or_type(&paren, NULL);

		/* If followed by = then use a default */
		if (*paren.str == '=') {
			edjcalc_t *calc;
			const char	*end, *err;

			/* Move past the '=' */
			paren.str++;

			/* Parse the expression */
			err = NULL;
			calc = edj_calc_parse(paren.str, &end, &err, 0);
			if (err) {
				*referr = edj_cmd_error(src->str, "%s in default value", err);
				goto Error;
			}
			if (*end && *end != ',') {
				*referr = edj_cmd_error(src->str, "Syntax error near %.10s", end);
				goto Error;
			}
			paren.str = end;

			/* Evaluate the expression */
			defvalue = edj_calc(calc, NULL, NULL);
			if (edj_is_error(defvalue)) {
				if (defvalue->first)
					*referr = edj_cmd_error((const char *)defvalue->first, "%s", defvalue->text);
				else
					*referr = edj_cmd_error(src->str, "%s", defvalue->text);
				goto Error;
			}

			/* Free the expression */
			edj_calc_free(calc);
		} else {
			/* Use null as the default value */
			defvalue = edj_null();
		}

		/* Add the parameter to the params object */
		edj_append(params, edj_key(pname, defvalue));

		/* Free the name */
		free(pname);

		/* If followed by comma, skip the comma */
		if (*paren.str == ',') {
			paren.str++;
			edj_cmd_parse_whitespace(&paren);
		}
	}

	/* Parentheses may be followed by a return type declaration */
	edj_cmd_parse_whitespace_or_type(src, &returntype);

	/* Body -- if no body, that's okay */
	if (*src->str == '{')
		body = edj_cmd_parse_curly(src, referr);
	else if (edj_calc_function_by_name(fname)) {
		/* No body but the function is already defined -- we were
		 * just redundantly declaring an already-defined function.
		 */
		free(fname);
		free(returntype);
		return NULL;
	} else
		body = NULL;

	/* Define it! */
	if (!*referr) {
		if (edj_calc_function_user(fname, params, (char *)paren.buf, returntype, body)) {
			/* Tried to redefine a built-in, which isn't allowed. */
			*referr = edj_cmd_error(src->str, "Can't redefine built-in function \"%s\"", fname);
			goto Error;
		}

		/* We're done.  Nothing more will be required at runtime.
		 * The fname, params, and body remain allocated since the
		 * function needs them.
		 */
		return  NULL;
	}

Error:
	if (fname)
		free(fname);
	if (paren.buf)
		free((char *)paren.buf);
	if (params)
		edj_free(params);
	if (returntype)
		free(returntype);
	if (body)
		edj_cmd_free(body);
	return NULL;
}

static edjcmdout_t *function_run(edjcmd_t *cmd, edjcontext_t **refcontext)
{
	/* Can't happen */
	return NULL;
}

static edjcmd_t *return_parse(edjsrc_t *src, edjcmdout_t **referr)
{
	edjcalc_t *calc;
	const char	*end, *err;
	edjcmd_t *cmd;
	edjsrc_t start;

	/* Allocate a cmd */
	start = *src;

	/* The return value is optional */
	edj_cmd_parse_whitespace(src);
	if (*src->str && *src->str != ';' && *src->str != '}') {
		/* Parse the expression */
		err = NULL;
		calc = edj_calc_parse(src->str, &end, &err, 0);
		if (err) {
			*referr = edj_cmd_error(src->str, "%s", err);
			if (calc)
				edj_calc_free(calc);
			return NULL;
		}
		if (*end && (*end != ';' && *end != '}')) {
			*referr = edj_cmd_error(src->str, "Syntax error near %.10s", end);
			if (calc)
				edj_calc_free(calc);
			return NULL;
		}
		src->str = end;

	} else {
		/* With no expression, assume "null */
		calc = edj_calc_parse("null", NULL, NULL, 0);
	}

	/* Move past the ';', if there is one */
	if (*src->str == ';')
		src->str++;
	edj_cmd_parse_whitespace(src);

	/* Build the command, and return it */
	cmd = edj_cmd(&start, &jcn_return);
	cmd->calc = calc;
	return cmd;
}

static edjcmdout_t *return_run(edjcmd_t *cmd, edjcontext_t **refcontext)
{
	/* Return a 'return" pseudo-error, returning whatever edj_calc() gives
	 * us.  If edj_calc() returns an actual error, so be it.  If that
	 * error doesn't include the position of the error, then use the
	 * position of this "return" command.
	 */
	edjcmdout_t *err = edj_cmd_error(cmd->where, "");
	err->ret = edj_calc(cmd->calc, *refcontext, NULL);
	if (edj_is_error(err->ret) && !err->ret->first)
		err->ret->first = (edj_t *)cmd->where;
	return err;
}

static edjcmd_t *switch_parse(edjsrc_t *src, edjcmdout_t **referr)
{
	edjcmd_t	*parsed;
	char	*str;
	const char *end, *err = NULL;

	/* Skip leading whitespace */
	edj_cmd_parse_whitespace(src);

	/* Allocate the edjcmd_t for it */
	parsed = edj_cmd(src, &jcn_switch);

	/* Get the condition */
	str = edj_cmd_parse_paren(src);
	if (!str) {
		*referr = edj_cmd_error(src->str, "Missing or malformed \"%s\" expression", "switch");
		return parsed;
	}

	/* Parse the expression */
	parsed->calc = edj_calc_parse(str, &end, &err, 0);
	if (err || *end || !parsed->calc) {
		free(str);
		if (err)
			*referr = edj_cmd_error(src->str, "%s", err);
		else
			*referr = edj_cmd_error(src->str, "Syntax error in \"%s\" expression", "switch");
		return parsed;
	}
	free(str);

	/* Get the "body" statements */
	parsed->sub = edj_cmd_parse_curly(src, referr);
	if (*referr)
		return parsed;

	/* Return it */
	return parsed;
}

static edjcmdout_t *switch_run(edjcmd_t *cmd, edjcontext_t **refcontext)
{
	edjcontext_t *layer;
	edj_t	*switchcase;
	edjcmdout_t *result;

	/* Evaluate the expression */
	switchcase = edj_calc(cmd->calc, *refcontext, NULL);

	/* Add a context for store the "switchcase" variable */
	layer = edj_context(*refcontext, edj_object(), 0);

	/* Store the value in the variable */
	edj_append(layer->data, edj_key("switchcase", switchcase));

	/* Execute the body of the switch */
	result = edj_cmd_run(cmd->sub, &layer);

	/* If exited with a "break", ignore it */
	if (result && result->ret == &edj_cmd_break) {
		free(result);
		result = NULL;
	}

	/* Clean up */
	edj_context_free(layer);

	return result;
}

static edjcmd_t *case_parse(edjsrc_t *src, edjcmdout_t **referr)
{
	edjcmd_t	*parsed;
	const char *end, *err = NULL;
	int	len, quote, nest, escape;

	/* Skip leading whitespace */
	edj_cmd_parse_whitespace(src);

	/* Allocate the edjcmd_t for it */
	parsed = edj_cmd(src, &jcn_case);

	/* Extract the case.  This is trickier than it sounds -- we can't just
	 * parse it because it should end with a ":", and ":" is a valid
	 * operator.
	 */
	 nest = quote = escape = 0;
	 for (len = 0;
	      src->str[len] >= ' ' && (nest || quote || src->str[len] != ':');
	      len++) {
		if (escape)
			escape = 0;
		else if (quote && src->str[len] == quote)
			quote = 0;
		else if (!quote && (src->str[len] == '"' || src->str[len] == '\''))
			quote = src->str[len];
		else if (quote && src->str[len] == '\\')
			escape = 1;
		else if (!quote && src->str[len] == '(')
			nest++;
		else if (!quote && src->str[len] == ')')
			nest--;
	}
	if (len == 0 || src->str[len] != ':') {
		*referr = edj_cmd_error(src->str, "Missing or malformed \"%s\" expression", "case");
		return parsed;
	}
	char str[len + 1];
	strncpy(str, src->str, len);
	str[len] = '\0';

	/* Parse the case.  */
	parsed->calc = edj_calc_parse(str, &end, &err, 0);
	if (err || *end || !parsed->calc) {
		free(str);
		if (err)
			*referr = edj_cmd_error(src->str, "%s", err);
		else
			*referr = edj_cmd_error(src->str, "Syntax error in \"%s\" expression", "case");
		return parsed;
	}

	/* Move past the ":" */
	src->str += len + 1;

	/* Return it */
	return parsed;
}

static edjcmdout_t *case_run(edjcmd_t *cmd, edjcontext_t **refcontext)
{
	edj_t	*switchcase;
	int	match;

	/* Fetch the "switchcase" value.  Note that we only look in the top
	 * context layer so that if switch statements are nested, we don't see
	 * the outer one.  Use edj_by_key() instead of edj_context_by_key().
	 */
	switchcase = edj_by_key((*refcontext)->data, "switchcase");
	if (!switchcase)
		return edj_cmd_error(cmd->where, "case:Can't use \"%s\" outside of \"%s\"", "case", "switch");

	/* If "null" then continue with next command */
	if (edj_is_null(switchcase))
		return NULL;

	/* Compare the case value to switchcase.
	 * 
	 * One optimization: If the value is a literal then we don't bother
	 * calling edj_calc(), mostly so we can avoid allocating and freeing
	 * the value.
	 */
	if (cmd->calc->op == EDJOP_LITERAL) {
		match = edj_equal(cmd->calc->u.literal, switchcase);
	} else {
		edj_t *thiscase = edj_calc(cmd->calc, *refcontext, NULL);
		match = edj_equal(thiscase, switchcase);
		edj_free(thiscase);
	}

	/* Anything else should match exactly.  If this does, then
	 * set switchcase to null and return NULL so we continue to the
	 * next statement.  If it does NOT match then we leave switchcase
	 * unchanged, and continue to the next "case" or "default" statement.
	 */
	if (match) {
		/* Match!  Change "switchcase" to null, and continue on to
		 * the next statement.
		 */
		edj_append((*refcontext)->data, edj_key("switchcase", edj_null()));
		return NULL;
	} else {
		/* No match!  Leave "switchcase" unchanged, and skip to the
		 * next "case" or "default" statement.
		 */
		edjcmdout_t *result = edj_cmd_error(cmd->where, "");
		result->ret = &edj_cmd_case_mismatch;
		return result;
	}
}

static edjcmd_t *default_parse(edjsrc_t *src, edjcmdout_t **referr)
{
	edjcmd_t	*parsed;

	/* Skip leading whitespace */
	edj_cmd_parse_whitespace(src);

	/* Allocate the edjcmd_t for it */
	parsed = edj_cmd(src, &jcn_default);

	/* Ends with a colon */
	if (*src->str != ':') {
		*referr = edj_cmd_error(src->str, "Syntax error in \"%s\"", "default");
		return parsed;
	}
	src->str++;

	/* Return it */
	return parsed;
}

static edjcmdout_t *default_run(edjcmd_t *cmd, edjcontext_t **refcontext)
{
	/* The "default" command always continues to the next command */
	return NULL;
}

static edjcmd_t *void_parse(edjsrc_t *src, edjcmdout_t **referr)
{
	edjcalc_t *calc;
	const char	*end, *err;
	edjcmd_t *cmd;
	edjsrc_t start;

	/* Allocate a cmd */
	start = *src;

	/* The expression is mandatory */
	edj_cmd_parse_whitespace(src);
	if (!*src->str || *src->str == ';' || *src->str == '}') {
		edj_cmd_error(src->str, "The \"%s\" command requires an expression", "void");
	}

	/* Parse the expression */
	err = NULL;
	calc = edj_calc_parse(src->str, &end, &err, 0);
	if (err) {
		*referr = edj_cmd_error(src->str, "%s", err);
		if (calc)
			edj_calc_free(calc);
		return NULL;
	}
	if (*end && (*end != ';' && *end != '}')) {
		*referr = edj_cmd_error(src->str, "Syntax error near %.10s", end);
		if (calc)
			edj_calc_free(calc);
		return NULL;
	}
	src->str = end;

	/* Move past the ';', if there is one */
	if (*src->str == ';')
		src->str++;
	edj_cmd_parse_whitespace(src);

	/* Build the command, and return it */
	cmd = edj_cmd(&start, &jcn_void);
	cmd->calc = calc;
	return cmd;
}

static edjcmdout_t *void_run(edjcmd_t *cmd, edjcontext_t **refcontext)
{
	/* Evaluate the expression but return NULL */
	edj_free(edj_calc(cmd->calc, *refcontext, NULL));
	return NULL;
}

static edjcmd_t *explain_parse(edjsrc_t *src, edjcmdout_t **referr)
{
	const char	*end, *err;
	edjcmd_t *cmd;

	/* Allocate a cmd */
	cmd = edj_cmd(src, &jcn_explain);

	/* Three ways to go: "explain" explains the default table, "explain?"
	 * says where the default table is located, and "explain expr" explains
	 * the result of an expression.
	 */
	edj_cmd_parse_whitespace(src);
	if (!*src->str || *src->str == ';' || *src->str == '}' || *src->str == ',') {
		/* Use the default */
	} else if (*src->str == '?') {
		/* Use the default, but suppress the actual "explain" table */
		cmd->var = 1;
		src->str++;
		edj_cmd_parse_whitespace(src);
	} else {
		/* Use an expression */
		err = NULL;
		cmd->calc = edj_calc_parse(src->str, &end, &err, 0);
		if (err) {
			*referr = edj_cmd_error(src->str, "%s", err);
			edj_cmd_free(cmd);
			return NULL;
		}
		src->str = end;
	}

	/* If comma, then look for a word to search for in keys */
	edj_cmd_parse_whitespace(src);
	if (*src->str == ',') {
		src->str++;
		cmd->key = edj_cmd_parse_key(src, 1);
		if (!cmd->key) {
			*referr = edj_cmd_error(src->str, "Expected a partial key after the comma");
			edj_cmd_free(cmd);
			return NULL;
		}
	}

	/* Detect cruft after the arguments */
	if (*src->str && (*src->str != ';' && *src->str != '}')) {
		*referr = edj_cmd_error(src->str, "Syntax error near %.10s", src->str);
		edj_cmd_free(cmd);
		return NULL;
	}

	/* Move past the ';', if there is one */
	if (*src->str == ';')
		src->str++;
	edj_cmd_parse_whitespace(src);

	/* Return the command */
	return cmd;
}

static edjcmdout_t *explain_run(edjcmd_t *cmd, edjcontext_t **refcontext)
{
	edj_t	*table, *mustfree, *columns;
	char	*expr;

	/* Is there an expression, explicitly naming a table? */
	if (!cmd->calc) {
		/* No, so look for a default table */
		table = edj_context_default_table(*refcontext, &expr);
		mustfree = NULL;

		/* If no table, say so */
		if (!table)
			return edj_cmd_error(cmd->where, "noDefTable:No default table");
	} else {
		/* Yes, so evaluate it.
		 *
		 * NOTE: It'd be nice if we could do this without copying the
		 * table, since some tables are large.  However, even a huge
		 * table can be copied in a fraction of a second, and this
		 * command is really only useful when used interactively.
		 * For this reason, I'm not going to bother trying to optimize
		 * this for simple expressions.
		 */
		table = edj_calc(cmd->calc, *refcontext, NULL);
		mustfree = table;
		expr = NULL;
	}

	/* Detect errors */
	if (edj_is_error(table)) {
		edjcmdout_t *out = edj_cmd_error(cmd->where, "%s", table->text);
		edj_free(table);
		return out;
	}
	if (!edj_is_table(table)) {

		edjcmdout_t *out = edj_cmd_error(cmd->where, "explainNotTable:Not a table");
		edj_free(table);
		return out;
	}

	/* Output the explain results, unless the parameter text was just "?" */
	columns = NULL;
	if (!cmd->var) {
		/* If it is a deferred array, then we might want to check only
		 * some of the rows.
		 */
		if (edj_is_deferred_array(table)) {
			int deferexplain = 0;
			edj_t *jc = edj_by_key(edj_config, "deferexplain");
			if (jc && jc->type == EDJ_NUMBER)
				deferexplain = edj_int(jc);
			if (deferexplain >= 1) {
				for (table = edj_first(table); deferexplain > 0 && table; deferexplain--, table = edj_next(table))
					columns = edj_explain(columns, table, 0);
				edj_break(table);
			}
		}

		/* Otherwise scan all rows */
		if (!columns) {
			for (table = edj_first(table); table; table = edj_next(table))
				columns = edj_explain(columns, table, 0);
		}

		/* If there's a search key, remove entries that don't match */
		if (columns && cmd->key) {
			edj_t *scan, *lag, *next, *key;

			/* Convert the requested key to a "LIKE" pattern */
			char like[strlen(cmd->key) + 3];
			strcpy(like, "%");
			strcat(like, cmd->key);
			strcat(like, "%");

			/* Scan the rows, if "key" doesn't match, remove it */
			for (scan = columns->first, lag = NULL; scan; scan = next) { /* undeferred */
				next = scan->next;
				key = edj_by_key(scan, "key");
				if (!edj_mbs_like(key->text, like)) {
					if (lag)
						lag->next = next;
					else
						columns->first = next;
					scan->next = NULL;
					edj_free(scan);
				} else {
					lag = scan;
				}
			}
		}

		/* Output it */
		edj_print(columns, NULL);
	}

	/* If we have an expr for the default table, output it */
	if (expr)
		edj_user_printf(NULL, "normal", "%s\n", expr);

	/* Clean up */
	if (columns)
		edj_free(columns);
	if (mustfree)
		edj_free(mustfree);
	if (expr)
		free(expr);

	/* Done! */
	return NULL;
}

static edjcmd_t *file_parse(edjsrc_t *src, edjcmdout_t **referr)
{
	const char	*end, *err;
	edjcmd_t *cmd;

	/* Allocate a cmd */
	cmd = edj_cmd(src, &jcn_file);

	/* Many possible ways to invoke this.  Most commands have a strict
	 * syntax, but file is intended mostly for interactive use and should
	 * be user-friendly.  The main ways to invoke it are:
	 *   file       List all files, with the current one highlighted
	 *   file +	Move to the next file
	 *   file -	Move to the previous file
	 *   file word	Move to the named file.  If new, append it.
	 *   file (expr)Move to the result of expr
	 */
	edj_cmd_parse_whitespace(src);
	if (!*src->str || *src->str == ';' || *src->str == '}') {
		/* "file" with no arguments */
	} else if (*src->str == '+' || *src->str == '-') {
		/* Verify that it's ONLY + or -, not part of a calc expression*/
		char ch[2];
		ch[0] = *src->str++;
		ch[1] = '\0';
		edj_cmd_parse_whitespace(src);
		if (*src->str && *src->str != ';' && *src->str != '}') {
			*referr = edj_cmd_error(src->str, "Bad use of \"%s\" or \"%s\"", "file+", "file-");
			edj_cmd_free(cmd);
			return NULL;
		}
		cmd->key = strdup(ch);
	} else if (*src->str == '(') {
		/* Get the expression */
		char *str = edj_cmd_parse_paren(src);
		if (!str) {
			*referr = edj_cmd_error(src->str, "Missing ) in \"%s\" expression", "file");
			return cmd;
		}

		/* Parse the expression */
		cmd->calc = edj_calc_parse(str, &end, &err, 0);
		if (err || *end || !cmd->calc) {
			free(str);
			if (err)
				*referr = edj_cmd_error(src->str, "%s", err);
			else
				*referr = edj_cmd_error(src->str, "Syntax error in \"%s\" expression", "file");
			return cmd;
		}
		free(str);
	} else {
		/* Assume it is a filename.  Find the end of it, and copy it
		 * into the cmd->key.
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

static edjcmdout_t *file_run(edjcmd_t *cmd, edjcontext_t **refcontext)
{
	edj_t *files, *elem;
	int	current = EDJ_CONTEXT_FILE_SAME;

	/* Determine what type of "file" invocation this is */
	if (cmd->calc) {
		/* "file (calc) -- Evaluate the expression. */
		edj_t *result = edj_calc(cmd->calc, *refcontext, NULL);

		/* If we got an error, then return the error */
		if (edj_is_error(result)) {
			edjcmdout_t *err = edj_cmd_error(cmd->where, "%s", result->text);
			edj_free(result);
			return err;
		}

		/* If it's a number, then select a file by index */
		if (result->type == EDJ_NUMBER) {
			current = edj_int(result);
			files = edj_context_file(*refcontext, NULL, 0, &current);
			edj_free(result);
		} else if (result->type == EDJ_STRING) {
			current = EDJ_CONTEXT_FILE_NEXT;
			files = edj_context_file(*refcontext, result->text, 0, &current);
			edj_free(result);
		} else {
			edj_free(result);
			return edj_cmd_error(cmd->where, "fileExpr:file expressions should return a number or string.");
		}
	} else if (!cmd->key) {
		/* "file" with no args -- display the current filename */
		files = edj_context_file(*refcontext, NULL, 0, &current);
	} else if (!strcmp(cmd->key, "+")) {
		/* "file +" -- Move to the next file in the list */
		current = EDJ_CONTEXT_FILE_NEXT;
		files = edj_context_file(*refcontext, NULL, 0, &current);
	} else if (!strcmp(cmd->key, "-")) {
		/* "file -" -- Move to the previous file in the list */
		current = EDJ_CONTEXT_FILE_PREVIOUS;
		files = edj_context_file(*refcontext, NULL, 0, &current);
	} else {
		/* "file filename" -- Move to the named file */
		current = EDJ_CONTEXT_FILE_NEXT;
		files = edj_context_file(*refcontext, cmd->key, 0, &current);
	}

	/* After all that, display the current file's name */
	elem = edj_by_index(files, current);
	files = edj_by_key(elem, "filename");
	if (!files || files->type != EDJ_STRING)
		edj_user_printf(NULL, "normal", "%s\n", "(no files)");
	else
		edj_user_printf(NULL, "normal", "%s\n", files->text);
	edj_break(files);
	edj_break(elem);

	/* Return success always */
	return NULL;
}


static edjcmd_t *import_parse(edjsrc_t *src, edjcmdout_t **referr)
{
	const char	*end;
	char	*filename, *enddir;
	FILE	*fp;
	edjcmd_t *code, *cmd;
	edjsrc_t start = *src;
	edjfile_t *srcfile;

	/* Parse the name. */
	edj_cmd_parse_whitespace(src);
	for (end = src->str; *end && *end != ';' && *end != '}' && *end != '\n'; end++){
	}
	while (end > src->str && end[-1] == ' ')
		end--;
	filename = malloc(end - src->str + 4);
	strncpy(filename, src->str, end - src->str);
	filename[end - src->str] = '\0';
	src->str = end;

	/* If the filename has no extension, then assume ".edj" */
	end = strrchr(filename, '/');
	if (end)
		end++;
	else
		end = filename;
	end = strchr(end, '.');
	if (!end)
		strcat(filename, ".edj");

	/* For security's sake, make sure the name doesn't start with "/"
	 * or contain "../"
	 */
	if (filename[0] == '/' || strstr(filename, "../")) {
		*referr = edj_cmd_error(start.str, "impunsafe:Unsafe file name to import: \"%s\"", filename);
		free(filename);
		return NULL;
	}

	/* If "import" is in a script, then the named file is probably
	 * relative to the script file.  Look for the script's directory
	 * name, and if it has one then look for the include file there.
	 * Otherwise, hope it's in the current directory.
	 */
	srcfile = edj_file_containing(src->str, NULL);
	if (srcfile && srcfile->isfile && (enddir = strrchr(srcfile->filename, '/')) != NULL) {
		size_t dirlen = (enddir - srcfile->filename) + 1;
		char *tmp = malloc(dirlen + strlen(filename) + 1);
		strncpy(tmp, srcfile->filename, dirlen);
		strcpy(tmp + dirlen, filename);
		if (access(tmp, F_OK) >= 0) {
			free(filename);
			filename = tmp;
		}
	}

	/* If the file doesn't exist or is unreadable, fail */
	fp = fopen(filename, "r");
	if (!fp) {
		if (errno == ENOENT)
			*referr = edj_cmd_error(start.str, "impnoex:Import file \"%s\" does not exist", filename);
		else
			*referr = edj_cmd_error(start.str, "impunrd:Import file \"%s\" is unreadable", filename);
		free(filename);
		return NULL;
	}
	fclose(fp);

	/* Load the file. If it contains any code other than function
	 * definitions, then stow it in a cmd to run later; this is necessary
	 * for declaring variables and constants, because those get stored in
	 * the context but we don't have a context at parse time.
	 */
	cmd = NULL;
	code = edj_cmd_parse_file(filename);
	free(filename);
	if (code && code != EDJ_CMD_ERROR) {
		cmd = edj_cmd(&start, &jcn_import);
		cmd->sub = code;
	}

	/* Move past the ';', if there is one */
	edj_cmd_parse_whitespace(src);
	if (*src->str == ';')
		src->str++;
	edj_cmd_parse_whitespace(src);

	/* Probably nothing left to do at runtime... except maybe vars */
	return cmd;
}

static edjcmdout_t *import_run(edjcmd_t *cmd, edjcontext_t **refcontext)
{
	/* Declare variables and such */
	return edj_cmd_run(cmd->sub, refcontext);
}


static edjcmd_t *plugin_parse(edjsrc_t *src, edjcmdout_t **referr)
{
	size_t len;
	char	quote;
	char	*settings;
	edj_t	*err, *section;

	/* Find the end of the command */
	edj_cmd_parse_whitespace(src);
	for (len = 0, quote = 0; src->str[len] && (quote || !strchr(";\n}", src->str[len])); len++) {
		/* Handle backslash in a quoted string */
		if (quote && src->str[len] == '\\' && src->str[len + 1])
			len++;

		/* Start/end quoting */
		if (src->str[len] == quote)
			quote = 0;
		else if (!quote && strchr("\"'`", src->str[len]))
			quote = src->str[len];
	}

	/* Make a temp copy of the arguments */
	char str[len + 1];
	strncpy(str, src->str, len);
	str[len] = '\0';
	src->str += len;

	/* Separate the plugin name from settings */
	settings = strchr(str, ',');
	if (settings) {
		*settings++ = '\0';
		while (*settings == ' ')
			settings++;
	} else
		settings = "";

	/* Load the plugin */
	err = edj_plugin_load(str);
	if (err) {
		if (err->first)
			*referr = edj_cmd_error((char *)err->first, "%s", err->text);
		else
			*referr = edj_cmd_error(src->str, "%s", err->text);
		return NULL;
	}

	/* Process the settings, if any */
	if (*settings) {
		/* Find where this plugins settings are stored */
		section = edj_by_key(edj_config, "plugin");
		section = edj_by_key(section, str);
		if (!section) {
			*referr = edj_cmd_error(src->str, "The \"%s\" plugin doesn't use settings", str);
			return NULL;
		}

		/* Adjust the settings */
		err = edj_config_parse(section, settings, NULL);
		if (err) {
			if (err->first)
				*referr = edj_cmd_error((char *)err->first, "%s", err->text);
			else
				*referr = edj_cmd_error(src->str, "%s", err->text);
			return NULL;
		}

	}

	/* No action needed at runtime */
	return NULL;
}

static edjcmdout_t *plugin_run(edjcmd_t *cmd, edjcontext_t **refcontext)
{
	/* Plugins are loaded at parse time, not run time */
	return NULL;
}

/* Print a value.  If it's a string, print it without quotes or backslashes. */
static edjcmd_t *print_parse(edjsrc_t *src, edjcmdout_t **referr)
{
	edjcmd_t *cmd;
	edjsrc_t start;
	edjcalc_t *item, *list;
	const char	*err;

	start = *src;
	edj_cmd_parse_whitespace(src);
	if (!*src->str || *src->str == ';' || *src->str == '}') {
		/* "print" with no arguments does nothing */
		return NULL;
	}

	/* Parse a comma-delimited list of expressions to output, as an
	 * array generator expression.
	 */
	list = NULL;
	err = NULL;
	do {
		item = edj_calc_parse(src->str, &src->str, &err, FALSE);
		if (!item || err || (*src->str && !strchr(";},", *src->str))) {
			if (list)
				edj_calc_free(list);
			*referr = edj_cmd_error(start.str, err ? err : "printSyntax:Syntax error in \"%s\" expression", "print");
			return NULL;
		}
		list = edj_calc_list(list, item);
	} while (*src->str++ == ',');

	/* Build the command */
	cmd = edj_cmd(&start, &jcn_print);
	cmd->calc = list;
	return cmd;
}

static edjcmdout_t *print_run(edjcmd_t *cmd, edjcontext_t **refcontext)
{
	edj_t	*list, *scan;
	char	lastchar;

	/* Evaluate the expression list */
	list = edj_calc(cmd->calc, *refcontext, NULL);

	/* If error, then return the error */
	if (edj_is_error(list)) {
		edjcmdout_t *err = edj_cmd_error(cmd->where, "%s", list->text);
		edj_free(list);
		return err;
	}

	/* Otherwise output the results, all strung together without any
	 * added spaces or anything.  For strings, output the string literally.
	 */
	lastchar = '\n';
	for (scan = edj_first(list); scan; scan = edj_next(scan)) {
		if (scan->type == EDJ_STRING) {
			edj_user_printf(NULL, "normal", "%s", scan->text, stdout);
			if (*scan->text)
				lastchar = scan->text[strlen(scan->text) - 1];
		} else {
			char *tmp = edj_serialize(scan, NULL);
			edj_user_printf(NULL, "normal", "%s", tmp);
			free(tmp);
			lastchar = 'x'; /* Never empty, never '\n' */
		}
	}

	/* If the last character wasn't a newline, remember that. */
	edj_print_incomplete_line = (lastchar != '\n');

	/* Clean up */
	edj_free(list);
	return NULL;
}

/* Set an option. */
static edjcmd_t *set_parse(edjsrc_t *src, edjcmdout_t **referr)
{
	edjcmd_t *cmd;
	edjsrc_t start;
	edjcalc_t *calc;
	char	*str;
	const char *end, *err;
	size_t	len;

	start = *src;
	edj_cmd_parse_whitespace(src);

	/* The options settings can be either explicit text, or a parenthesized
	 * expression that returns a string.
	 */
	if (*src->str == '(') {
		/* Parenthesized expression -- Get it in a string */
		str = edj_cmd_parse_paren(src);
		if (!str) {
			*referr = edj_cmd_error(src->str, "Missing ) in \"%s\" expression", "set");
			return NULL;
		}

		/* Parse it */
		calc = edj_calc_parse(str, &end, &err, 0);
		if (!calc || err || (*src->str && !strchr(";},", *src->str))) {
			free(str);
			if (err)
				*referr = edj_cmd_error(start.str, "%s", err);
			else
				*referr = edj_cmd_error(start.str, "setSyntax:Syntax error in \"%s\" expression", "set");
			return NULL;
		}

		/* CLean up */
		free(str);
		str = NULL;
	} else {
		/* Explicit text -- Parsing this is tricky because the parser
		 * wants to store the change immediately, but we want to save
		 * those changes until the command is actually run.
		 */
		for (len = 0; src->str[len] && !strchr(";\n{", src->str[len]); len++) {
		}
		str = malloc(len + 1);
		strncpy(str, src->str, len);
		str[len] = '\0';
		src->str += len;
		calc = NULL;
	}

	/* Build the command */
	cmd = edj_cmd(&start, &jcn_set);
	cmd->calc = calc;
	cmd->key = str;
	return cmd;
}

static edjcmdout_t *set_run(edjcmd_t *cmd, edjcontext_t **refcontext)
{
	edj_t *result, *section, *conferr;
	char	*str;

	/* Are we using an expression to generate the settings on-the-fly? */
	if (cmd->calc) {
		/* Evaluate it */
		result = edj_calc(cmd->calc, *refcontext, NULL);
		if (edj_is_error(result)) {
			edjcmdout_t *err = edj_cmd_error(cmd->where, "%s", result->text);
			edj_free(result);
			return err;
		}

		/* Value must be a string */
		if (result->type != EDJ_STRING) {
			edj_free(result);
			return edj_cmd_error(cmd->where, "setString:set expression must return a string");
		}

		/* Use the string's text */
		str = result->text;
	} else {
		/* Use the literal text */
		str = cmd->key;
		result = NULL;
	}

	/* Parse it, and store the changes.  */
	section = edj_by_key(edj_config, edj_text_by_key(edj_system, "runmode"));
	conferr = edj_config_parse(section, str, NULL);
	if (conferr) {
		edjcmdout_t *err = edj_cmd_error(cmd->where, "%s", conferr->text);
		edj_free(conferr);
		if (result)
			edj_free(result);
		return err;
	}

	/* Make the changes effective in the format */
	edj_format_set(NULL, NULL);

	/* Clean up */
	if (result)
		edj_free(result);
	return NULL;
}



/* Delete a variable, or part of a variable */
static edjcmd_t *delete_parse(edjsrc_t *src, edjcmdout_t **referr)
{
	edjcmd_t *cmd;
	edjsrc_t start;
	edjcalc_t *calc;
	const char *err;

	start = *src;
	edj_cmd_parse_whitespace(src);

	/* Parse the lvalue to delete */
	err = NULL;
	calc = edj_calc_parse(src->str, &src->str, &err, FALSE);
	if (!calc || err) {
		*referr = edj_cmd_error(src->str, "%s", err);
		return NULL;
	}
#if 0
	edj_cmd_parse_whitespace(src);
	if (!strncasecmp(src->str, "where", 5) && !isalnum(src->str[5])) {
		/* Parse the "where" condition */
		edjcalc_t *where = calloc(1, sizeof(edjcalc_t));
		where->op = EDJOP_WHERE;
		src->str += 5;
		where->u.param.left = calc;
		where->u.param.right = edj_calc_parse(src->str, &src->str, &err, FALSE);
		if (!where->u.param.right || err) {
			edj_free(where);
			edjcmdout_t *err = edj_cmd_error(cmd->where, "%s", err);
			return err;
		}

		/* Merge the lvalue and "where" into a single edjcalc_t */
		calc = where;
	}
	else
#endif
	if ((*src->str && !strchr(";},", *src->str))) {
		free(calc);
		*referr = edj_cmd_error(src->str, "deleteexpr:Bad expression for \"%s\"", "delete");
		return NULL;
	}

	/* Build the command */
	cmd = edj_cmd(&start, &jcn_delete);
	cmd->calc = calc;
	return cmd;
}

static edjcmdout_t *delete_run(edjcmd_t *cmd, edjcontext_t **refcontext)
{
	edj_t *result;

#if 0
	/* Which flavor of "delete" are we doing? */
	if (cmd->calc->op == EDJOP_WHERE) {
		/* SQL-like "delete table where test" */
	} else
#endif
	{
		/* Delete the expression */
		result = edj_context_delete(cmd->calc, *refcontext);
		if (result) {
			edjcmdout_t *err = edj_cmd_error(cmd->where, "delete:%s", result->text);
			edj_free(result);
			return err;
		}
	}

	return NULL;
}


/* Handle an assignment or output expression */
static edjcmdout_t *calc_run(edjcmd_t *cmd, edjcontext_t **refcontext)
{
	/* Calculate the result of the expression.   If it's an assignment,
	 * then this will do the assignment too.
	 */
	edj_t *result = edj_calc(cmd->calc, *refcontext, NULL);

	/* If we got an error ("null" with text), then convert to edjcmdout_t */
	if (edj_is_error(result)) {
		edjcmdout_t *err = edj_cmd_error(result->first ? (const char *)result->first : cmd->where, "%s", result->text);
		edj_free(result);
		return err;
	}

	/* If not an assignment, then it's an output.  Output it! */
	if (cmd->calc->op != EDJOP_ASSIGN
	 && cmd->calc->op != EDJOP_APPEND
	 && cmd->calc->op != EDJOP_MAYBEASSIGN) {
		/* Print the result */
		edj_print(result, NULL);

		/* Give the user interface a chance to save the result.  If
		 * it doesn't want to do that, then free it.
		 */
		if (!edj_user_result(result))
			edj_free(result);
	} else {
		/* For assignment, a copy of the result is already saved to
		 * we can discard it.
		 */
		edj_free(result);
	}

	return NULL;
}

/* src/plugin/line/syntax.c
 *
 * This file handles all matters related to edj script syntax, for line input:
 *  1) Syntax coloring
 *  2) Incomplete line detection
 *  3) Mismatched parentheses locating
 *  4) Hinting
 *  5) Name completion
 *
 * That's a lot!  It's all folded together because tokenizing and syntax drives 
 * so much of it.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <edj.h>
#include <fineline.h>
#include <ctype.h>
#include <assert.h>
#include "pluginline.h"

#define set_style(fine, src, len, style) set_style_line(fine, src, len, style, __LINE__)

/* These stores the parsing state. */
typedef struct varconst_s {
	struct varconst_s *next;
	int	isvar;
	int	isglobal;
	char	*simple;
	char	name[1];
} varconst_t;
typedef struct {
	/* This contains all info about the text we're styling */
	fineline_t *fine;
	edjcontext_t *context;

	/* Name at the cursor, or NULL if none */
	char	*container; /* name before the partial name, if known */
	char	*partialname;
	size_t	partiallen;

	/* Boolean indicating whether the command is incomplete */
	int	more;

	/* Boolean indicating whether we're in a global context */
	int	isglobal;

	/* This is a list of known variables and constants */
	varconst_t *vclist;

	/* These are used while styling an expression */
	int nest;	
	int selectnest;
	int curlynest;
	int maybevc;	/* vars/consts are allowed here */
	int afterdot;	/* next name must be a key or function */
	int beforecolon;/* next name must be a key if followed by colon */
	int mayberegex;	/* regular expressions are allowed here. */
} syntaxstate_t;

static void parse_command(syntaxstate_t *state, edjsrc_t *src);

static void slog(const char *fmt, ...)
{
	static FILE *log;
	va_list ap;

	if (!log)
		log = fopen("linesyntax.log", "w");
	if (log) {
		va_start(ap, fmt);
		vfprintf(log, fmt, ap);
		va_end(ap);
		fflush(log);
	}
}

/* Set the style for a segment of the line buffer.  If the cursor is located
 * in that segment, then ignore attempts to make it be "error".  Move src
 * past the end of the segment.
 *
 * NOTE: The fineline library will convert NULL to the previous type, so it
 * only requires the first character to be marked with the type.  However,
 * this syntax coloring module has its own requirements, which include setting
 * the style for the whole span.
 *
 * Returns the style name that was passed in.
 */
#if 0
static const char *set_style(fineline_t *fine, edjsrc_t *src, size_t len, const char *style)
#else
static const char *set_style_line(fineline_t *fine, edjsrc_t *src, size_t len, const char *style, int line)
#endif
{
	/* Find the offset of src, so we know the index into fine->style */
	size_t i = (src->str - src->buf), j;

slog("set_style: \"%.*s\" is %s at line %d\n", len, src->str, style, line);
	/* If the new style is "error" and the span contains the cursor,
	 * then don't do it.  Mostly this is so that a partially entered
	 * string isn't flagged as an error before the user even has a
	 * chance to finish it.
	 */
	if (i <= fine->cursor && fine->cursor <= i + len && !strcmp(style, "error")) {
		/* Move src past the segment even though we didn't style it. */
		src->str += len;
		return style;
	}

	/* Set the style */
	for (j = len; j-- > 0; )
		fine->style[i++] = style;

	/* Move src past the styled segment */
	src->str += len;
	return style;
}

/* SKip whitespace and comments */
static void skip(fineline_t *fine, edjsrc_t *src)
{
	size_t	len;

	/* Skip whitespace */
	edj_cmd_parse_whitespace(src);

	/* If comment, then style it and skip more whitespace */
	while (src->str[0] == '/' && src->str[1] == '/') {
		for (len = 2; src->str[len] && src->str[len] != '\n'; len++) {
		}
		set_style(fine, src, len, "comment");
		edj_cmd_parse_whitespace(src);
	}
}

/* Highlight mismatched parentheses */
static int mismatch(fineline_t *fine)
{
	size_t	i;
	size_t	*stack;
	size_t	stacksize, sp;

	/* Allocate the initial stack */
	stacksize = 100;
	stack = calloc(stacksize, sizeof *stack);
	sp = 0;

	/* Look for normal parentheses (not ones in strings, comments) */
	for (i = 0; fine->line[i]; i++) {
		/* Skip non-normal chars, such as parentheses in strings */
		if (fine->style[i] && strcmp(fine->style[i], "normal"))
			continue;

		switch (fine->line[i]) {
		case '(':
		case '[':
		case '{':
			/* add it to the stack */
			if (sp >= stacksize) {
				stacksize *= 2;
				stack = realloc(stack, stacksize * sizeof *stack);
			}
			stack[sp++] = i;
			break;
		case ')':
			if (fine->line[stack[sp - 1]] == '(')
				sp--;
			else
				fine->style[i] = "error";
			break;
		case ']':
			if (fine->line[stack[sp - 1]] == '[')
				sp--;
			else
				fine->style[i] = "error";
			break;
		case '}':
			if (fine->line[stack[sp - 1]] == '{')
				sp--;
			else
				fine->style[i] = "error";
			break;
		}
	}

	/* Mark anything left on the stack as a mismatch */
	i = sp;
	while (sp > 0)
		fine->style[stack[--sp]] = "mismatch";

	/* Clean up */
	free(stack);

	return i == 0;
}

/* Test for end-of-command markers */
static int end_cmd(char ch)
{
	return ch == '\0' || ch == ';' || ch == '}';
}


/* Search for a name, first in state->vclist and if not there, then as a global
 * variable in state->context.  If found, return it; else return NULL.  In the
 * case of a global from the context, it'll add the name to the vclist.
 */
varconst_t *find_var_const(syntaxstate_t *state, const char *key)
{
	varconst_t *vc;
	char	*simple = malloc(strlen(key) + 1);
	edjcontext_t *layer;

	/* First look for an exact match in locally declared vars */
	for (vc = state->vclist; vc; vc = vc->next)
		if (!strcmp(vc->name, key))
			return vc;

	/* Then look for a simple match */
	edj_mbs_simple_key(simple, key);
	for (vc = state->vclist; vc; vc = vc->next)
		if (!strcmp(vc->simple, simple))
			return vc;

	/* Then look in the context */
	if (edj_context_by_key(state->context, key, &layer)) {
		/* Found! Add it to state->vclist and return it. */
		vc = malloc(sizeof *vc + strlen(key));
		strcpy(vc->name, key);
		vc->simple = vc->name;
		vc->isvar = (layer->flags & EDJ_CONTEXT_VAR) != 0;
		vc->isglobal = 1;
		vc->next = state->vclist;
		state->vclist = vc;
		return vc;
	}

	/* Not found */
	return NULL;
}

/* Test whether a given word occurs at src, case-independently.  If so, return
 * its length; else return 0.
 */
static size_t match(const char *word, edjsrc_t *src)
{
	size_t len = strlen(word);
	if (strncasecmp(word, src->str, len))
		return 0;
	if (isalpha(*word) && isalnum(src->str[len])) /* not UTF-8 safe! */
		return 0;
	return len;
}

/* Count the length of a quoted string or regex.  This isn't fancy; it just
 * handles backslashes enough to allow the quote to be escaped.
 */
static size_t quote_length(edjsrc_t *src)
{
	size_t	len;

	for (len = 1; src->str[len] >= ' ' && src->str[len] != *src->str; len++) {
		if (src->str[len] == '\\' && src->str[len + 1] >= ' ')
			len++;
	}
	if (src->str[len] == *src->str)
		len++;
	return len;
}

/* Parse a token, advancing "src" past it.  Return the style name as const
 * string.  If it is a name then it returns "key" and also sets *refname to
 * a dynamic copy of the name.  Because strings may sometimes be used as keys,
 * it also sets *name to a copy for for strings but backslashes like \n are
 * not converted.
 */
static const char *parse_token(syntaxstate_t *state, edjsrc_t *src, char **refname)
{
	fineline_t *fine = state->fine;
	size_t	   len;
	int	   i;
	const char *start;

	/* This is a list of multi-character operators that require special
	 * processing.  Most operators are treated like any other punctuation.
	 * Longer operators are listed before short ones, and the words after
	 * SELECT are only special in a SELECT clause.
	 */
	const char *longop[] = {
		"...", "..", ".@",
		"##", "#^", "#$", "#=", "#<", "#>", "#/",
		"and", "or", "not",
		"is", /* "is null" and "is not null" */
		"between", "like", "in", "values", 
		"select", "distinct", "from", "where", "group by", "having", "order by", "descending", "desc", "limit",
		NULL
	};

	/* For now, assume no name will be copied into *refname */
	*refname = NULL;

	/* Maybe check for a regular expression */
	if (state->mayberegex && *src->str == '/') {
		/* Find the end */
		len = quote_length(src);

		/* Allow flags */
		while (isalpha(src->str[len]))
			len++;

		/* Style it and return "regex" */
		return set_style(fine, src, len, "regex");
	}

	/* Check for special operators */
	if (strchr(".#aionblvsdfwgh", *src->str)) {
		for (i = 0; longop[i]; i++) {
			len = match(longop[i], src);
			if (len > 0) {
				return set_style(fine, src, len, "normal");
			}

			/* Some are only special after "select" */
slog("SELECT: op=\"%s\", selectnest=%d, nest=%d\n", longop[i], state->selectnest, state->nest);
			if (!strcmp("select", longop[i]) && state->selectnest < state->nest)
				break;
		}
	}

	/* Strings */
	if (*src->str == '"' || *src->str == '\'') {
		len = quote_length(src);

		/* We want to store the string's contents at *refname, and then
		 * style the whole string including quotes.  Easy!  Unless
		 * it's a partially entered string with no end.
		 */
		if (len >= 2) {
			*refname = malloc(len - 1);
			if (len > 2)
				strncpy(*refname, src->str + 1, len - 2);
			(*refname)[len - 2] = '\0';
		} else {
			*refname = strdup("");
		}
		return set_style(fine, src, len, "string");
	}

	/* `-quoted names */
	if (*src->str == '`') {
		for (len = 1; src->str[len] && src->str[len] != '`'; len++) {
		}
		*refname = malloc(len);
		strncpy(*refname, src->str + 1, len -1);
		(*refname)[len] = '\0';
		if (src->str[len] == '`')
			len++;
		return set_style(fine, src, len, "key");
	}

	/* Numbers */
	if (isdigit(*src->str) || (*src->str == '-' && isdigit(src->str[1]))) {
		int	decimal = 0, exponent = 0;

		/* Count the length.  Allow one "." and one "e"/"E" */
		for (len = 1; isdigit(src->str[len]) || src->str[len] == '.' || src->str[len] == 'e' || src->str[len] == 'E'; len++) {
			if (src->str[len] == '.') {
				if (decimal || exponent)
					break;
				decimal = 1;
			}
			if (src->str[len] == 'e' || src->str[len] == 'E') {
				if (exponent)
					break;
				exponent = 1;
			}
		}

		/* Style it */
		return set_style(fine, src, len, "number");
	}

	/* Any other punctuation is treated like an operator.  Some of these
	 * will actually be parts of multi-character operators, but that
	 * doesn't affect styling.
	 */
	if ((*src->str & 0x80) == 0 && ispunct(*src->str))
		return set_style(fine, src, 1, "normal");

	/* Check for true, false, null */
	len = match("true", src);
	if (!len)
		len = match("false", src);
	if (!len)
		len = match("null", src);
	if (len)
		return set_style(fine, src, len, "symbol");

	/* Anything else is likely to be a name */
	start = src->str;
	*refname = edj_cmd_parse_key(src, 0);
	len = src->str - start;
	src->str = start;
	while (len > 0 && src->str[len - 1] == ' ')
		len--;
	if (*refname)
		return set_style(fine, src, len, "key");

	/* Don't know what it is, really, so just skip it */
	return set_style(fine, src, 1, "normal");
}

/* Parse an expression.  Mostly this means splitting it into tokens and styling
 * them individually, but there are some complications:
 *   - We need to pair up parentheses so we can detect the end of an expression.
 *     In particular, some commands use an expression in parentheses, and we
 *     want to end it at the ")".
 *   - Some operators have word names: "in", "like", etc.  The clause markers
 *     after SELECT are only special after SELECT.  Quoted names are never
 *     operators: `in`, `like`, etc.
 *   - We want to style vars, consts, and keys differently.  This often means
 *     looking up names in either state->vclist or in state->fine->context.
 *   - Syntax says some names must be keys: before ":" in an object generator,
 *     or after a ., .., or .@ operator.
 *   - Syntax also says where a regular expression should be allowed: As an
 *     argument to a function, after LIKE.
 */
static void parse_expression(syntaxstate_t *state, edjsrc_t *src)
{
	size_t len;
	const char *start;
	const char *token;
	char *name;
	varconst_t *vc;
	fineline_t *fine = state->fine;
slog("parse_expression(state, \"%.15s\")\n", src->str);

	/* Initialize the state fields that affect expression parsing */
	state->nest = 0;
	state->selectnest = -1;
	state->afterdot = 0;
	state->mayberegex = 1;

	/* Parse a series of tokens.  A few have special meaning */
	skip(fine, src);
assert(*src->str != ';' && *src->str != '}');
	while ((state->nest > 0 && *src->str) || !end_cmd(*src->str)) {
		/* Fetch and style the next token */
		start = src->str;
		token = parse_token(state, src, &name);
		len = src->str - start;
		while (len > 0 && start[len - 1] == ' ')
			len--;
slog("token=%s len=%d start=\"%.*s\" name=%s nest=%d\n", token, (int)len, (int)len, start, name, state->nest);

		/* Regex can happen after "(" or "," */
		state->mayberegex = (*start == '(' || *start == ',');

		/* Check for nested parentheses */
		if (*start == '(') {
			state->nest++;
		} else if (*start == ')') {
			if (state->nest == 0) {
				src->str = start;
				return;
			}
			state->nest--;
			if (state->nest < state->selectnest)
				state->selectnest = -1;
		}

		/* The SELECT token enables other tokens like FROM and WHERE */
		if (token == "normal" && !strncasecmp(start, "select", 6))
			state->selectnest = state->nest;

		/* Check for curly braces.  In curlies, we expect each comma-
		 * delimited clause to start with "key:".
		 */
		if (*start == '{') {
			state->nest++;
			state->curlynest = state->nest;
			state->beforecolon = 1;
		} else if (*start == '}') {
			state->nest--;
		} else if (*start == ',' && state->nest == state->curlynest) {
			state->beforecolon = 1;
		}

		/* Some tokens imply that the next name must be a key */
		if (!strcmp(token, "normal") && (*start == '.' || (len == 2 && !strncasecmp(start, "as", 2))))
			state->afterdot = 1;

		/* If it's a name, try to determine whether it is a const/var,
		 * key, or function.  Strings also get mixed in here because
		 * they can be used as quoted keys.
		 */
		if (name) {
slog("Looking up \"%s\" in expression\n", name);
			if (*src->str == '(' && !strcmp(token, "key")) {
				/* Name followed by '(' is a function.  If not
				 * a known function, that's an error.
				 */
				src->str = start;
				token = edj_calc_function_by_name(name) ? "function" : "error";
				set_style(fine, src, len, token);
			} else if (state->afterdot) {
				/* name after "." is a key */
				if (*token != 'k') {
					src->str = start;
					set_style(fine, src, len, "key");
				}
			} else if (state->beforecolon && *src->str == ':') {
				/* name followed by ":" in curlies is a key */
				if (*token != 'k') {
					src->str = start;
					set_style(fine, src, len, "key");
				}
			} else if (*token != 'k') {
				/* Leave it as a string */
			} else {
				/* Try looking up the name as a var/const */
				vc = find_var_const(state, name);
				if (vc) {
					/* It's a var or const */
					token = vc->isvar ? "var" : "const";
					src->str = start;
					set_style(fine, src, len, token);
				}
				/* else it should remain a key */
			}

			/* clean up */
			free(name);
		}

		/* Allow whitespace/comments between tokens */
		skip(fine, src);
	}
}

/* Parse var/const/args declarations.  Add them to the list of known names.
 * Returns 0 normally, or 1 on error.
 */
static int var_const(syntaxstate_t *state, edjsrc_t *src, int isvar)
{
	varconst_t *vc;
	fineline_t *fine = state->fine; /* save typing */
	edjsrc_t start;
	char	*name;

	/* comma-delimited series of names, :types, and =expr */
	skip(fine, src);
	while (!end_cmd(*src->str) && *src->str != ')') {
		/* Name */
		start = *src;
		name = edj_cmd_parse_key(src, 0);
		if (!name)
			return 1; /* No variable name */
		set_style(fine, &start, src->str - start.str, isvar ? "var" : "const");

		/* Add the name to the list of known names */
		vc = malloc(sizeof *vc + strlen(name) * 2);
		vc->isglobal = state->isglobal;
		vc->isvar = isvar;
		strcpy(vc->name, name);
		vc->simple = vc->name + strlen(name) + 1;
		edj_mbs_simple_key(vc->simple, name);
		vc->next = state->vclist;
		state->vclist = vc;
		free(name);

		/* Optional type */
		if (*src->str == ':') {
			start = *src;
			edj_cmd_parse_whitespace_or_type(&start, &name);
slog("type name: \"%s\"\n", name);
			if (!name)
				return 1; /* missing type name */
			if (name) {
				/* Skip the colon */
slog("%d: src->str=\"%s\"\n", __LINE__, src->str);
				set_style(fine, src, 1, "normal");
slog("%d: src->str=\"%s\"\n", __LINE__, src->str);
				edj_cmd_parse_whitespace(src);
slog("%d: src->str=\"%s\"\n", __LINE__, src->str);

				/* Set the style of the type */
				set_style(fine, src, strlen(name), "type");

				/* Clean up */
				free(name);
			}
slog("%d: src->str=\"%s\"\n", __LINE__, src->str);
			skip(fine, src);
slog("%d: src->str=\"%s\"\n", __LINE__, src->str);
		}

		/* Optional initial value */
slog("%d: src->str=\"%s\"\n", __LINE__, src->str);
		if (*src->str == '=') {
			set_style(fine, src, 1, "normal");
slog("%d: src->str=\"%s\"\n", __LINE__, src->str);
			parse_expression(state, src);
slog("%d: src->str=\"%s\"\n", __LINE__, src->str);
		}

		/* If not a comma, we should be done */
		if (*src->str != ',')
			break;
		set_style(fine, src, 1, "normal");
		skip(fine, src);
	}
	return 0;
}

/* Parse a single command, or a {...} series of commands */
static void parse_block(syntaxstate_t *state, edjsrc_t *src)
{
	edjsrc_t start;
	skip(state->fine, src);
	start = *src;
slog("parse_block(\"%.*s...\")\n", 20, src->str);
	if (*src->str == '{') {
		/* Do a block */
		set_style(state->fine, src, 1, "normal");
		skip(state->fine, src);
		while (*src->str && *src->str != '}') {
			parse_command(state, src);
			skip(state->fine, src);
		}
		if (*src->str == '}')
			set_style(state->fine, src, 1, "normal");
	} else {
		/* Do a single command */
		parse_command(state, src);
	}
slog("parse_block(...) found <<%.*s>>\n", src->str - start.str, start.str);
}

/* Parse a single command.  Most commands start with a command name; the only
 * exceptions are assignments and output expressions, both of which are parsed
 * as expressions.  Each command has its own syntax, and hence its own parser.
 * function-as-command is more uniform.
 */
static void parse_command(syntaxstate_t *state, edjsrc_t *src)
{
	char	*name, *tmp;
	edjcmdname_t *cmd;
	edjfunc_t *func;
	edjsrc_t start;
	size_t	len;
	fineline_t *fine = state->fine;
	varconst_t *vc;

	/* Try to get a command name.  If no name, assume it's an expression */
	start = *src;
	name = edj_cmd_parse_key(src, 0);
	if (!name) {
		*src = start;

		/* Empty commands are allowed too */
		if (!end_cmd(*src->str))
			parse_expression(state, src);
		if (*src->str == ';')
			set_style(fine, src, 1, "normal");
		return;
	}

	/* Is it really a command name? */
	cmd = edj_cmd_by_name(name);

	/* Is it a function name not followed by "("? */
	if (!cmd && *src->str != '(' && edj_calc_function_by_name(name)) {
		/* Treat it like the "print" command */
		cmd = edj_cmd_by_name("print");
	}
	free(name);

	/* When all else fails, assume it's an expression */
	if (!cmd) {
		*src = start;
		if (!end_cmd(*src->str))
			parse_expression(state, src);
		if (*src->str == ';')
			set_style(fine, src, 1, "normal");
		return;
	}

	/* Style the command name as a command name */
	set_style(fine, &start, src->str - start.str, "command");

	/* Parse the arguments, depending on the command */
slog("Doing command \"%s\"\n", cmd->name);
	if (!strcmp(cmd->name, "break")
	  ||!strcmp(cmd->name, "continue")
	  ||!strcmp(cmd->name, "pwd")) {
		/* No arguments */
	} else if (!strcmp(cmd->name, "default")) {
		/* No arguments, ends with ":" */
		skip(fine, src);
		if (*src->str == ':')
			src->str++;
		else
			set_style(fine, src, 1, "error");
		return;
	} else if (!strcmp(cmd->name, "case")) {
		/* Expression (literal really) */
		parse_expression(state, src);
		if (*src->str == ':')
			src->str++;
		else
			set_style(fine, src, 1, "error");
		return;
	} else if (!strcmp(cmd->name, "const")
		|| !strcmp(cmd->name, "var")
		|| !strcmp(cmd->name, "let")) {
		if (var_const(state, src, cmd->name[0] != 'c'))
			goto Recover;
	} else if (!strcmp(cmd->name, "delete")
		|| !strcmp(cmd->name, "void")
		|| !strcmp(cmd->name, "return")) {
		/* Any single expression */
		parse_expression(state, src);
	} else if (!strcmp(cmd->name, "for")) {
		char *type;

		/* Expect parentheses */
		skip(fine, src);
		if (*src->str != '(')
			goto Recover;
		set_style(fine, src, 1, "normal");
		skip(fine, src);

		/* Optional "var" or "const" */
		type = "var";
		if (match("var", src)) {
			type = "var";
			set_style(fine, src, 3, "command");
		} else if (match("const", src)) {
			type = "const";
			set_style(fine, src, 5, "command");
		}
		skip(fine, src);

		/* Optional "name=" or "name of". */
		start = *src;
		name = edj_cmd_parse_key(src, 0);
		if (name) {
			if ((*src->str == '=' && src->str[1] != '=') || match("of", src)) {
				/* Style the name */
				if (type) {
					set_style(fine, &start, src->str - start.str, type);
					/* Register the name, temporarily */
					vc = malloc(sizeof *vc + strlen(name) * 2);
					vc->isglobal = state->isglobal;
					vc->isvar = (*type == 'v');
					strcpy(vc->name, name);
					vc->simple = vc->name + strlen(name) + 1;
					edj_mbs_simple_key(vc->simple, name);
					vc->next = state->vclist;
					state->vclist = vc;
				} else if ((vc = find_var_const(state, name)) != NULL && vc->isvar) {
					set_style(fine, &start, src->str - start.str, "var");
				} else {
					set_style(fine, &start, src->str - start.str, "error");
				}

				/* Style the "=" or "of" */
				set_style(fine, src, *src->str == '=' ? 1 : 2, "normal");

			}
			free(name);
			if (!type)
				name = NULL;
		}
		/* NOTE: At this point, "name" is either NULL or it points to
		 * freed memory.  That's useful!  Non-NULL means we need to
		 * free a variable from state->vclist later.
		 */

		/* Expression */
		parse_expression(state, src);

		/* Expect ")" */
		skip(fine, src);
		if (*src->str != ')')
			goto Recover;
		set_style(fine, src, 1, "normal");
		skip(fine, src);

		/* Expect the body of the loop */
		parse_block(state, src);

		/* If we allocated a loop variable, free it */
		if (name) {
			vc = state->vclist;
			state->vclist = state->vclist->next;
			free(vc);
		}
	} else if (!strcmp(cmd->name, "function")) {
		/* Expect a function name.  Style it like a function name even
		 * if it isn't known yet.
		 */
		state->isglobal = 0;
		skip(fine, src);
		start = *src;
		name = edj_cmd_parse_key(src, 0);
		if (name) {
			free(name);
			set_style(fine, &start, src->str - start.str, "function");
			skip(fine, src);

			/* Expect parentheses and arguments */
			if (*src->str == '(') {
				set_style(fine, src, 1, "normal");
				if (var_const(state, src, 0) || *src->str != ')')
					goto Recover;
				set_style(fine, src, 1, "normal");

				/* Allow a return type */
				skip(fine, src);
				if (*src->str == ':') {
					start = *src;
					edj_cmd_parse_whitespace_or_type(src, NULL);
					set_style(fine, &start, src->str - start.str, "type");
				}

				/* Expect a body */
				skip(fine, src);
				if (*src->str == '{')
					parse_block(state, src);
			}
		}
		state->isglobal = 1;

	} else if (!strcmp(cmd->name, "if")
		|| !strcmp(cmd->name, "while")
		|| !strcmp(cmd->name, "switch")) {
		/* Parenthesized boolean expression */
		skip(fine, src);
		if (*src->str != '(')
			goto Recover;
		set_style(fine, src, 1, "normal");
		parse_expression(state, src);
		if (*src->str != ')')
			goto Recover;
		set_style(fine, src, 1, "normal");

		/* Single command or block */
		parse_block(state, src);

		/* "if" may be followed by "else" and another block */
		if (!strcmp(cmd->name, "if") && match("else", src)) {
			set_style(fine, src, 4, "command");
			parse_block(state, src);
		}
	} else if (!strcmp(cmd->name, "print")) {
		/* Comma-delimited series of expressions */
		skip(fine, src);
		while (!end_cmd(*src->str)) {
			parse_expression(state, src);
			skip(fine, src);
			if (*src->str == ',')
				src->str++;
			else {
				for (len = 1; !end_cmd(src->str[len]); len++) {
				}
				set_style(fine, src, len, "error");
				break;
			}
		}
	} else if (!strcmp(cmd->name, "throw")) {
		/* First arg must be a string */
		skip(fine, src);
		if (strcmp("string", parse_token(state, src, &name))) {
			if (name)
				free(name);
			goto Recover;
		}

		/* If the string contains "%s" then there must be a second
		 * arg which is an expression.  Otherwise there can't be a
		 * second arg.
		 */
		tmp = strstr(name, "%s");
		if (tmp) {
			/* Expect a comma and expression */
			if (*src->str != ',') {
				start = *src;
				start.str = tmp;
				set_style(fine, &start, 2, "error");
				goto Recover;
			}
			set_style(fine, src, 1, "normal");
			parse_expression(state, src);
		}

		/* If not the end of a command, then error. */
		if (!end_cmd(*src->str))
			goto Recover;
	} else if (!strcmp(cmd->name, "try")) {
		/* Expect a command or block */
		parse_block(state, src);

		/* May be followed by multiple "catch" clauses */
		while (match("catch", src)) {
			set_style(fine, src, 5, "command");
			skip(fine, src);
			if (*src->str == '(') {
				name = edj_cmd_parse_paren(src);
				if (!name)
					goto Recover;
				free(name);
			}
			parse_block(state, src);
		}
	} else {
		/* Just whatever */
Recover:
		for (len = 0; !end_cmd(src->str[len]); len++) {
		}
		set_style(fine, src, len, "normal");
	}

	/* Skip the terminal ";" if any */
	skip(fine, src);
	if (*src->str == ';')
		src->str++;
	return;

}

/* Colorize a line buffer, and return 1 if <Enter> should process it, or 0 if
 * <Enter> should just add a newline.
 */
int line_syntax(fineline_t *fine)
{
	edjsrc_t src[1];
	size_t	len;
	syntaxstate_t state;

slog("\n----------------------------------------\n%s\n", fine->line);
	/* Start scanning */
	src->buf = fine->line;
	src->str = fine->line;
	src->size = strlen(fine->line);
	src->nocomment = 1;
	memset(&state, 0, sizeof state);
	state.fine = fine;
	state.context = (edjcontext_t *)fine->context;

	/* If the first line starts with "#!", that's a comment */
	if (src->str[0] == '#' && src->str[1] == '!') {
		for (len = 2; src->str[len] && src->str[len] != '\n'; len++) {
		}
		set_style(fine, src, len, "comment");
	}

	/* Check whether the cursor is at the end of a partial name */

	/* Parse a series of commands */
	skip(fine, src);
	while (*src->str) {
		parse_command(&state, src);
		skip(fine, src);
	}

	/* Highlight mismatched parentheses */
	state.more |= mismatch(fine);

	/* If the cursor is at the end of a name, try name completion */

{ int i; for (i = 0; fine->line[i]; i++) slog("%c %s\n", fine->line[i], fine->style[i]); }
{ varconst_t *vc; for (vc = state.vclist; vc; vc = vc->next) slog("declared %s %s %s\n", vc->isglobal ? "global" : "local", vc->isvar ? "var" : "const", vc->name);}

	/* Free the state's resources */
	while (state.vclist) {
		varconst_t *doomed = state.vclist;
		state.vclist = state.vclist->next;
		free(doomed);
	}

	/* Return a boolean indicator of whether input is complete */
	return state.more;
}

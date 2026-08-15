#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <edj.h>

/* This type is used only in this file to store a private list of the
 * registers table formats.
 */
typedef struct jctablefmt_s {
	struct jctablefmt_s *other;
	char *name;
	void (*fn)(edj_t *json, edjformat_t *format);
} jctablefmt_t;

/* This flag is used to terminate processing, generally in response to a
 * <Ctrl-C> being pressed while running in interactive mode.
 */
int edj_interrupt;

/* Print an edj_t tree as JSON text.  "format" controls the format.  */
static void jcprint(edj_t *json, int indent, edjformat_t *format)
{
	edj_t	*scan;
	char	*str;

	/* output the indent */
	if (indent > 0 && (format->pretty || format->elem))
		edj_user_printf(format, "result", "%*s", indent, "");
	else
		edj_user_printf(format, "result", ""); /* just to set color */

	/* If this is a key, then output the key and switch to its value */
	scan = json;
	if (json->type == EDJ_KEY)
	{
                edj_user_ch('"');
                for (str = json->text; *str; str++) {
			switch (*str) {
			case '"':
			case '\\':
				edj_user_ch('\\');
				edj_user_ch(*str);
				break;
			case '\'':
				/* For "sh" format, the entire output is
				 * enclosed in ' quotes which is great for
				 * everything except the ' character itself.
				 * For that, we need to end the quote, add a
				 * backslash-', and start a new quote.
				 */
				if (format->sh) {
					edj_user_ch('\'');
					edj_user_ch('\\');
					edj_user_ch('\'');
				}
				edj_user_ch('\'');
				break;
			case '\n':
				edj_user_ch('\\');
				edj_user_ch('n');
				break;
			default:
				edj_user_ch(*str);
			}
		}
                edj_user_ch('"');
                edj_user_ch(':');
		scan = json->first;
	}

	switch (scan->type)
	{
	  case EDJ_OBJECT:
                edj_user_ch('{');
		if (format->pretty || format->elem) {
                        edj_user_ch('\n');
                        for (scan = scan->first; scan; scan = scan->next) { /* object */
                                jcprint(scan, indent + format->tab, format);
			}
                        if (indent > 0)
                                edj_user_printf(format, "result", "%*s", indent, "");
                } else {
                        for (scan = scan->first; scan; scan = scan->next) { /* object */
                                jcprint(scan, 0, format);
			}
                }
                edj_user_ch('}');
		break;

	  case EDJ_ARRAY:
		edj_user_ch('[');
		if (format->pretty || format->elem) {
			edjformat_t byelem;
			edj_user_ch('\n');
			byelem = *format;
			if (format->elem) {
				byelem.tab = 0;
				byelem.pretty = 0;
				byelem.elem = 0;
			}
                        for (scan = edj_first(scan); scan && !edj_interrupt; scan = edj_next(scan)) {
                                if (format->elem && indent + format->tab > 0)
					edj_user_printf(format, "result", "%*c", indent + format->tab, ' ');
                                jcprint(scan, indent + format->tab, &byelem);
				if (format->elem)
					edj_user_ch('\n');
			}

			/* If we didn't finish scanning a deferred array, then
			 * we may need to do extra cleanup.
			 */
			edj_break(scan);
                        if (indent > 0)
                                edj_user_printf(format, "result", "%*s", indent, "");
                } else {
                        for (scan = edj_first(scan); scan && !edj_interrupt; scan = edj_next(scan)) {
                                jcprint(scan, indent + format->tab, format);
			}
		}
		if (scan)
			edj_break(scan);
		edj_user_ch(']');
		break;

	  case EDJ_STRING:
		str = edj_serialize(scan, format);
		edj_user_printf(format, "result", "%s", str);
		free(str);
		break;

	  case EDJ_NUMBER:
		/* could be binary int or double, or it could be text */
		if (scan->text[0] == '\0' && scan->text[1] == 'i')
			edj_user_printf(format, "result", "%d", EDJ_INT(scan));
		else if (scan->text[0] == '\0' && scan->text[1] == 'd')
			edj_user_printf(format, "result", "%.*g", format->digits, EDJ_DOUBLE(scan));
		else
			edj_user_printf(format, "result", "%s", scan->text);
		break;

	  case EDJ_BOOLEAN:
		edj_user_printf(format, "result", "%s", scan->text);
		break;

	  case EDJ_NULL:
		edj_user_printf(format, "result", "null");
		break;

	  default:
	  	; /* shouldn't happen */
	}

	if (!edj_is_last(json))
		edj_user_ch(',');
	if (format->pretty || format->elem)
		edj_user_ch('\n');
}

/* Output each row of a table as a line containing a series of name=value pairs */
static void jcsh(edj_t *json, edjformat_t *format){
	edj_t	*row;
	edj_t	*col;
	char	*s, *t, *frees;

	for (row = edj_first(json); row && !edj_interrupt; row = edj_next(row)) {
		for (col = row->first; col; col = col->next) { /* object */
			/* Output the prefix, name, and an = */
			edj_user_printf(format, "result", "%s%s=", format->prefix, col->text);

			/* Get the value */
			frees = NULL;
			if (col->first->type == EDJ_STRING)
				s = col->first->text;
			else if (edj_is_null(col->first))
				s = format->null;
			else
				s = frees = edj_serialize(col->first, format);

			/* Does it need quotes? */
			for (t = s; *t; t++)
				if (!isalnum(*t) && *t != '.' && *t != '-')
					break;

			/* Output it, maybe with quotes */
			if (*t)
				edj_user_ch('\'');
			for (; *s; s++) {
				if ((unsigned char)*s < ' ' || *s == '\177')
					; /* omit all control characters */
				else if (*s == '\'') {
					/* To output a ' we must end quoting,
					 * output \' and start new quoting.
					 */
					edj_user_ch('\'');
					edj_user_ch('\\');
					edj_user_ch('\'');
					edj_user_ch('\'');
				} else if ((*s & 0x80) != 0 && format->ascii) {
					char buf[13], *c;
					s = (char *)edj_mbs_ascii(s, buf);
					s--; /* because for-loop does s++ */
					for (c = buf; *c; c++)
						edj_user_ch(*c);
				} else
					edj_user_ch(*s);
			}
			if (*t)
				edj_user_ch('\'');

			/* If supposed to free it, do that */
			if (frees)
				free(frees);

			/* If not the last, then output a space */
			if (col->next) /* object */
				edj_user_ch(' ');
		}
		edj_user_ch('\n');
	}

	/* If we stopped before the end of a deferred array, there could be
	 * extra cleanup.
	 */
	if (row)
		edj_break(row);
}


/* When writing to stdout, this line indicates whether the last character
 * written was a newline.  This matters ONLY because the GNU readline()
 * function misbehaves if the cursor doesn't start at the begining of a line.
 * Dependence on this variable isn't threadsafe, but neither is readline().
 */
int edj_print_incomplete_line;

/* This stores the list of possible table formats, other than JSON */
static jctablefmt_t tablesh = {NULL, "sh", jcsh};
static jctablefmt_t tablegrid = {&tablesh, "grid", edj_grid};
static jctablefmt_t *tablefmts = &tablegrid;

void edj_print_table_hook(char *name, void (*fn)(edj_t *json, edjformat_t *format)) {
	jctablefmt_t *t;
	edj_t	*list;

	/* Scan to see if this format is already in the list */
	for (t = tablefmts; t; t = t->other) {
		if (!edj_mbs_casecmp(name, t->name)) {
			/* Yes, we know it.  Just change the function pointer */
			t->fn = fn;
			return;
		}
	}

	/* It's new.  Add it to the list */
	t = malloc(sizeof(jctablefmt_t));
	t->name = name;
	t->fn = fn;
	t->other = tablefmts;
	tablefmts = t;

	/* Also add it to the list of preferred values for config "table". */
	list = edj_by_expr(edj_config, "interactive.\"table-list\"", NULL, NULL, NULL);/* undeferred */
	edj_append(list, edj_string(name, -1));
	list = edj_by_expr(edj_config, "batch.\"table-list\"", NULL, NULL, NULL);/* undeferred */
	edj_append(list, edj_string(name, -1));
}

/* Print an edj_t tree as JSON text.  "format" is a combination of values from
 * the EDJ_FORMAT_XXXX macros, most importantly EDJ_FORMAT_INDENT(n).
 * Returns 1 if the output did NOT end with a newline, or 0 if it did.
 */
void edj_print(edj_t *json, edjformat_t *format)
{
	edjformat_t tweaked;
	jctablefmt_t *t;

	/* If NULL pointer then don't print anything (not even "null") */
	if (!json)
		return;

	/* Create a tweakable copy of the format */
	if (!format)
		format = &edj_format_default;
	tweaked = *format;
	if (tweaked.fp == NULL)
		tweaked.fp = stdout;

	/* Maybe output error messages embedded in "null" */
	if (tweaked.errors && json->type == EDJ_NULL && *json->text) {
		edj_user_printf(&tweaked, "error", "%s\n", json->text);
		/* ... but continue to output "null" too */
	}

	/* Maybe output strings unadorned (and without adding a newline) */
	if (tweaked.string && json->type == EDJ_STRING) {
		size_t len;
		edj_user_printf(&tweaked, "normal", "%s", json->text);
		len = strlen(json->text);
		if (tweaked.fp == stdout && len > 0)
			edj_print_incomplete_line = (len > 0 && json->text[len - 1] != '\n');
		return;
	}

	/* Maybe treat short like compact */
	if (tweaked.oneline > 0 && edj_is_short(json, tweaked.oneline))
	        tweaked.oneline = tweaked.pretty = 0;

	/* If quoting for the shell, disable pretty */
	if (tweaked.sh)
	        tweaked.oneline = tweaked.pretty = 0;

	/* Table output? */
	if (edj_is_table(json)){
		/* Scan for this format */
		for (t = tablefmts; t; t = t->other) {
			/* If not the one we want, keep looking */
			if (edj_mbs_casecmp(t->name, tweaked.table))
				continue;

			/* Use this method to format the table */
			t->fn(json, &tweaked);

			/* Table output always ends with a newline */
			if (tweaked.fp == stdout)
				edj_print_incomplete_line = 0;
			return;
		}
	}

	/* Output it as JSON, possibly "pretty" */
	if (tweaked.sh)
		edj_user_printf(&tweaked, "result", "'"); /* set color too */
	jcprint(json, 0, &tweaked);
	if (tweaked.sh)
		edj_user_printf(&tweaked, "result", "'"); /* set color too */

	/* "Pretty" mode always ends with a newline, but for non-"pretty"
	 * we want to add a newline now.
	 */
	if (!tweaked.pretty)
		edj_user_printf(&tweaked, "normal", "\n");
	if (tweaked.fp == stdout)
		edj_print_incomplete_line = 0;
}

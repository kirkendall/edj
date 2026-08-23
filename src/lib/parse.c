#include <sys/mman.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include <edj.h>

/* Here we need to access the "real" parse functions */
#ifdef EDJ_DEBUG_MEMORY
# undef edj_parse_string
#endif

/* This data type is used only in this file to track the list of registered
 * table formats.
 */
typedef struct edjparser_s {
	struct edjparser_s *other;
	const char	*name;
	int	(*tester)(const char *str, size_t len);
	edj_t	*(*parser)(const char *str, size_t len, const char **refend, const char **referr);
	int	(*updater)(edj_t *data, const char *filename);
} edjparser_t;

static edj_t *parseJSON(const char *str, size_t len, const char **refend, const char **referr, int allowdefer);

/******************************************************************************/
/* This next section of code is all in support of deferred arrays.            */

/* This is used to store the details of a deferred array */
typedef struct {
	edjdef_t basic; /* normal stuff */
	const char *start;/* position within that file where array starts */
	const char *end;  /* where it ends */
} jdefarray_t;

/* Parse the first element of the array, and return it.  This also involves
 * making a copy of the array's EDJ_DEFER node and related data.
 */
static edj_t *jdefarray_first(edj_t *array)
{
	/* Parse the first element.  Deferred arrays always have at least one
	 * element.
	 */
	jdefarray_t *def = (jdefarray_t *)array->first;
	jdefarray_t *nextdef;
	const char *next;
	edj_t *elem = parseJSON(def->start, (def->end - def->start), &next, NULL, 0);

	/* Make its "->next" point to a copy of "def" with its "->start"
	 * pointing to the next element's position in the data source code.
	 * Note that we don't copy basic.file because files' ref counts are
	 * maintained per deferred array, not per deferred element.
	 */
	elem->next = edj_defer(def->basic.fns);
	nextdef = (jdefarray_t *)elem->next;
	nextdef->basic.fns = def->basic.fns;
	nextdef->start = next;
	nextdef->end = def->end;

	/* Return the element. */
	return elem;
}

/* Parse the next element of the array and return it.  This also frees the
 * previous element but reuses its EDJ_DEFER node.  If there is no next
 * element then also free the EDJ_DEFER node and return NULL.
 */
static edj_t *jdefarray_next(edj_t *elem)
{
	jdefarray_t *def = (jdefarray_t *)elem->next;
	const char *next;

	/* Parse the next element.  If none, then return NULL and trust the
	 * edj_next() function (which calls this) to do the cleanup.
	 */
	edj_t *nextelem = parseJSON(def->start, (def->end - def->start), &next, NULL, 0);
	if (!nextelem)
		return NULL;

	/* Reuse the "def" with the next element, tweaking its "start" to point
	 * to the next next element.
	 */
	nextelem->next = (edj_t *)def;
	def->start = next;

	/* Free the previous element, but not its ->next */
	elem->next = NULL;
	edj_free(elem);

	return nextelem;
}

/* Test whether the current element is the last element. */
static int jdefarray_islast(const edj_t *elem)
{
	jdefarray_t *def = (jdefarray_t *)elem->first;
	const char *skip;

	/* "start" points to the next element's source code. Skip over
	 * whitespace and commas, and then check whether we hit a "]".
	 *
	 */
	for (skip = def->start; *skip == ',' || isspace(*skip); skip++) {
	}
	return *skip == ']';
}

static edjdeffns_t jdefarrayfns = {
	sizeof(jdefarray_t),	/* size */
	"JSON",			/* desc */
	jdefarray_first,	/* first */
	jdefarray_next,		/* next */
	jdefarray_islast,	/* islast */
	NULL,			/* free */
	NULL,			/* byindex */
	NULL			/* bykey */
};

/******************************************************************************/


/* Append an element to an array */
static void jappendarray(edj_t *container, edj_t *more)
{
	edj_t	*scan;

	if (!container->first) {
		/* First element */
		assert(EDJ_END_POINTER(container) == NULL);
		container->first = more;
	} else if ((scan = EDJ_END_POINTER(container)) != NULL) {
		/* Next element, optimized via EDJ_POINTER_END() */
		assert(scan->next == NULL); /* undeferred */
		scan->next = more; /* undeferred */
	} else {
		/* Next element, unoptimized */
		for (scan = container->first; scan->next; scan = scan->next) { /* undeferred */
		}
		scan->next = more; /* undeferred */
	}
	EDJ_END_POINTER(container) = more;
	EDJ_ARRAY_LENGTH(container)++;
	if (container->text[1] == 't' && (more->type != EDJ_OBJECT || more->first == NULL))
		container->text[1] = 'n';
}

/* Append a member to an object.  This version is only useable in the parser
 * because it assumes each member is new (no duplicates).
 */
static void jappendobject(edj_t *container, edj_t *more)
{
	edj_t	*scan;

	if (!container->first) {
		container->first = more;
	} else {
		for (scan = container->first; strcmp(scan->text, more->text); scan = scan->next) { /* object */
			if (!scan->next) { /* object */
				/* adding a new name */
				scan->next = more; /* object */
				EDJ_END_POINTER(container) = more;
				return;
			}
		}

		/* Replace the value of the member at "scan" */
		edj_free(scan->first);
		scan->first = more->first;
		more->first = NULL;
		edj_free(more);
	}
}

/* Add data to an object, array, or key.  Returns NULL normally, or an
 * error message if an error is detected.
 */
char *edj_append(edj_t *container, edj_t *more)
{
	assert(container != NULL && more != NULL);
	assert(container->type == EDJ_ARRAY || container->type == EDJ_OBJECT || container->type == EDJ_KEY);
	assert(container->type != EDJ_OBJECT || more->type == EDJ_KEY);

	switch (container->type) {
	  case EDJ_KEY:
		if (more->type == EDJ_KEY)
			return "Attempt to add a key as a value of a key";

		/* If the key already has a value, free it before storing
		 * the new value.
		 */
		if (container->first)
			free(container->first);
		container->first = more;
		break;

	  case EDJ_ARRAY:
		jappendarray(container, more);
		break;

	  case EDJ_OBJECT:
		if (more->type != EDJ_KEY)
			return "Attempt to add unkeyed data to an object";
		jappendobject(container, more);
		break;

	  case EDJ_BADTOKEN:
		return "edj_parse_append(..., EDJ_BADTOKEN)";
		break;

	  default:
		return "Attempt to append into a non-container";
	}
	return NULL;
}

/* This scans an array's source to determine whether it is worth deferring.
 * It quickly moves past an array without storing it, and returns a pointer to
 * the first character after the array.  Initially, "str" should point to the
 * "[" at the start of the array.  If refcount is non-NULL then store
 * the count of elements there.  If reftable is non-NULL then it stores a
 * flag indicating whether it is a table (non-empty array of objects).
 * Returns a pointer to the first character after the "]" marking the end
 * of the array.
 */
const char *jskim(const char *strin, const char *end, int *refcount, int *reftable)
{
        int     nest = 1;	/* Nesting depth for [] and {} */
        int	count = 0;	/* number of elements */
        int	nonobject = 0;	/* boolean: any non-object elements? */
        register const char *str = strin;

        /* Skip the '[' and trailing whitespace, to find the first element */
        do {
		str++;
	} while (isspace(*str));
	if (*str != ']') {
		count++;
		if (*str != '{')
			nonobject = 1;
	}

        /* Skip over data, counting array elements. */
        for (; nest > 0 && str < end; str++) {
                switch (*str) {
                  case '"':
			/* Skip past the string, minding backslashes */
			str++;
			while (*str != '"')
				if (*str++ == '\\')
					str++;
			break;
		  case ',':
			/* If top-level, then count an element */
			if (nest == 1) {
				do {
					str++;
				} while (isspace(*str));
				if (*str != ']') {
					count++;
					if (*str != '{')
						nonobject = 1;
				}
				str--;
			}
			break;
                  case '[':
                  case '{':
			nest++;
			break;
                  case ']':
                  case '}':
			nest--;
			break;
                }
        }

	/* Return the results. */
	if (refcount)
		*refcount = count;
	if (reftable)
		*reftable = (count > 0 && !nonobject);
        return str;
}

/* Parse an in-memory JSON document.  This could be a string, or an mmap()ed
 * file.
 */
static edj_t *parseJSON(const char *str, size_t len, const char **refend, const char **referr, int allowdefer)
{
	edj_t *stack[100];
	int	sp;
	edj_t	arraybuf;
	edj_t	*jc, *tail;
	const char	*end, *error;
	char	*key;
	size_t	keysize;
	size_t	tlen;	/* token length */
	int	escape;
	char	*emptyobject = "object";
	int	defersize = 0;

	/* Get parser config */
	jc = edj_by_key(edj_config, "emptyobject");
	if (jc && jc->type == EDJ_STRING)
		emptyobject = jc->text;
	jc = edj_by_key(edj_config, "defersize");
	if (jc && jc->type == EDJ_NUMBER)
		defersize = edj_int(jc);

	/* Start with a stack containing an empty array.  We expect parsing to
	 * put one thing in the array.
	 */
	memset(&arraybuf, 0, sizeof arraybuf);
	arraybuf.type = EDJ_ARRAY;
	sp = 0;
	stack[sp] = &arraybuf;

	/* Guess key size.  If we encounter a longer member key after this,
	 * we'll reallocate it.
	 */
	keysize = 100;
	key = malloc(keysize);
	*key = '\0';

	/* Locate the end of the text */
	end = str + len;

	/* ... aaaaaand... begin! */
	jc = NULL;
	tail = NULL;
	while (!stack[0]->first || sp > 0) {
		/* If we hit the end of the string without fully parsing
		 * anything, then that's an error
		 */
		if (str >= end) {
			error = "Incomplete JSON text";
			goto Error;
		}

		/* The next character determines the token type */
		switch (*str) {
		case '"':
			/* String or member key -- parse to unbackslashed ",
			 * noting whether any backslashes occur.
			 */
			str++;
			for (escape = 0, tlen = 0; str + tlen <= end && str[tlen] != '"'; tlen++) {
				if (str[tlen] == '\\') {
					escape = 1;
					tlen++;
				}
			}

			/* Is this supposed to be a key? Or a string value? */ 
			if (stack[sp]->type == EDJ_OBJECT && !*key) {
				/* It's a key.  But it could still use escapes */
				if (escape) {
					/* Get the length when unescaped */
					size_t bytes = edj_mbs_unescape(NULL, str, tlen);

					/* Enlarge buffer if necessary */
					if (bytes + 1 > keysize) {
						free(key);
						keysize = bytes + 20;
						key = malloc(keysize);
					}

					/* Decode escapes, copy key to keybuf */
					(void)edj_mbs_unescape(key, str, tlen);
					key[bytes] = '\0';
				} else {
					/* Enlarge buffer if necessary */
					if (tlen + 1 > keysize) {
						free(key);
						keysize = tlen + 20;
						key = malloc(keysize);
					}

					/* Copy the key */
					strncpy(key, str, tlen);
					key[tlen] = '\0';
				}
			} else {
				/* It's a string value.  Set "jc" */
				/* If it has escapes, process them */
				if (escape) {
					/* Get the length when unescaped */
					size_t bytes = edj_mbs_unescape(NULL, str, tlen);

					/* Allocate a big enough EDJ_STRING */
					jc = edj_string("", bytes);

					/* Copy the value into the string,
					 * converting any backslash escapes.
					 */
					(void)edj_mbs_unescape(jc->text, str, tlen);
					jc->text[bytes] = '\0';
				} else {
					jc = edj_string(str, tlen);
				}
			}

			/* Move "str" past the closing quote */
			str += tlen + 1;
			break;

		case '-':
		case '0':
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
		case '8':
		case '9':
		case '.':
			/* Number.  Collect digits */
			tlen = 0;
			if (str[tlen] == '-')
				tlen++;
			while (isdigit(str[tlen]))
				tlen++;
			if (str[tlen] == '.')
				tlen++;
			while (isdigit(str[tlen]))
				tlen++;
			if (str[tlen] == 'e' || str[tlen] == 'E') {
				tlen++;
				if (str[tlen] == '+' || str[tlen] == '-')
					tlen++;
				while (isdigit(str[tlen]))
					tlen++;
			}
			jc = edj_number(str, tlen);
			str += tlen;
			break;

		case 't':
			/* "true" */
			if (strncmp(str, "true", 4) || isalnum(str[4]))
				goto BadSymbol;
			jc = edj_boolean(1);
			str += 4;
			break;

		case 'f':
			/* "false" */
			if (strncmp(str, "false", 5) || isalnum(str[5]))
				goto BadSymbol;
			jc = edj_boolean(0);
			str += 5;
			break;

		case 'n':
			/* "null" */
			if (strncmp(str, "null", 4) || isalnum(str[4]))
				goto BadSymbol;
			jc = edj_null();
			str += 4;
			break;

		case '[':
			/* Start of an array  -- maybe deferred? */
			jc = edj_array();
			if (allowdefer && defersize > 0 && (end - str) >= defersize) {
				/* Find the end of the array */
				int count, istable;
				const char *endarray = jskim(str, end, &count, &istable);
				/* Is it big enough to be worth deferring? */
				if ((endarray - str) >= defersize) {
					/* Yes, defer it */
					jdefarray_t *def;
					jc->text[1] = istable ? 't' : 'n';
					EDJ_ARRAY_LENGTH(jc) = count;
					jc->first = edj_defer(&jdefarrayfns);
					def = (jdefarray_t *)jc->first;
					def->start = str + 1;
					def->end = endarray;
					def->basic.file = edj_file_containing(str, NULL);
					if (def->basic.file)
						def->basic.file->refs++;

					/* Move past the array.  "- 1" because
					 * of the "str++" before "break".
					 */
					str = endarray - 1;
				}
			}
			str++;
			break;

		case ']':
			/* End of an array */
			if (stack[sp]->type != EDJ_ARRAY) {
				error = "Missing }";
				goto Error;
			}
			sp--;
			str++;
			break;

		case '{':
			/* Start of object */
			jc = edj_object();
			str++;
			break;

		case '}':
			/* End of object */
			if (stack[sp]->type != EDJ_OBJECT) {
				error = "Missing ]";
				goto Error;
			}

			/* If empty object, maybe convert it to an empty
			 * string or array.
			 */
			if (!stack[sp]->first) {
				if (*emptyobject == 'a')
					stack[sp]->type = EDJ_ARRAY;
				else if (*emptyobject == 's')
					stack[sp]->type = EDJ_STRING;
			}
			sp--;
			str++;
			tail = NULL;
			break;

		case ':':
		case ',':
		case ' ':
		case '\t':
		case '\n':
		case '\r':
			/* Whitespace can be ignored.  Surprisingly, so can
			 * colons and commas.
			 */
			while (isspace(*str) || *str == ':' || *str == ',')
				str++;
			break;

		default:
			/* Unexpected character */
			error = "Unexpected character in JSON data";
			goto Error;
		}

		/* If jc is set, add it to the container on the stack */
		if (jc) {

			if (stack[sp]->type == EDJ_OBJECT) {
				edj_t *jk;

				if (!*key) {
					error = "Object member has no key";
					goto Error;
				}

				/* Combine the key and value */
				jk = edj_key(key, jc);
				*key = '\0';

				/* We don't use edj_append() here because
				 * we know this is a non-duplicate key and
				 * hence must append.  We want to skip scanning
				 * the whole object each time we do this, so
				 * we maintain a "tail" pointer.
				 */
				if (!stack[sp]->first)
					stack[sp]->first = jk;
				else {
					if (tail == NULL) {
						for (tail = stack[sp]->first;
						     tail->next; /* object */
						     tail = tail->next) { /* object */
						}
					}
					tail->next = jk; /* object */
				}
				tail = jk;
			} else {
				/* Append to an array.  Arrays maintain their
				 * own "tail" pointer so edj_append() works
				 * efficiently here.
				 */
				edj_append(stack[sp], jc);
			}

			/* If it's a new array or object, push it onto the stack
			 * so we can start to accumulate its members/elements.
			 * Except if deferred array.
			 */
			if ((jc->type == EDJ_ARRAY && !edj_is_deferred_array(jc)) || jc->type == EDJ_OBJECT)
				stack[++sp] = jc;
		}

		jc = NULL;
	}

	/* Return the thing in the arraybuf */
	if (refend)
		*refend = str;
	free(key);
	return arraybuf.first;

BadSymbol:
	error = "Bad symbol";

Error:
	/* Free up any partial results.  Since every item gets added to
	 * whatever object or array contains it immediately (even nested
	 * arrays and objects get added before they're fully parsed), we
	 * DON'T need to loop over the stack.  stack[0]->first contains
	 * &arraybuf, so it shouldn't be freed either, but arraybuf.first
	 * should.  And maybe jc, if it isn't NULL.
	 */
	edj_free(arraybuf.first);
	if (jc)
		edj_free(jc);
	free(key);

	/* Stuff the error info into the appropriate places */
	if (refend)
		*refend = str;
	if (referr)
		*referr = error;
	return NULL;
}

/* List of registered parsers (other than the built-in JSON parser) */
edjparser_t *parsers;

/* This is used by both edj_parse_file() and edj_parse_string() to do the
 * actual JSON parsing.
 */
static edj_t *parse(const char *str, size_t len, const char **refend, const char **referr, int allowdefer)
{
	edjparser_t *jp;

	/* If any add-on parser wants it, let it parse try */
	for (jp = parsers; jp; jp = jp->other) {
		if (jp->tester(str, len))
			return jp->parser(str, len, refend, referr);
	}

	/* How about binary? */
	if (edj_blob_test(str, len))
		return edj_blob_parse(str, len, refend, referr);

	/* Otherwise, fall back on the JSON parser */
	return parseJSON(str, len, refend, referr, allowdefer);
}


/* Parse a string and return its edj_t.  If there's an error, then it will
 * return a "null" edj_t containing the error text.
 */
edj_t *edj_parse_string(const char *str)
{
	const char 	*end, *error;
	edj_t	*result;

	/* Parse it */
	result = parse(str, strlen(str), &end, &error, 0);

	/* If error, then return a "null" edj_t with an error message */
	if (!result)
		return edj_error_null(NULL, "%s", error);
	return result;
}

/* Parse a file and return its edj_t.  Returns NULL if the file can't be
 * opened.  If it can be opened but not parsed, it returns a "null" edj_t
 * containing the error message.  Otherwise it returns the parsed data.
 */
edj_t *edj_parse_file(const char *filename)
{
	edjfile_t *jf;
	const char	*end, *error;
	edj_t	*result;

	/* Map the file into memory */
	jf = edj_file_load(filename);
	if (!jf)
		return NULL;

	/* Parse it */
	result = parse(jf->base, jf->size, &end, &error, 1);

	/* Close/unmap the file */
	edj_file_unload(jf);

	/* If error, then return a "null" edj_t with an error message */
	if (!result)
		return edj_error_null(NULL, "%s", error);
	return result;
}


/* Register a new type of parser.  The arguments are the parser's name, a
 * pointer to a tester function, and a pointer to a parser function.  If the
 * tester function returns a non-zero value, then the parser function is used
 * to parse this data.
 */
void edj_parse_hook(
	const char *plugin,
	const char *name,
	const char *suffix,
	const char *mimetype,
	int (*tester)(const char *str, size_t len),
	edj_t *(*parser)(const char *str, size_t len, const char **refend, const char **referr),
	int (*updater)(edj_t *data, const char *filename))
{
	edj_t	*table, *row;
	edjparser_t	*jp, *scan;

	/* Allocate a new edjparser_t for it */
	jp = malloc(sizeof *jp);
	jp->other = NULL;
	jp->name = name;
	jp->tester = tester;
	jp->parser = parser;
	jp->updater = updater;

	/* Add it to the end of the list */
	if (parsers) {
		for (scan = parsers; scan->other; scan = scan->other) {
		}
		scan->other = jp;
	} else {
		parsers = jp;
	}

	/* Add a row to the "parsers" table in edj_system */
	table = edj_by_key(edj_system, "parsers");
	row = edj_object();
	edj_append(row, edj_key("name", edj_string(name, -1)));
	edj_append(row, edj_key("plugin", plugin ? edj_string(plugin, -1) : edj_null()));
	edj_append(row, edj_key("suffix", suffix ? edj_string(suffix, -1) : edj_null()));
	edj_append(row, edj_key("mimetype", mimetype ? edj_string(mimetype, -1) : edj_null()));
	edj_append(row, edj_key("writable", edj_boolean(updater != NULL)));
	edj_append(table, row);
}

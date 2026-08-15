/* find.c */
#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <regex.h>
#include <ctype.h>
#include <edj.h>

/* This stores the search criteria and a incremental results.  The help_find()
 * function is heavily recursive, so if we had to pass all of this as
 * parameters individually it'd be a big burden.
 */
typedef struct {
	edj_t	*needle;	/* String or number to search for */
	regex_t *regex;		/* Regular expression to search for */
	edjcalc_t *calc;		/* Expression to look for (RHS of @ operator) */
	edjcontext_t *context;	/* Context of the "calc" expression */
	char	*needkey;	/* If not NULL, key must match this */
	int	needint;	/* Integer to search for */
	double	needdouble;	/* Double to search for */
	char	needdigits[30];	/* Integer value as a digit string */
	int	int_or_double;	/* 'i' for needint, else needdouble */
	int	ignorecase;	/* non-zero to let uppercase match lowercase */
	int	grep;		/* return matching rows instead of a list */
	int	index;		/* Outermost array subscript of match */
	char	*key;		/* Innermost object member key of match */
	char	*expr;		/* Buffer for building an expression ex match */
	size_t	size;		/* Size of expr buffer */
	size_t	used;		/* Amount of expr buffer that's used now */
	edj_t	*result;	/* Table of found matches */
} edjfind_t;

/* Append a string to find->expr, expanding it if necessary.  When we move
 * deeper into a data structure, this is used to build the path up to whatever
 * part we're scanning now.
 */
static void help_find_cat(edjfind_t *find, char *str)
{
	size_t len, quotes, newsize;

	/* Compute the size of the string.  This will be the length of the
	 * string, plus quotes and maybe a "." at the end.
	 */
	if (*str == '[') {
		len = strlen(str);
		quotes = 0;
	} else {
		for (len = quotes = 0; str[len]; len++)
			if (!isalnum(str[len]) && str[len] != '_')
				quotes = 2;
	}

	/* If necessary, expand ->expr */
	newsize = find->used + len + quotes + 1;
	if (find->used > 0 && *str != '[')
		newsize++; /* because we'll need to add a "." */
	if (newsize > find->size) {
		/* Round up to a multiple of 32 bytes */
		newsize = ((newsize - 1) | 0x1f) + 1;

		/* Reallocate the buffer */
		find->expr = realloc(find->expr, newsize);
		find->size = newsize;
	}

	/* Append it */
	if (find->used > 0 && *str != '[')
		find->expr[find->used++] = '.';
	if (quotes)
		find->expr[find->used++] = '`';
	strcpy(find->expr + find->used, str);
	find->used += len;
	if (quotes)
		find->expr[find->used++] = '`';
}

/* Append a row to the find->result table */
static void help_find_row(edjfind_t *find, edj_t *node)
{
	/* If doing grep, then don't add rows this way. */
	if (find->grep)
		return;

	/* Add a row describing where the value was found */
	edj_t *found = edj_object();
	if (find->index >= 0)
		edj_append(found, edj_key("index", edj_from_int(find->index)));
	if (find->key)
		edj_append(found, edj_key("key", edj_string(find->key, -1)));
	edj_append(found, edj_key("value", edj_copy(node)));
	edj_append(found, edj_key("expr", edj_string(find->expr, find->used)));
	edj_append(find->result, found);
}

/* Do a deep search for a value.  This is a helper function for jfn_find(),
 * which implement's edj's find() function.
 */
static int help_find(edj_t *haystack, edjfind_t *find)
{
	edj_t	*scan;
	int	wasused, wasindex, i, match;
	char	*waskey;
	char	indexstr[40];

	/* If given a "calc" test and it matches, then add it to the result
	 * but DON'T continue to scan its contents for additional matches.
	 */
	if (find->calc) {
		edj_t *test = edj_calc(find->calc, find->context, NULL);
		match = edj_is_true(test);
		edj_free(test);
		if (match) {
			help_find_row(find, haystack);
			return 1;
		}
	}

	/* Arrays and objects are treated a bit differently */
	if (haystack->type == EDJ_ARRAY) {
		/* For each element... */
		for (i = 0, scan = edj_first(haystack); scan && !edj_interrupt; i++, scan = edj_next(scan)) {
			/* If the value is an object or array, recurse */
			if (scan->type == EDJ_OBJECT || scan->type == EDJ_ARRAY) {
				/* Append this element's index to expr */
				wasused = find->used;
				snprintf(indexstr, sizeof indexstr, "[%d]", i);
				help_find_cat(find, indexstr);
				wasindex = find->index;
				if (find->index == -1)
					find->index = i;
				if (find->context)
					find->context = edj_context(find->context, scan, EDJ_CONTEXT_NOFREE|EDJ_CONTEXT_THIS);

				/* Recurse */
				if (help_find(scan, find) && find->grep)
					return 1;

				/* Restore expr */
				if (find->context)
					find->context = edj_context_free(find->context);
				find->used = wasused;
				find->index = wasindex;
				continue;
			} else if (find->calc) {
				/* Already did the calc test */
				continue;
			} else if (find->needkey && (!find->key || 0 != edj_mbs_casecmp(find->needkey, find->key) )) {
				/* Wrong key.  The only reason we're scanning
				 * this array is that it might have an element
				 * that's an object with needkey, but this
				 * element isn't an object/array.
				 */
				continue;
			} else if (!find->needle && !find->regex) {

				/* Arrays are weird in one way: When searching
				 * for "any value" (usually because we're only
				 * looking or a specific key) then we DON'T
				 * want to add each element to the result.
				 * We've already added the array as a whole
				 * if it is the value of a member with the
				 * desired key; that's enough.
				 */
				if (find->needkey)
					continue;
			} else if (scan->type == EDJ_STRING && find->regex) {
				/* Compare against the regexp */
				regmatch_t matches[10];
				if (regexec(find->regex, scan->text, 10, matches, 0) != 0)
					continue;
			} else if (scan->type == EDJ_STRING && find->needle && find->needle->type == EDJ_STRING) {
				/* Compare as strings */
				if (find->ignorecase) {
					if (0 != edj_mbs_casecmp(find->needle->text, scan->text))
						continue;
				} else {
					if (0 != strcmp(find->needle->text, scan->text))
						continue;
				}
			} else if (scan->type == EDJ_NUMBER && find->needle && find->needle->type == EDJ_NUMBER) {
				/* Does it match? */

				/* Optimization for comparing binary integers */
				if (scan->text[0] == 0 && scan->text[1] == 'i' && find->int_or_double == 'i') {
					/* Compare binary integers */
					if (edj_int(scan) != find->needint)
						continue;
				} else {
					/* Compare as double */
					if (edj_double(scan) != find->needdouble)
						continue;
				}

			} else if (scan->type == EDJ_STRING && find->needle && find->needle->type == EDJ_NUMBER && find->int_or_double == 'i') {
				/* Compare a digit string to an integer */
				if (strcmp(scan->text, find->needdigits))
					continue;
			} else if (find->needle) {
				/* it can't be what we're looking for */
				continue;
			}

			/* If we get here, it matched.  We need to append this
			 * element's index to expr, and then add a match to the
			 * result array
			 */
			if (find->grep) {
				edj_break(scan);
				return 1;
			}
			wasused = find->used;
			snprintf(indexstr, sizeof indexstr, "[%d]", i);
			help_find_cat(find, indexstr);
			if (find->index == -1)
				find->index = i;
			help_find_row(find, scan);

			/* Restore expr */
			find->used = wasused;
			continue;
		}
	} else /* EDJ_OBJECT */ {
		/* For each member... */
		for (scan = haystack->first; scan && !edj_interrupt; scan = scan->next) { /* object */
			/* If the value is an object or array, recurse */
			if (scan->first->type == EDJ_OBJECT || scan->first->type == EDJ_ARRAY) {
				/* Append this member's key to expr */
				wasused = find->used;
				help_find_cat(find, scan->text);

				/* If any value can match, and the key matches
				 * (or we don't care about the key) then add
				 * this member as a match.
				 */
				if (!find->needle
				 && !find->regex
				 && !find->calc
				 && (!find->needkey || !edj_mbs_casecmp(find->needkey, scan->text))) {
					if (find->grep)
						return 1;
					help_find_row(find, scan->first);
				}

				/* Store the key so that if we're scanning an
				 * array, we'll know which array this is.
				 */
				waskey = find->key;
				find->key = scan->text;
				if (find->context)
					find->context = edj_context(find->context, scan, EDJ_CONTEXT_NOFREE|EDJ_CONTEXT_THIS);

				/* Recurse */
				if (help_find(scan->first, find) && find->grep)
					return 1;

				/* Restore expr */
				if (find->context)
					find->context = edj_context_free(find->context);
				find->key = waskey;
				find->used = wasused;
				continue;
			} else if (find->calc) {
				/* We already checked */
				continue;
			} else if (find->needkey && 0 != edj_mbs_casecmp(find->needkey, scan->text)) {
				/* Wrong key */
				continue;
			} else if (scan->first->type == EDJ_STRING && find->needle && find->needle->type == EDJ_STRING) {
				/* Compare as strings */
				if (find->ignorecase) {
					if (0 != edj_mbs_casecmp(find->needle->text, scan->first->text))
						continue;
				} else {
					if (0 != strcmp(find->needle->text, scan->first->text))
						continue;
				}
			} else if (scan->first->type == EDJ_STRING && find->regex) {
				/* Compare against the regexp */
				regmatch_t matches[10];
				if (regexec(find->regex, scan->first->text, 10, matches, 0) != 0)
					continue;
			} else if (scan->first->type == EDJ_NUMBER && find->needle && find->needle->type == EDJ_NUMBER) {
				/* Does it match? */

				/* Optimization for comparing binary integers */
				if (scan->first->text[0] == 0 && scan->first->text[1] == 'i' && find->int_or_double == 'i') {
					/* Compare binary integers */
					if (edj_int(scan->first) != find->needint)
						continue;
				} else {
					/* Compare as double */
					if (edj_double(scan->first) != find->needdouble)
						continue;
				}

			} else if (!find->needle && !find->regex && !find->calc) {
				/* any value matches */
			} else {
				/* it can't be what we're looking for */
				continue;
			}

			/* If we get here, it matched.  We need to append this
			 * member's key to expr, and then add a match to the
			 * result array
			 */
			if (find->grep)
				return 1;
			wasused = find->used;
			help_find_cat(find, scan->text);
			waskey = find->key;
			find->key = scan->text;
			help_find_row(find, scan->first);

			/* Restore expr */
			find->key = waskey;
			find->used = wasused;
			continue;
		}
	}

	return 0;
}

/* Search for a given value.  This is a deep search, meaning it'll look through
 * any nested objects or arrays too.
 * 
 * "haystack" is an object or array to search through, "needle" is the string
 * or number to search for, "ignorecase" makes string searches be
 * case-insensitive, "regex" if not null will override "needle" and search
 * for a string via a regular expression, and "key" if not null will ignore
 * matches unless they're in a member with that name.
 *
 * The result is a table containing a list of matches.  The members of each
 * row are:
 *   index	The outermost array subscript of the match. Omitted if no array.
 *   key	The innermost member name of the match.  Omitted if no object.
 *   value	The matching value that was found.
 *   expr	Expression for the match, suitable for use with edj_by_expr()
 *
 * If no matches are found, an empty array is returned.  Parameter errors cause
 * a "null" edj_t to be returned containing an error message.
 */
static edj_t *find_any(edj_t *haystack, edj_t *needle, int ignorecase, regex_t *regex, char *needkey, int grep, edjcalc_t *calc, edjcontext_t *context)
{
	edjfind_t find;

	/* Check parameters */
	if (haystack->type != EDJ_ARRAY && haystack->type != EDJ_OBJECT)
		return edj_error_null(0, "Can only find within an object or array");
	if (!regex && needle && (needle->type != EDJ_STRING && needle->type != EDJ_NUMBER))
		return edj_error_null(0, "Can only find string, number, or regex");

	/* Fill in the find argument block */
	memset(&find, 0, sizeof find);
	find.needle = needle;
	find.regex = regex;
	find.needkey = needkey;
	find.ignorecase = ignorecase;
	find.grep = grep;
	find.calc = calc;
	find.context = context;
	find.index = -1;
	find.key = NULL;
	find.size = 100;
	find.used = 0;
	find.expr = malloc(find.size);
	find.result = edj_array();

	/* If searching for a number, convert it to binary */
	if (needle && needle->type == EDJ_NUMBER) {
		if (needle->text[0] == '\0') {
			find.int_or_double = needle->text[1];
		} else {
			if (strchr(find.needle->text, '.')
			 || strchr(find.needle->text, 'e')
			 || strchr(find.needle->text, 'E'))
				find.int_or_double = 'd';
			else
				find.int_or_double = 'i';
		}
		if (find.int_or_double == 'i') {
			find.needint = edj_int(needle);
			find.needdouble = (double)find.needint;
			snprintf(find.needdigits, sizeof find.needdigits, "%d", find.needint);
		} else {
			find.needdouble = edj_double(needle);
			/* don't need find->needint */
		}
	}

	/* Let the helper function do most of the work */
	if (grep) {
		edj_t *scan;
		for (scan = edj_first(haystack); scan && !edj_interrupt; scan = edj_next(scan)) {
			/* Since help_find can only handle arrays and objects,
			 * we need to stuff each element into a bogus array
			 * of its own.
			 */
			edj_t array, *next;
			array.type = EDJ_ARRAY;
			array.first = scan;
			next = scan->next;
			scan->next = NULL;
			if (help_find(&array, &find))
				edj_append(find.result, edj_copy(scan));
			scan->next = next;

		}
	} else {
		(void)help_find(haystack, &find);
	}

	/* Clean up, and Return the result */
	free(find.expr);
	return find.result;
}

/* Do a deep search for a value */
edj_t *edj_find(edj_t *haystack, edj_t *needle, int ignorecase, char *needkey)
{
	return find_any(haystack, needle, ignorecase, NULL, needkey, 0, NULL, NULL);
}

/* Do a deep search for a regular expression */
edj_t *edj_find_regex(edj_t *haystack, regex_t *regex, char *needkey)
{
	return find_any(haystack, NULL, 0, regex, needkey, 0, NULL, NULL);
}

/* Do a deep search for a value that makes an expression true.  This is used
 * to implement the "@" operator.
 */
edj_t *edj_find_calc(edj_t *haystack, edjcalc_t *calc, edjcontext_t *context)
{
	return find_any(haystack, NULL, 0, NULL, NULL, 0, calc, context);
}

/* Do a deep search for a value in rows of a table*/
edj_t *edj_grep(edj_t *haystack, edj_t *needle, int ignorecase, char *needkey)
{
	if (haystack->type != EDJ_ARRAY)
		return edj_error_null(NULL, "needarray:The first argument to %s() must be an array", "grep");
	return find_any(haystack, needle, ignorecase, NULL, needkey, 1, NULL, NULL);
}

/* Do a deep search for a regular expression in rows of a table */
edj_t *edj_grep_regex(edj_t *haystack, regex_t *regex, char *needkey)
{
	if (haystack->type != EDJ_ARRAY)
		return edj_error_null(NULL, "needarray:The first argument to %s() must be an array", "grep");
	return find_any(haystack, NULL, 0, regex, needkey, 1, NULL, NULL);
}

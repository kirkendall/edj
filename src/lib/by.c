#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include <edj.h>

/* Return the value of a named field within an object or array.  If there
 * is no such element, then return NULL.
 */
edj_t *edj_by_key(const edj_t *container, const char *key)
{
	edj_t *scan;
	char	*simple, *loose;

	/* Defend against NULL */
	if (!container)
		return NULL;

	/* Only objects should have named values (though JavaScript also
	 * supports "associative arrays" which do the same thing)
	 */
	if (container->type != EDJ_OBJECT)
	{
		/* EEE "Attempt to find named item in a non-object"); */
		return NULL;
	}

	/* Scan for it.  If found, return its value */
	for (scan = container->first; scan; scan = scan->next) /* object */
	{
		assert(scan->type == EDJ_KEY);
		if (!strcmp(scan->text, key))
			return scan->first;
	}

	/* Not found, but try again using loose name comparison */
	simple = strdup(key);
	(void)edj_mbs_simple_key(simple, key);
	for (scan = container->first; scan; scan = scan->next) /* object */
	{
		/* Locate the key's "loose" version.  If it doesn't exist
		 * then create it now.
		 */
		loose = scan->text + strlen(scan->text) + 1;
		if (!*loose)
			(void)edj_mbs_simple_key(loose, scan->text);

		/* Compare them now */
		if (!strcmp(loose, simple)) {
			free(simple);
			return scan->first;
		}
	}
	free(simple);

	/* Not found */
	return NULL;
}

/* Look for a member by key.  If not in the top-level object, then look
 * in anything that it contains.  Also, the container can be an array, not
 * just an object.
 */
edj_t *edj_by_deep_key(edj_t *container, char *key)
{
        edj_t  *result, *scan;

        /* First try the container itself (no nesting) */
        if (container->type == EDJ_OBJECT) {
                result = edj_by_key(container, key);
                if (result)
                        return result;
        }

        /* Next try any containers within this object */
        result = NULL;
        for (scan = container->first; scan && !result; scan = scan->next) { /* object */
                if (scan->type == EDJ_KEY && (scan->first->type == EDJ_OBJECT || scan->first->type == EDJ_ARRAY))
                        result = edj_by_deep_key(scan->first, key);
                else if (scan->type == EDJ_OBJECT || scan->type == EDJ_ARRAY)
                        result = edj_by_deep_key(scan, key);
        }
        return result;
}

/* Return the value of an indexed element within an array.  If there
 * is no such element, then return NULL.
 *
 * IMPORTANT NOTE: If container is a deferred array, then you must call
 * edj_break() on the returned element when you're done with it.  If not a
 * deferred array, then edj_break() is still safe to call on the element.
 */
edj_t *edj_by_index(edj_t *container, int idx)
{
	edj_t *scan;
	edjdef_t *def;
	int	scanidx;

	/* Defend against NULL */
	if (!container)
		return NULL;

	/* Only arrays should have indexed values */
	if (container->type != EDJ_ARRAY)
	{
		/* EEE "Attempt to find indexed item in a non-array" */
		return NULL;
	}

	/* Defend against negative indexes.  But also try to use them as being
	 * an index relative to the end of the array.
	 */
	if (idx < 0) {
		idx += edj_length(container);
		if (idx < 0)
			return NULL;
	}

	/* If this is a deferred array, and it has a quick way to jump to a
	 * given index, then use that.
	 */
	if (edj_is_deferred_array(container)
	 && (def = (edjdef_t *)(container->first))->fns->byindex)
		return (*def->fns->byindex)(container, idx);

	/* Scan for it.  If found, return its value */
	for (scan = edj_first(container), scanidx = 0; scan; scan = edj_next(scan))
	{
		/* if the index matches, use it */
		if (scanidx == idx)
			return scan;
		scanidx++;
	}

	/* Not found */
	return NULL;
}


/* Find an element of an array that contains a member with a given name and
 * optionally a given value for that name.  If found, return it; else return
 * NULL.
 * 
 * IMPORTANT NOTE: If container is a deferred array, then you must call
 * edj_break() on the returned element when you're done with it.  If not a
 * deferred array, then edj_break() is still safe to call on the element.
 */
edj_t *edj_by_key_value(edj_t *container, const char *key, edj_t *value) 
{
	edj_t	*scan, *found;
	edjdef_t *def;

	/* This only works on tables (arrays of objects) */
	if (!edj_is_table(container))
		return NULL;

	/* If it is a deferred array, and it has a "bykey" function pointer,
	 * then use that.
	 */
	if (edj_is_deferred_array(container)
	 && (def = (edjdef_t*)container->first)->fns->bykeyvalue)

		return (*def->fns->bykeyvalue)(container, key, value);

	/* Scan array for element with that member key:value */
	for (scan = edj_first(container); scan; scan = edj_next(scan)) {
		if (scan->type != EDJ_OBJECT)
			continue;
		found = edj_by_key(scan, key);
		if (found && edj_equal(found, value)) {
			return scan;
		}
	}

	/* Not found */
	return NULL;
}

/* Return an item inside nested objects or arrays, selected via a
 * JavaScript-like expression.  The expr is a string containing a series of
 * member names or subscript numbers.  Each of these values may be delimited
 * by one or more characters from the list ".[]", with the idea being that
 * you would use something like "ROList.ro[0].job[0].opcode" to fetch the
 * first opcode of the first RO.
 * 
 * The expression ends at the first character that isn't a letter, digit,
 * or one of "_[].".  If the "next" argument isn't null, the pointer that
 * it refers to will be set to the first character after the expression;
 * this way you can write wrappers to handle things such as comma-delimited
 * lists of expressions.
 *
 * The "parent" and "key" arguments are usually NULL.  If non-NULL then *parent
 * will be set to the edj_t of the array or object that contains the returned
 * edj_t.  If *parent is an object, then *key will be set to a dynamically
 * allocated copy of the value's key.  This is handy if you're hoping
 * to replace the edj_t with some other value.  When you're using edj_by_expr()
 * this way, a return value of NULL is not necessarily a bad thing.
 *
 * Deferred arrays cause problems.  We want to return the edj_t within the
 * container, but deferred arrays allocate and free elements as they are
 * scanned.  So if a deferred array is involved then the returned item must
 * look like a deferred element that edj_break() can clean up.
 */
edj_t *edj_by_expr(edj_t *container, const char *expr, const char **next, edj_t **parent, char **key)
{
	char	keybuf[100];
	int	i, deep, quote;
	edj_t	*step;
	edj_t	*defelem;

	/* Defend against NULL */
	if (!container)
		return NULL;

	/* We need to keep track of whether the returned data is part of a
	 * deferred element.  If so, then we have some extra work to do before
	 * returning it.  For now, assume there is no deferred array.
	 */
	defelem = 0;

	/* Initialize *parent and *key to NULL. */
	if (parent)
		*parent = NULL;
	if (key)
		*key = NULL;

	/* Initialize keybuf, so we can detect whether we've used it */
	*keybuf = '\0';

	/* Work through the expr, and down into the container */
	do
	{
		/* Skip leading delimiters */
		deep = 0;
		while (*expr && strchr("[].~", *expr)) {
			if (expr[0] == '.' && expr[1] == '.')
				deep = 1;
			expr++;
		}

		/* Detect number or symbol.  If neither, we're done */
		if (isdigit(*expr))
		{
			if (container) {
				if (container->type != EDJ_ARRAY)
				{
					/* EEE if (edj_debug_flags.expr) "Attempt to use an index on a non-array via an expr");*/
					if (defelem)
						edj_break(defelem);
					return NULL;
				}
				step = edj_by_index(container, atoi(expr));
				if (!step) {
					if (defelem)
						edj_break(defelem);
					return NULL;
				}
			}
			while (isdigit(*expr))
				expr++;

			/* If this is an element of a deferred array, remember
			 * that so we can clean up later.
			 */
			if (edj_is_deferred_element(step) && !defelem)
				defelem = step;
		}
		else if (isalpha(*expr) || *expr == '_')
		{
			if (container) {
				if (container->type != EDJ_OBJECT)
				{
					/* EEE if (edj_debug_flags.expr) edj_throw(NULL, "Attempt to find a member in a non-object via an expr");*/
					if (defelem)
						edj_break(defelem);
					return NULL;
				}
				for (i = 0; i < sizeof keybuf - 1 && (isalnum(*expr) || *expr == '_'); i++)
					keybuf[i] = *expr++;
				keybuf[i] = '\0';
				if (deep)
					step = edj_by_deep_key(container, keybuf);
				else
					step = edj_by_key(container, keybuf);
			}
		}
		else if (*expr == '"' || *expr == '`')
		{
			if (container) {
				if (container->type != EDJ_OBJECT)
				{
					/* EEE if (edj_debug_flags.expr) edj_throw(NULL, "Attempt to find a member in a non-object via an expr"); */
					if (defelem)
						edj_break(defelem);
					return NULL;
				}
				quote = *expr++;
				for (i = 0; i < sizeof keybuf - 1 && *expr != quote; i++)
					keybuf[i] = *expr++;
				keybuf[i] = '\0';
				expr++;
				if (deep)
					step = edj_by_deep_key(container, keybuf);
				else
					step = edj_by_key(container, keybuf);
			}
		}
		else
		{
			break;
		}

		/* NOTE: If we couldn't find what we're looking for, then
		 * container gets set to NULL and stays that way.  We continue
		 * parsing the expression anyway so we can find the end of it
		 * if there's a "next" pointer.
		 */
		if (!step && !next)
			break;
		if (parent)
			*parent = defelem ? NULL : container;
		container = step;
	} while (*expr && strchr("[].~", *expr));

	/* If we were in a deferred array, then we need to make the returned
	 * edj_t look like an element of that deferred array.
	 */
	if (defelem && container && !edj_is_deferred_element(container)) {
		/* We need to call edj_break() on the defelem to free up
		 * the scanning resources.  Before we do that, though, we
		 * need to make a copy of the returned value.
		 */
		container = edj_copy(container);

		/* The copy should have its ->next pointer going to a generic
		 * EDJ_DEFER node, just so edj_break() will free it.
		 */
		container->next = edj_defer(NULL);

		/* Okay, now it's safe to free the deferred element */
		edj_break(defelem);
	}

	/* return the result */
	if (next)
		*next = expr;
	/* *parent was already set, if appropriate */
	if (key && *keybuf)
		*key = strdup(keybuf);
	return container;
}

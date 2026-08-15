#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <edj.h>


/* Return a new array which merges any embedded arrays into new array proper.
 * For example, [1,[2,3],4] would become [1,2,3,4].  Depth can be 0 to just
 * copy without changing anything, 1 for a single layer, 2 for 2 layers deep,
 * or as a special case, -1 means unlimited depth.
 */
edj_t *edj_array_flat(edj_t *array, int depth)
{
	edj_t *result;
	edj_t *scan, *lag;
	edj_t *copy;

	/* Defend against NULL */
	if (!array)
		return NULL;

	/* If not an array, return NULL.  (We don't dare return it unchanged
	 * because this function is intended to return a NEW array, not a
	 * link to existing data.)
	 */
	if (array->type != EDJ_ARRAY)
		return NULL;

	/* Start a new array */
	result = edj_array();

	/* For each element of the array... */
	for (lag = NULL, scan = array->first; scan; scan = scan->next) { /* undeferred */
		/* If depth is 0 or this element isn't array, copy it */
		if (depth == 0 || scan->type != EDJ_ARRAY) {
			lag = edj_copy(scan);
			edj_append(result, lag);
			continue;
		}

		/* Okay, we have an array and we want to include it.  Copy it
		 * via a recursive call to edj_array_flat(), and then munge
		 * the pointers to make it appear after "lag".  If "lag" is
		 * NULL then it's the first segment in the result.  Leave
		 * "lag" pointing at the end of the array.
		 */
		copy = edj_array_flat(scan, depth - 1);
		if (lag)
			lag->next = copy->first; /* undeferred */
		else
			result->first = copy->first;
		for (lag = copy->first; lag && lag->next; lag = lag->next) { /* undeferred */
		}
		EDJ_END_POINTER(result) = lag;

		/* Free the copy array node, but not its elements. */
		copy->first = NULL;
		edj_free(copy);
	}

	/* Done! */
	return result;
}

/* Unroll nested tables (arrays of objects).  "table" should be the outer
 * table, and "nestlist" is an array of strings giving the names of the
 * nested arrays to unroll.  You may also intersperse boolean symbols to
 * control how to handle missing/empty tables -- false skips the whole
 * outer element, and true just keeps the outer element with nothing added
 * for inner rows.
 *
 * This always returns a table, except that in some situations it may return
 * an empty array which technically isn't a table.  The returned value is
 * COPIED from the input table; the original table is unchanged.
 */
edj_t *edj_unroll(edj_t *table, edj_t *nestlist)
{
	int	skipempty = 0;	/* If nested list is empty, do we skip it? */
	edj_t	*value;		/* value of member to recursively unroll */
	edj_t	*nested;	/* recursively-unrolled nested array */
	edj_t	*nrow;		/* Used for scanning nested array's rows */
	edj_t	*tmember;	/* Used for scanning table element's members */
	edj_t	*nmember;	/* Used for scanning nrow object's members */
	edj_t	*row;		/* Used for building a result element */
	edj_t	*result;	/* Used to accumulate the result array */

	/* If not a table (including null!), just return an empty array */
	if (!table || (!edj_is_table(table) && table->type != EDJ_OBJECT))
		return edj_array();

#if 0
	/* This won't work for deferred arrays */
	edj_undefer(table);
#endif

	/* We want to treat the nest list as linked list of EDJ_STRINGs.
	 * Probably it comes to us as a EDJ_ARRAY though; skip to the start
	 * of the first element of the array.  Skip any non-strings.  If we
	 * encounter a boolean, set the skipempty flag accordingly.
	 */
	if (nestlist && nestlist->type == EDJ_ARRAY)
		nestlist = nestlist->first; /* undeferred */
	while (nestlist && nestlist->type != EDJ_STRING) {
		if (nestlist->type == EDJ_BOOLEAN)
			skipempty = edj_is_true(nestlist);
		nestlist = nestlist->next; /* undeferred */
	}

	/* If nesting list is empty, return a copy of the table. */
	if (!nestlist || (nestlist->type == EDJ_ARRAY && !nestlist->first)) {
		/* Actually, it's a bit more complicated.  If we were passed
		 * an object instead of a table, then we should convert it to
		 * table by stuffing it into an array.
		 */
		if (table->type == EDJ_OBJECT) {
			result = edj_array();
			edj_append(result, edj_copy(table));
			return result;
		}
		return edj_copy(table);
	}

	/* Start with an empty response array */
	result = edj_array();

	/* For each row of the table... */
	for (table = edj_first(table); table; table = edj_next(table)) {
		/* Fetch the unrolled nested variable */
		value = edj_by_expr(table, nestlist->text, NULL, NULL, NULL);/* undeferred */

		/* Recursively unroll the nested value.  Since the name of
		 * this member was passed as part of nestlist, we expect value
		 * to be a table but if it isn't then edj_unroll() will return
		 * an empty array.  Either way, it's an array.
		 */
		nested = edj_unroll(value, nestlist->next);

		/* If nested is empty, either skip the row or allow the
		 * remainder of the loop to add a row containing the current
		 * object.
		 */
		if (!nested->first) {
			if (skipempty) {
				edj_free(nested);
				continue;
			}
			edj_append(nested, edj_object());
		}

		/* For each element of nested... */
		for (nrow = edj_first(nested); nrow; nrow = edj_next(nrow)) {
			/* Create a new object which combines members of the
			 * table row and the current nested row.
			 */
			row = edj_object();
			for (tmember = table->first; tmember; tmember = tmember->next) { /* object */
				/* Is this the unrolled element? */
				if (tmember->first == value) {
					/* Yes!  Replace it with copies of the
					 * nested object's members.  This is
					 * where the unrolling really happens.
					 */

					/* If this is the last row, we can
					 * recycle the members, but for other
					 * rows we need to make copies.  This
					 * works because nested was returned by
					 * edj_unroll() which always returns a
					 * copy of the data, never the original
					 * data.
					 */
					if (!edj_is_last(nrow) || edj_is_deferred_element(nrow)) {
						/* Append copies of the nested members */
						for (nmember = nrow->first; nmember; nmember = nmember->next) /* object */
							edj_append(row, edj_copy(nmember));
					} else {
						/* Last row, move nested members */
						edj_t *next;
						for (nmember = nrow->first; nmember; nmember = next) {
							next = nmember->next; /* object */
							nmember->next = NULL; /* object */
							edj_append(row, nmember);
						}
						nrow->first = NULL;/* so members won't be freed */
					}
				} else {
					/* Append a copy of this member */
					edj_append(row, edj_copy(tmember));
				}

			}

			/* Append the new row to the result array */
			edj_append(result, row);
		}

		/* Clean up.  We're reusing nested's elements, but we can free
		 * nested itself (the EDJ_ARRAY node) and its object shells
		 * (the EDJ_OBJECT nodes).
		 */
		edj_free(nested);
	}

	/* Return the result */
	return result;
}

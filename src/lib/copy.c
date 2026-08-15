#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <edj.h>

/* Even if memory debugging is enabled, here we're defining the non-debugging
 * version of the edj_copy() and edj_copy_filter() functions.
 */
#ifdef EDJ_DEBUG_MEMORY
# undef edj_copy
# undef edj_copy_filter
#endif

/* Return a deep copy of a json object... meaning that if "json" is a container
 * then its contents are deep-copied too.  The returned object will be identical
 * to the "json" object, but altering one will have no effect on the other.
 */
edj_t *edj_copy_filter(edj_t *json, int (*test)(edj_t *elem))
{
	edj_t *copy;
	edj_t *tail = NULL;
	edj_t *scan;
	edj_t *sub;

	/* Defend against NULL */
	if (!json)
		return NULL;

	/* If there's a test, apply it to this item */
	if (test && !test(json))
		return NULL;

	/* The top node's copy method depends on its type */
	switch (json->type)
	{
	  case EDJ_OBJECT:
		copy = edj_object();
		for (tail = NULL, scan = json->first; scan; scan = scan->next) /* object */
		{
			sub = edj_copy_filter(scan, test);
			if (!sub)
				continue;
			if (tail)
				tail->next = sub; /* object */
			else
				copy->first = sub;
			tail = sub;
		}
		break;

	  case EDJ_ARRAY:
		copy = edj_array();

		/* For deferred arrays without a test, keep it deferred */
		if (edj_is_deferred_array(json) && !test) {
			edj_t basic;
			edjdef_t *def = (edjdef_t *)json->first;
			copy->first = edj_defer(def->fns);

			/* We want to copy all data associated with this
			 * deferred array, except for "basic".  We keep
			 * "basic" separate so its memory tracking is
			 * independent.
			 */
			basic = *copy->first;
			memcpy(copy->first, json->first, def->fns->size);
			*copy->first = basic;

			/* If a file is referenced, this is a new reference */
			if (def->file)
				def->file->refs++;
			break;
		}

		/* Otherwise we scan, filter, and copy elements individually */
		for (scan = edj_first(json); scan; scan = edj_next(scan))
		{
			sub = edj_copy_filter(scan, test);
			if (!sub)
				continue;
			edj_append(copy, sub);
		}
		break;

	  case EDJ_KEY:
		return edj_key(json->text, edj_copy(json->first));

	  case EDJ_STRING:
	  case EDJ_BOOLEAN:
	  case EDJ_NULL:
		return edj_simple(json->text, -1, json->type);

	  case EDJ_NUMBER:
		/* Numbers can be represented internally either as a string of
		 * ASCII digits copied directly from a JSON document, or in
		 * binary.  This affects the way we copy it.
		 */
		if (json->text[0])
			return edj_number(json->text, -1);
		if (json->text[1] == 'i')
			return edj_from_int(EDJ_INT(json));
		return edj_from_double(EDJ_DOUBLE(json));

	  case EDJ_DEFER:
	  default:
	  	return NULL; /* should never happen */
	}

	/* Return the whole array or object */
	return copy;
}

edj_t *edj_copy(edj_t *json)
{
	/* !!! It'd be nice if a deferred array was copied as deferred too */
	return edj_copy_filter(json, NULL);
}

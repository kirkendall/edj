#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <edj.h>

/* Compare two objects, given a list of keys.  Return 1 if the first object
 * compares as higher, -1 if the second object compares as higher, or 0 if
 * they compare as equal.  orderby is a list of keys names (array of strings);
 * to select descending order for any key, insert a true symbol into the
 * list before the key string.
 */
int edj_compare(edj_t *obj1, edj_t *obj2, edj_t *orderby)
{
	edj_t *field1, *field2, *key;
	int	descending, isnull1, isnull2;
	double	diff;

	// Check parameters.
	if (obj1->type != EDJ_OBJECT || obj2->type != EDJ_OBJECT)
		;/* EEE "Records passed to edj_compare() must be objects" */
	if (orderby->type != EDJ_ARRAY && orderby->type != EDJ_STRING)
		;/* EEE "The field list passed to edj_compare() must be an array or a string" */

	// For each field...
	descending = 0;
	if (orderby->type == EDJ_ARRAY)
		key = orderby->first;
	else
		key = orderby;
	for (;key; key = key->next) { /* object */
		// If this is a "descending" flag, then just do that
		if (key->type == EDJ_BOOLEAN) {
			descending = edj_is_true(key);
			continue;
		}

		/* Silently ignore non-strings */
		if (key->type != EDJ_STRING)
			continue;

		/* Get the members for the next key */
		field1 = edj_by_expr(obj1, key->text, NULL, NULL, NULL);
		field2 = edj_by_expr(obj2, key->text, NULL, NULL, NULL);

		/* null comes after non-null, always */
		isnull1 = edj_is_null(field1);
		isnull2 = edj_is_null(field2);
		if (isnull1 || isnull2) {
			/* edj_by_expr() may encounter deferred arrays */
			edj_break(field1);
			edj_break(field2);

			/* Skip if both null */
			if (isnull1 && isnull2)
				continue;

			/* Otherwise, the non-null comes first */
			if (isnull1)
				return 1;
			return -1;
		}

		/* Compare, based on type.  Assume both are same type */
		if (field1->type == EDJ_NUMBER)
			diff = edj_double(field1) - edj_double(field2);
		else
			diff = strcasecmp(field1->text, field2->text);

		/* edj_by_expr() may encounter deferred arrays */
		edj_break(field1);
		edj_break(field2);

		/* If descending then invert */
		if (descending) {
			diff = -diff;
			descending = !descending;
		}

		/* If equal then skip to the next field */
		if (diff == 0)
			continue;

		/* Return only -1, 1 */
		return (diff < 0) ? -1 : 1;
	}

	/* We reached the end without finding any difference */
	return 0;
}

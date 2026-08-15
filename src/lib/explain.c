#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <locale.h>
#include <assert.h>
#include <edj.h>



/* Return a string describing the data type of an edj_t.  Possible return
 * values are:
 *   "boolean"	The symbols true and false.
 *   "null"	A NULL pointer, or the symbol null, or unexpected/error values
 *   "object"	Any object
 *   "number"	Any number
 *   "string"	Any other string
 *   "array"	Any other array
 * If "extended" is true, then it can refine the response as follows:
 *   "array*"	Empty array
 *   "table"	A non-empty array of objects
 *   "object*"  Empty object
 *   "date"     A string that looks like an ISO-8601 date
 *   "time"     A string that looks like an ISO-8601 time
 *   "datetime"	A string that looks like an ISO-8601 datetime
 *   "period"	A string that looks like an ISO-8601 period
 */
char *edj_typeof(edj_t *json, int extended)
{
	/* Defend against NULL */
	if (!json)
		return "null";

	/* First clue is the "type" field */
	switch (json->type) {
	  case EDJ_NUMBER:
		return "number";
	  case EDJ_OBJECT:
		if (!json->first && extended)
			return "object*";
		return "object";
	  case EDJ_BOOLEAN:
		return "boolean";
	  case EDJ_NULL:
		return "null";
	  case EDJ_STRING:
		/* Strings may be dates, times, or datetimes. Or just strings */
		if (extended) {
			if (!*json->text)
				return "string*";
			if (edj_is_date(json))
				return "date";
			if (edj_is_time(json))
				return "time";
			if (edj_is_datetime(json))
				return "datetime";
			if (edj_is_period(json))
				return "period";
		}
		return "string";
	  case EDJ_ARRAY:
		/* If it's a non-empty array of objects, call it a table.
		 * Otherwise, it's an array.
		 */
		if (extended) {
			if (!json->first)
				return "array*";
			if (edj_is_table(json))
				return "table";
		}
		return "array";
	  default:
		/* Shouldn't happen */
		return "null";
	}
}

/* Combine oldtype and newtype.  Try to keep the result as specific as
 * possible.  If total chaos, just return "any".
 */
char *edj_mix_types(char *oldtype, char *newtype)
{
	/* If typenames are the same, or oldtype is "any", it's easy */
	if (!strcmp(oldtype, newtype) || !strcmp(oldtype, "any"))
		return NULL;

	/* If oldtype is "null" then we have more info now! */
	if (!strcmp(oldtype, "null"))
		return newtype;

	/* If newtype is "null" then that doesn't give us any info */
	if (!strcmp(newtype, "null"))
		return oldtype;

	/* Bad XML conversions can't distinguish between empty strings,
	 * empty arrays, or empty objects.  Don't let an empty value mess up
	 * what we thought we knew about the type.
	 */
	if (strchr(newtype, '*'))
		return oldtype;
	if (strchr(oldtype, '*'))
		return newtype;

	/* Mixing "object*" and "object" is "object" */
	if (!strncmp(oldtype, "object", 6) && !strncmp(newtype, "object", 6))
		return "object";

	/* Mixing "table" and "array" is an "array" */
	if ((!strcmp(newtype, "table") || !strncmp(newtype, "array", 5))
	 && (!strcmp(oldtype, "table") || !strncmp(oldtype, "array", 5)))
		return "array";

	/* "date", "time", and "datetime" are all variations of string.
	 * If they don't match then "string" is the safest classification.
	 */
	if ((!strncmp(newtype, "date", 4) || !strcmp(newtype, "time") || !strcmp(newtype, "string"))
	 && (!strncmp(oldtype, "date", 4) || !strcmp(oldtype, "time") || !strcmp(oldtype, "string")))
		return "string";

	/* Chaos */
	return "any";
}

/* Collect column info from a single row and merge it into aggregated info. If
 * the aggregated info is NULL, allocate it.  Depth is 0 normally, or higher
 * values to allow embedded objects and tables (arrays of objects) to also
 * be explained -- 1 allows a single layer, 2 for 2 layers deep, etc.  -1
 * will allow any depth.
 *
 * Returns the updated aggregated data, as an array of objects describing each
 * column.  When the aggregated data is no longer needed, you must free it
 * via the usual edj_free() function.
 */
edj_t *edj_explain(edj_t *columns, edj_t *row, int depth)
{
	edj_t *col, *stats, *t, *t2;
	char	*newtype, *oldtype;
	int	newwidth, oldwidth;
	int	firstrow;
	char	number[40];

	/* If row isn't an object, then we can't do much with it */
	if (!row || row->type != EDJ_OBJECT)
		return columns;

	/* Allocate an array to store the columns, if we don't have one yet */
	firstrow = 0;
	if (!columns) {
		columns = edj_array();
		firstrow = 1;
	}

	/* For each column of the row ... */
	for (col = row->first; col; col = col->next) { /* object */
		assert(col->type == EDJ_KEY);

		/* Derive the type by examining the key's value */
		newtype = edj_typeof(col->first, 1);

		/* Locate the columns entry for this line, if any */
		for (stats = columns->first; stats; stats = stats->next) { /* undeferred */
			if ((t = edj_by_key(stats, "key")) != NULL
			 && t->type == EDJ_STRING
			 && !strcmp(t->text, col->text))
				break;
		}

		/* Are we updating existing stats for this column? */
		if (stats) {
			/* Yes, we have existing stats.  Update it */

			/* If newtype is "null" then the element is nullable */
			if (!strcmp(newtype, "null"))
				edj_append(stats, edj_key("nullable", edj_boolean(1)));

			/* Mixing types is  bit tricky */
			oldtype = edj_text_by_key(stats, "type");
			newtype = edj_mix_types(oldtype, newtype);
			if (newtype)
				edj_append(stats, edj_key("type", edj_string(newtype, -1)));
			newtype = oldtype;

			/* Get the new width.  The biggest complication here
			 * is that sometimes numbers are binary.
			 */
			if (col->first->type == EDJ_NUMBER && !col->first->text[0]) {
				if (col->first->text[1] == 'i')
					snprintf(number, sizeof number, "%d", EDJ_INT(col->first));
				else
					snprintf(number, sizeof number, "%.*g", edj_format_default.digits, EDJ_DOUBLE(col->first));
				newwidth = strlen(number);
			} else  {
				newwidth = edj_mbs_width(col->first->text);
			}

			/* Width can only increase */
			oldwidth = edj_int(edj_by_key(stats, "width"));
			if (newwidth > oldwidth)
				edj_append(stats, edj_key("width", edj_from_int(newwidth)));
		} else {
			/* No, this is a new column.  Add it. */
			stats = edj_object();
			edj_append(stats, edj_key("key", edj_string(col->text, -1)));
			edj_append(stats, edj_key("type", edj_string(newtype, -1)));
			if (col->first->type == EDJ_NUMBER && !col->first->text[0]) {
				if (col->first->text[1] == 'i')
					snprintf(number, sizeof number, "%d", EDJ_INT(col->first));
				else
					snprintf(number, sizeof number, "%.*g", edj_format_default.digits, EDJ_DOUBLE(col->first));
				newwidth = strlen(number);
			} else {
				newwidth = edj_mbs_width(col->first->text);
			}
			edj_append(stats, edj_key("width", edj_from_int(newwidth)));
			edj_append(stats, edj_key("nullable", edj_boolean(!strcmp(newtype, "null") || !firstrow)));
			edj_append(columns, stats);
#if 0
			oldtype = newtype;
#endif
		}

		/* Do we want to recurse for opjects/tables? */
		if (depth != 0) {
			if (!strcmp(newtype, "object") && col->first->type == EDJ_OBJECT) {
				t = edj_by_key(stats, "explain");
				t2 = edj_explain(t, col->first, depth - 1);
				if (!t)
					edj_append(stats, edj_key("explain", t2));
			} else if (!strcmp(newtype, "table") && col->first->type == EDJ_ARRAY) {
				t = edj_by_key(stats, "explain");
				for (t2 = col->first->first; t2; t2 = t2->next) { /* undeferred */
					t = edj_explain(t, t2, depth - 1);
				}
				if (edj_by_key(stats, "explain") != t)
					edj_append(stats, edj_key("explain", t));
			}
		}

	}

	/* If any known columns are missing from this row, assume the column
	 * is nullable.
	 */
	for (stats = columns->first; stats; stats = stats->next) { /* undeferred */
		if (edj_by_key(row, edj_text_by_key(stats, "key")) == NULL)
			edj_append(stats, edj_key("nullable", edj_boolean(1)));
	}

	/* Return the array of stats */
	return columns;
}

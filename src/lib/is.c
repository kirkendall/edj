#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <edj.h>

/* Test whether a JSON value is true.  Everything is true except for the
 * symbols "false" and "null", the number 0, an empty string, or an empty
 * array/object.
 */
int edj_is_true(edj_t *json)
{
        /* NULL pointer is false */
        if (!json)
		return 0;

        /* Otherwise it depends on type and value */
        switch (json->type) {
          case EDJ_BOOLEAN:
                return json->text[0] == 't';
	  case EDJ_NULL:
		return 0;
          case EDJ_STRING:
                return json->text[0] != '\0';
          case EDJ_NUMBER:
                if (json->text[0] == '0')
			return 0; /* 0 in string form */
		if (json->text[0] != '\0')
			return 1; /* non-0 in string form */
                if (json->text[1] == 'i')
			return EDJ_INT(json) != 0; /* binary integer */
                return EDJ_DOUBLE(json) != 0.0; /* binary double */
          case EDJ_ARRAY:
          case EDJ_OBJECT:
                return json->first != NULL;
          default: /* EDJ_KEY? Shouldn't happen */
                return 0;
        }
}

/* Test whether a JSON value is NULL.  This could be either because the
 * pointer is a NULL pointer (which generally means the value is absent
 * from an object) or the symbol "null".
 */
int edj_is_null(edj_t *json)
{
	return (!json || json->type == EDJ_NULL);
}

/* Test whether a JSON value is a NULL that represents an error. */
int edj_is_error(edj_t *json)
{
	return (json && json->type == EDJ_NULL && json->text[0]);
}

/* Test whether a JSON value is an array of objects */
int edj_is_table(edj_t *json)
{
	int	anydata;
	edj_t	*elem;

        /* Must be an array */
        if (!json || json->type != EDJ_ARRAY)
                return 0;

	/* If we already have an answer in ->text[1], use it */
	if (json->text[1] == 't')
		return 1;
	else if (json->text[1] == 'n')
		return 0;

        /* Every element must be a non-empty object. */
        anydata = 0;
        for (elem = edj_first(json); elem; elem = edj_next(elem)) {
                if (elem->type != EDJ_OBJECT) {
			edj_break(elem);
			json->text[1] = 'n';
                        return 0;
		}
		if (elem->first)
			anydata = 1;
	}
	if (!anydata) {
		json->text[1] = 'n';
		return 0;
	}

        /* Looks good. */
        json->text[1] = 't';
        return 1;
}


/* This is a recursive function to help edj_is_short().  It returns an
 * estimate of the length, but stops counting once the "oneline" threshold
 * has been crossed.  This is only approximate!
 */
static size_t shorthelper(edj_t *json, size_t oneline)
{
        size_t size = 0;

        while (json && size < oneline) {
		/* Assume deferred arrays are long */
		if (edj_is_deferred_array(json))
			return oneline;

                /* Text and punctuation */
                switch (json->type) {
                  case EDJ_STRING:
                        size += strlen(json->text) + 2; /* ignoring escapes */
                        break;
                  case EDJ_NUMBER:
			if (*json->text)
				size += strlen(json->text);
			else if (json->text[1] == 'i') {
				int i = EDJ_INT(json);
				if (i < 0) {
					size++; /* for "-" */
					i = -i;
				}
				if (i < 10)
					size += 1;
				else if (i < 100)
					size += 2;
				else if (i < 1000)
					size += 3;
				else
					size += 10;
			}
			else
				size += 10;
			break;
                  case EDJ_NULL:
			size += 4;
			break;
                  case EDJ_BOOLEAN:
                        size += strlen(json->text);
                        break;
                  case EDJ_KEY:
                        size += strlen(json->text) + 3;

			/* Handle "first" recursively */
			size += shorthelper(json->first, oneline);
			if (size >= oneline)
				return size;
                        break;
                  default:
                        size += 2; /* for "[]" or "{}" */

			/* Handle "first" recursively */
			size += shorthelper(json->first, oneline);
			if (size >= oneline)
				return size;
                }

                /* Handle "next" iteratively */
                size++; /* for "," */
                json = json->next; /* undeferred */
        }
        return size;
}

/* Test whether the serialized version of an expression is short.  This is
 * quick -- it stops counting once the non-short threshold has been crossed.
 * Also, it uses approximations so the "oneline" parameter is not precise.
 */
int edj_is_short(edj_t *json, size_t oneline)
{
        return shorthelper(json, oneline) < oneline;
}

/* Return 1 iff json looks like an ISO date string "YYYY-MM-DD" */
int edj_is_date(edj_t *json)
{
	if (!json || json->type != EDJ_STRING || !edj_str_date(json->text))
		return 0;
	return 1;
}

/* Return 1 iff json looks like an ISO time string "hh:mm:ss" */
int edj_is_time(edj_t *json)
{
	if (!json || json->type != EDJ_STRING || !edj_str_time(json->text))
		return 0;
	return 1;
}

/* Return 1 iff json looks like an ISO datetime string "YYYY-MM-DDThh:mm:ss" */
int edj_is_datetime(edj_t *json)
{
	if (!json || json->type != EDJ_STRING || !edj_str_datetime(json->text))
		return 0;
	return 1;
}

/* Return 1 iff json looks like an ISO period string "PnYnMnWnDTnHnMnS" */
int edj_is_period(edj_t *json)
{
	if (!json || json->type != EDJ_STRING || !edj_str_period(json->text))
		return 0;
	return 1;
}

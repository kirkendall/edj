#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <edj.h>


/* Compare two edj_t values for equality.  Return 1 if equal, 0 if different.
 * This compares type as well as value.  It can even compare arrays (same types
 * and values in the same order) and objects (same member names and values
 * in any order).
 */
int edj_equal(edj_t *j1, edj_t *j2)
{
        edj_t  *tmp;

        /* Trivial case */
        if (j1 == j2)
                return 1;

        /* Different types don't match */
        if (j1->type !=  j2->type)
                return 0;

        /* Compare types as appropriate */
        switch (j1->type) {
          case EDJ_BOOLEAN:
          case EDJ_STRING:
                /* Compare their literal text, case-sensitively. */
                return !strcmp(j1->text, j2->text);

	  case EDJ_NULL:
		return 1;

          case EDJ_NUMBER:
		/* Numbers may be binary or text. */
		if (j1->text[0] == '\0' && j1->text[1] == 'i'
		 && j2->text[0] == '\0' && j2->text[1] == 'i')
			return EDJ_INT(j1) == EDJ_INT(j2);
		if (j1->text[0] == '\0' && j1->text[1] == 'd'
		 && j2->text[0] == '\0' && j2->text[1] == 'd')
			return EDJ_DOUBLE(j1) == EDJ_DOUBLE(j2);
		return edj_double(j1) == edj_double(j2);

          case EDJ_ARRAY:
                /* Compare length, and values of elements. */
                if (edj_length(j1) != edj_length(j2))
                        return 0;
                for (j1 = edj_first(j1), j2 = edj_first(j2); j1 && j2; j1 = edj_next(j1), j2 = edj_next(j2)) {
                        if (!edj_equal(j1, j2)) {
				edj_break(j1);
				edj_break(j2);
                                return 0;
			}
                }
                return 1;

          case EDJ_OBJECT:
                /* Compare length, and values/names of members.  It's okay if
                 * the members are listed in a different order; we find them
                 * by name.
                 */
                if (edj_length(j1) != edj_length(j2))
                        return 0;
                for (j1 = j1->first; j1; j1 = j1->next) { /* object */
                        tmp = edj_by_key(j2, j1->text);
                        if (!tmp || !edj_equal(j1->first, tmp))
                                return 0;
                }
                return 1;

          default:
                /* shouldn't happen */
                return 0;
        }
}

#include <stdlib.h>
#include <stdio.h>
#include <edj.h>

/* Return the number of elements in an array, or members in an object */
int edj_length(edj_t *container)
{
	int	len;
	edj_t	*scan;

	/* Defend against NULL */
	if (!container)
		return 0;

	/* This is mostly for arrays, but we a special case we also allow it
	 * to count the members in an object.
	 */
	if (container->type == EDJ_OBJECT) {
		for (len = 0, scan = container->first; scan; scan = scan->next)
			len++;
		return len;
	}

	/* Defend against invalid arguments */
	if (container->type != EDJ_ARRAY) {
		/* EEE "Attempt to find length of a non-array" */
		return 0;
	}

	/* For arrays, we might be able to use a shortcut */
	if (container->type == EDJ_ARRAY) {
		if (container->first == 0)
			return 0;
		if (EDJ_ARRAY_LENGTH(container) > 0)
			return EDJ_ARRAY_LENGTH(container);
	}

	/* Count the elements */
	for (len = 0, scan = edj_first(container); scan; len++, scan = edj_next(scan)) {
	}

	/* Store the count */
	EDJ_ARRAY_LENGTH(container) = len;

	/* Return the count */
	return len;
}

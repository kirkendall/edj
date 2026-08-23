#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <edj.h>

/* This is the size of the hash table.  Don't change it, unless you also change
 * the hash calculation in jcsort(), below.
 */
#define HASHSIZE 1024

/* Elements with the same value in a sort field are collected in a linked list.
 * "arraybuf" is the head of that list, and "value" points to the member that
 * we're sorting by.
 */
typedef struct {
	edj_t	arraybuf;
	edj_t	*value;
	double	dvalue;
	int	nexthash;
} bucket_t;

/* Compare the sorting values for two buckets */
static int cmpascending(const void *v1, const void *v2)
{
	bucket_t *b1 = (bucket_t *)v1;
	bucket_t *b2 = (bucket_t *)v2;

	/* object/array/null/missing comes LAST */
	if (b1->value && !b2->value)
		return -1;
	if (b2->value && !b1->value)
		return 1;
	if (!b2->value)
		return 0;

	/* Booleans before numbers or strings */
	if (b1->value->type == EDJ_BOOLEAN && b2->value->type == EDJ_BOOLEAN)
		return strcmp(b1->value->text, b2->value->text);
	if (b1->value->type == EDJ_BOOLEAN || b2->value->type == EDJ_BOOLEAN)
		return -1;

	/* Strings before numbers */
	if (b1->value->type == EDJ_STRING && b2->value->type == EDJ_STRING)
		return edj_mbs_casecmp(b1->value->text, b2->value->text);
	if (b1->value->type == EDJ_STRING)
		return -1;
	if (b2->value->type == EDJ_STRING)
		return 1;

	/* Numbers */
	if (b1->dvalue < b2->dvalue)
		return -1;
	if (b1->dvalue > b2->dvalue)
		return 1;
	return 0;
}

/* Descending version of sort comparison.  We just swap arguments. */
static int cmpdescending(const void *v1, const void *v2)
{
	return cmpascending(v2, v1);
}

/* This helper function does the real sorting, after parameters have been
 * checked.
 */
static void jcsort(edj_t *array, edj_t *orderby, int grouping)
{
	edj_t	*elem, *value;
	int	descending;
	int	nbuckets, used, b, b2;
	bucket_t *bucket;
	double	dvalue;
	int	hash, hashtable[HASHSIZE], hashlength[HASHSIZE];
	char	*s;

	descending = 0;
	if (orderby && orderby->type == EDJ_BOOLEAN) {
		descending = edj_is_true(orderby);
		orderby = orderby->next; /* undeferred */
	}

	/* If no more keys to sort by, then do nothing */
	if (!orderby)
		return;

	/* Empty arrays and single-element arrays are inherently sorted. */
	if (!array->first || !array->first->next) { /* undeferred */
		/* If we're grouping, then we need to convert a single-element
		 * array into a nested subarray though, making it a group of 1.
		 */
		if (grouping && array->first) {
			elem = array->first;
			EDJ_END_POINTER(array) = array->first = edj_array();
			edj_append(array->first, elem);
		}
		return;
	}

	/* Start with an empty bucket array and empty hash table */
	nbuckets = used = 0;
	bucket = NULL;
	for (hash = 0; hash < HASHSIZE; hash++) {
		hashtable[hash] = -1;
		hashlength[hash] = 0;
	}

	/* Split the array elements out to buckets.  For strings, we do this
	 * in a case-sensitive way at this phase.
	 */
	while (array->first) {
#if 0
		/* If user aborted, then quit. !!! I need to clean up! */
		if (edj_interrupt)
			return;
#endif

		/* Pull the element out of the array */
		elem = array->first;
		array->first = elem->next; /* undeferred */
		elem->next = NULL; /* undeferred */

		/* Fetch its sort value. */
		value = edj_by_expr(elem, orderby->text, NULL, NULL, NULL);
		dvalue = 0.0;
		if (value && value->type == EDJ_NUMBER)
			dvalue = edj_double(value);

		/* Generate a hash number */
		if (value->type == EDJ_STRING) {
			for (hash = 1022, s = value->text; *s; s++)
				hash = ((hash << 3) ^ (*s & 0x1f) ^ (hash >> 7)) & 0x3ff;
		} else if (value->type == EDJ_NUMBER) {
			hash = ((int)dvalue % 1024) ^ ((int)(dvalue * 1024) % 1024) ;
		} else {
			hash = 1023;
		}

		/* Find a bucket for this value.  This uses a linear search
		 * so it can be inefficient -- adding n items takes O(n^2)
		 * time.  A hash table is used to cut that by a factor of 1000
		 * or so, but its still just a faster O(n^2).
		 */
		for (b = hashtable[hash]; b >= 0; b = bucket[b].nexthash) {
			if (!value && !bucket[b].value)
				break;
			else if (value
			      && bucket[b].value
			      && (value->type == EDJ_STRING || value->type == EDJ_BOOLEAN)
			      && bucket[b].value->type == value->type) {
				if (!strcmp(value->text, bucket[b].value->text))
					break;
			} else if (value
			      && value->type == EDJ_NUMBER
			      && bucket[b].value->type == EDJ_NUMBER) {
				if (dvalue == bucket[b].dvalue)
					break;
			} else if (!bucket[b].value)
				break; /* so arrays/objects/null all share a bucket */
		}

		/* If no bucket was found, allocate one */
		if (b < 0) {
			/* Maybe enlarge the bucket array */
			if (used == nbuckets) {
				nbuckets = used * 3 / 2 + 100;
				bucket = realloc(bucket, nbuckets * sizeof(bucket_t));
				memset(&bucket[used], 0, (nbuckets - used) * sizeof(bucket_t));
				for (b2 = used; b2 < nbuckets; b2++)
					bucket[b2].arraybuf.type = EDJ_ARRAY;
			}

			/* Set its value */
			b = used;
			bucket[b].value = value;
			bucket[b].dvalue = dvalue;
			bucket[b].nexthash = hashtable[hash];
			hashtable[hash] = b;
			hashlength[hash]++;
			used++;
		}

		/* Add this element to the bucket */
		edj_append(&bucket[b].arraybuf, elem);
	}

	/* Sort the buckets */
	if (descending)
		qsort(bucket, used, sizeof(bucket_t), cmpdescending);
	else
		qsort(bucket, used, sizeof(bucket_t), cmpascending);

#if 0
	/* Merge any buckets that have the same case-insensitive string value.
	 * We only have to do this for strings, and we know each bucket contains
	 * at least one item so EDJ_END_POINTER() is non-NULL.
	 */
	for (b = 0, b2 = 1; b2 < used; b2++) {
		if (bucket[b].value
		 && bucket[b].value->type == EDJ_STRING
		 && bucket[b2].value
		 && bucket[b2].value->type == EDJ_STRING
		 && !edj_mbs_casecmp(bucket[b].value->text, bucket[b2].value->text)) {
			/* Same, case-insensitively.  Append b2 to b */
			EDJ_END_POINTER(&bucket[b].arraybuf)->next = bucket[b2].arraybuf.first; /* undeferred */
			EDJ_END_POINTER(&bucket[b].arraybuf) = EDJ_END_POINTER(&bucket[b2].arraybuf);
			EDJ_ARRAY_LENGTH(&bucket[b].arraybuf) += EDJ_ARRAY_LENGTH(&bucket[b2].arraybuf);
		} else {
			b++;
		}
	}
	used = b + 1;
#endif

	/* If there are more sort keys, then recursively sort each bucket */
	if (orderby->next) { /* undeferred */
		for (b = 0; b < used; b++)
			jcsort(&bucket[b].arraybuf, orderby->next, grouping); /* undeferred */
	}

	/* Merge the buckets back into the array again.  For a non-grouping
	 * sort, we append all items into a single array.  For grouping, we
	 * add the groups to the result array instead of their elements, but
	 * that's slightly tricky if there were more keys to sort/group by.
	 */
	if (!grouping || orderby->next) { /* undeferred */
		/* normal non-grouping sort */
		array->first = bucket[0].arraybuf.first;
		EDJ_END_POINTER(array) = EDJ_END_POINTER(&bucket[0].arraybuf);
		EDJ_ARRAY_LENGTH(array) = EDJ_ARRAY_LENGTH(&bucket[0].arraybuf);
		for (b = 1; b < used; b++) {
			EDJ_END_POINTER(array)->next = bucket[b].arraybuf.first; /* undeferred */
			EDJ_END_POINTER(array) = EDJ_END_POINTER(&bucket[b].arraybuf);
			EDJ_ARRAY_LENGTH(array) += EDJ_ARRAY_LENGTH(&bucket[b].arraybuf);
		}
	} else {
		/* grouping, and this is the last sort/group key */
		EDJ_END_POINTER(array) = NULL;
		EDJ_ARRAY_LENGTH(array) = 0;
		for (b = 0; b < used; b++) {
			elem = edj_array();
			elem->first = bucket[b].arraybuf.first;
			EDJ_END_POINTER(elem) = EDJ_END_POINTER(&bucket[b].arraybuf);
			EDJ_ARRAY_LENGTH(elem) = EDJ_ARRAY_LENGTH(&bucket[b].arraybuf);
			edj_append(array, elem);
		}
	}

	/* Clean up */
	free(bucket);
}


/* Sort a JSON table (array of objects) in place, given a list of fields.
 * The orderby list should be an array of strings; you may also include
 * a boolean "true" before any field name to make it use descending sort.
 * The "grouping" parameter should be 0 for a normal sort, or 1 to group
 * items via nested arrays.
 */
void edj_sort(edj_t *array, edj_t *orderby, int grouping)
{
	edj_t	*check;
	int	anykeys;

	/* Check parameters. "array" must be a table, and "orderby" should
	 * be a list (array, or linked list of elements from an array) of
	 * field names and descending flags.
	 */
	if (!edj_is_table(array)) {
		/* EEE "edj_sort() should be passed an array of objects" */
		return;
	}
	if (edj_is_deferred_array(orderby)) {
		/* EEE "edj_sort() orderby should be an in-memory array (not deferred) */
		return;
	}
	if (orderby->type == EDJ_ARRAY)
		orderby = orderby->first;
	anykeys = 0;
	for (check = orderby; check; check = check->next) { /* undeferred */
		if (check->type == EDJ_STRING)
			anykeys++;
		else if (check->type != EDJ_BOOLEAN) {
			/* EEE edj_sort() key list must be strings and booleans */
			return;
		}
		else if (!check->next) { /* undeferred */
			/* EEE edj_sort() key list can't end with a boolean */
			return;
		}
	}
	if (!anykeys) {
		/* EEE Empty orderby list */
		return;
	}

	/* Sorting only works on in-memory tables (not deferred) */
	edj_undefer(array);

	/* Do the real sort */
	jcsort(array, orderby, grouping);
}

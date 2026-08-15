#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <edj.h>

typedef struct {
	int	index;
	int	count;
} edjcol_t;

typedef struct edjcommon_s {
	struct edjcommon_s *other;
	edj_t	*value;
	int	hash;
	int	inRow;	/* number of columns containing this value */
	edjcol_t	col[1];	/* expanded as necessary */
} edjcommon_t;
#define COMMONSIZE(inputs) (sizeof(edjcommon_t) + (inputs-1) * sizeof(edjin_t))

/* Add a single input array element to the cumulative info. */
static void commonHelper(edjcommon_t **hashTable, int hashBits, edj_t *value, int ncols, int colnum, int index, int style)
{
	/* Generate the hash value */
	int hash = edj_hash(value, 0);

	/* Derive the index into the hash table from the hash value */
	int h = (hash ^ (hash >> 14)) & ((1 << hashBits) - 1);

	/* Scan for this value in the hash table */
	edjcommon_t *common;
	for (common = hashTable[h]; common; common = common->other)
		if (common->hash == hash
		 && ((style & EDJ_COMMON_RECHECK) == 0 || edj_equal(common->value, value)) )
			break;

	/* If not found in the hash table, then insert it now */
	if (!common) {
		common = calloc(1, sizeof(*common) + (ncols - 1) * sizeof(edjcol_t *));
		common->value = value;
		common->hash = hash;
		common->other = hashTable[h];
		hashTable[h] = common;
	}

	/* Update info for this array element */
	if (common->col[colnum].count == 0)
		common->inRow++;
	common->col[colnum].count++;
	common->col[colnum].index = index;
}


edj_t *edj_common(const char **keys, edj_t **values, int style)
{
	edjcommon_t **hashTable;
	int	hashBits, a, i, h;
	edj_t	*scan, *result, *row, *cell, *in, *out;
	edjcommon_t *each;
	int	ncols;	/* number of values columns */

	/* Count the columns */
	for (ncols = 0; values[ncols]; ncols++) {
	}

	/* If not "force" then verify that each array contains only strings
	 * or numbers.
	 */

	/* Allocate the hash table */
	hashBits = edj_config_get_int("common", "hashTable");
	if (hashBits < 0)
		hashBits = 0;
	else if (hashBits > 16)
		hashBits = 16;
	hashTable = calloc(1 << hashBits, sizeof(edjcommon_t *));

	/* For each array... */
	for (a = 0; values[a]; a++) {
		/* For each element of the array... */
		for (i = 0, scan = edj_first(values[a]); scan; i++, scan = edj_next(scan)) {
			/* Add or increment the value */
			commonHelper(hashTable, hashBits, scan, ncols, a, i, style);
		}
	}
	ncols = a;

	/* Generate the result table, one row at a time */
	result = edj_array();
	in = edj_config_get("common", "in");
	out = edj_config_get("common", "out");
	for (h = 0; h < 1<<hashBits; h++) {
		for (each = hashTable[h]; each; each = each->other) {
			/* If supposed to omit, then skip it */
			if (each->inRow == a) {
				/* Full row */
				if ((style & EDJ_COMMON_ALL) == 0)
					continue;
			} else {
				/* Partial row */
				if ((style & EDJ_COMMON_NONE) == EDJ_COMMON_NONE)
					continue;
				if (each->inRow != 1 && (style & EDJ_COMMON_NONE) == EDJ_COMMON_IN_ONLY)
					continue;
				if (each->inRow != a - 1 && (style & EDJ_COMMON_NONE) == EDJ_COMMON_OUT_ONLY)
					continue;
			}

			/* Add the row. */
			row = edj_object();
			edj_append(row, edj_key("value", edj_copy(each->value)));
			for (a = 0; keys[a]; a++) {
				edjcol_t col = each->col[a];
				if (col.count == 0)
					cell = edj_copy(out);
				else if ((style & EDJ_COMMON_CHECK) == EDJ_COMMON_INDEX)
					cell = edj_from_int(col.index);
				else if ((style & EDJ_COMMON_CHECK) == EDJ_COMMON_COUNT)
					cell = edj_from_int(col.count);
				else if ((style & EDJ_COMMON_CHECK) == EDJ_COMMON_CHECK)
					cell = edj_string(each->inRow == 1 ? "\u2714" : "\u2713", -1);
				else
					cell = edj_copy(in);
				edj_append(row, edj_key(keys[a], cell));
			}
			edj_append(result, row);
		}
	}

	/* Maybe sort it.  One important detail: If the array is empty because
	 * we're only looking for statistics, then skip sorting because that'd
	 * mark the empty table as being a non-table.
	 */
	if ((style & EDJ_COMMON_NOSORT) != EDJ_COMMON_NOSORT && result->first) {
		edj_t *orderby = edj_string("value", -1);
		edj_sort(result, orderby, 0);
		edj_free(orderby);
	}

	/* Maybe append rows for statistics */
	if ((style & EDJ_COMMON_STATS) == EDJ_COMMON_STATS) {
		/* Overall totals */
		edj_t *row = edj_object();
		edj_append(row, edj_key("value", edj_string("length", -1)));
		for (a = 0; values[a]; a++)
			edj_append(row, edj_key(keys[a], edj_from_int(edj_length(values[a]))));
		edj_append(result, row);

		/* Count rows where this is the only "in" column */
		row = edj_object();
		edj_append(row, edj_key("value", edj_string("inOnly", -1)));
		for (a = 0; keys[a]; a++) {
			i = 0;
			for (h = 0; h < 1<<hashBits; h++) {
				for (each = hashTable[h]; each; each = each->other) {
					if (each->inRow == 1 && each->col[a].count > 0)
						i++;
				}
			}
			edj_append(row, edj_key(keys[a], edj_from_int(i)));
		}
		edj_append(result, row);

		/* Count rows where this is the only "out" column */
		row = edj_object();
		edj_append(row, edj_key("value", edj_string("outOnly", -1)));
		for (a = 0; keys[a]; a++) {
			i = 0;
			for (h = 0; h < 1<<hashBits; h++) {
				for (each = hashTable[h]; each; each = each->other) {
					if (each->inRow == ncols - 1 && each->col[a].count == 0)
						i++;
				}
			}
			edj_append(row, edj_key(keys[a], edj_from_int(i)));
		}
		edj_append(result, row);

		/* "in" counts across rows */
		row = edj_object();
		edj_append(row, edj_key("value", edj_string("spread", -1)));
		for (a = 0; keys[a]; a++) {
			i = 0;
			for (h = 0; h < 1<<hashBits; h++) {
				for (each = hashTable[h]; each; each = each->other) {
					if (each->inRow == a + 1)
						i++;
				}
			}
			edj_append(row, edj_key(keys[a], edj_from_int(i)));
		}
		edj_append(result, row);
	}

	/* Clean up */
	for (h = 0; h < 1<<hashBits; h++) {
		while (hashTable[h]) {
			each = hashTable[h]->other;
			free(hashTable[h]);
			hashTable[h] = each;
		}
	}
	free(hashTable);

	/* Return the table */
	return result;
}

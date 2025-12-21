#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <jx.h>

typedef struct {
	int	index;
	int	count;
} jxcol_t;

typedef struct jxcommon_s {
	struct jxcommon_s *other;
	jx_t	*value;
	int	hash;
	int	inRow;	/* number of columns containing this value */
	jxcol_t	col[1];	/* expanded as necessary */
} jxcommon_t;
#define COMMONSIZE(inputs) (sizeof(jxcommon_t) + (inputs-1) * sizeof(jxin_t))

/* Add a single input array element to the cumulative info. */
static void commonHelper(jxcommon_t **hashTable, int hashBits, jx_t *value, int ncols, int colnum, int index, int style)
{
	/* Generate the hash value */
	int hash = jx_hash(value, 0);

	/* Derive the index into the hash table from the hash value */
	int h = (hash ^ (hash >> 14)) & ((1 << hashBits) - 1);

	/* Scan for this value in the hash table */
	jxcommon_t *common;
	for (common = hashTable[h]; common; common = common->other)
		if (common->hash == hash
		 && ((style & JX_COMMON_RECHECK) == 0 || jx_equal(common->value, value)) )
			break;

	/* If not found in the hash table, then insert it now */
	if (!common) {
		common = calloc(1, sizeof(*common) + (ncols - 1) * sizeof(jxcol_t *));
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


jx_t *jx_common(const char **keys, jx_t **values, int style)
{
	jxcommon_t **hashTable;
	int	hashBits, a, i, h;
	jx_t	*scan, *result, *row, *cell, *in, *out;
	jxcommon_t *each;
	int	ncols;	/* number of values columns */

	/* Count the columns */
	for (ncols = 0; values[ncols]; ncols++) {
	}

	/* If not "force" then verify that each array contains only strings
	 * or numbers.
	 */

	/* Allocate the hash table */
	hashBits = jx_config_get_int("common", "hashTable");
	if (hashBits < 0)
		hashBits = 0;
	else if (hashBits > 16)
		hashBits = 16;
	hashTable = calloc(1 << hashBits, sizeof(jxcommon_t *));

	/* For each array... */
	for (a = 0; values[a]; a++) {
		/* For each element of the array... */
		for (i = 0, scan = jx_first(values[a]); scan; i++, scan = jx_next(scan)) {
			/* Add or increment the value */
			commonHelper(hashTable, hashBits, scan, ncols, a, i, style);
		}
	}
	ncols = a;

	/* Generate the result table, one row at a time */
	result = jx_array();
	in = jx_config_get("common", "in");
	out = jx_config_get("common", "out");
	for (h = 0; h < 1<<hashBits; h++) {
		for (each = hashTable[h]; each; each = each->other) {
			/* If supposed to omit, then skip it */
			if (each->inRow == a) {
				/* Full row */
				if ((style & JX_COMMON_ALL) == 0)
					continue;
			} else {
				/* Partial row */
				if ((style & JX_COMMON_NONE) == JX_COMMON_NONE)
					continue;
				if (each->inRow != 1 && (style & JX_COMMON_NONE) == JX_COMMON_IN_ONLY)
					continue;
				if (each->inRow != a - 1 && (style & JX_COMMON_NONE) == JX_COMMON_OUT_ONLY)
					continue;
			}

			/* Add the row. */
			row = jx_object();
			jx_append(row, jx_key("value", jx_copy(each->value)));
			for (a = 0; keys[a]; a++) {
				jxcol_t col = each->col[a];
				if (col.count == 0)
					cell = jx_copy(out);
				else if ((style & JX_COMMON_CHECK) == JX_COMMON_INDEX)
					cell = jx_from_int(col.index);
				else if ((style & JX_COMMON_CHECK) == JX_COMMON_COUNT)
					cell = jx_from_int(col.count);
				else if ((style & JX_COMMON_CHECK) == JX_COMMON_CHECK)
					cell = jx_string(each->inRow == 1 ? "\u2714" : "\u2713", -1);
				else
					cell = jx_copy(in);
				jx_append(row, jx_key(keys[a], cell));
			}
			jx_append(result, row);
		}
	}

	/* Maybe sort it.  One important detail: If the array is empty because
	 * we're only looking for statistics, then skip sorting because that'd
	 * mark the empty table as being a non-table.
	 */
	if ((style & JX_COMMON_NOSORT) != JX_COMMON_NOSORT && result->first) {
		jx_t *orderby = jx_string("value", -1);
		jx_sort(result, orderby, 0);
		jx_free(orderby);
	}

	/* Maybe append rows for statistics */
	if ((style & JX_COMMON_STATS) == JX_COMMON_STATS) {
		/* Overall totals */
		jx_t *row = jx_object();
		jx_append(row, jx_key("value", jx_string("length", -1)));
		for (a = 0; values[a]; a++)
			jx_append(row, jx_key(keys[a], jx_from_int(jx_length(values[a]))));
		jx_append(result, row);

		/* Count rows where this is the only "in" column */
		row = jx_object();
		jx_append(row, jx_key("value", jx_string("inOnly", -1)));
		for (a = 0; keys[a]; a++) {
			i = 0;
			for (h = 0; h < 1<<hashBits; h++) {
				for (each = hashTable[h]; each; each = each->other) {
					if (each->inRow == 1 && each->col[a].count > 0)
						i++;
				}
			}
			jx_append(row, jx_key(keys[a], jx_from_int(i)));
		}
		jx_append(result, row);

		/* Count rows where this is the only "out" column */
		row = jx_object();
		jx_append(row, jx_key("value", jx_string("outOnly", -1)));
		for (a = 0; keys[a]; a++) {
			i = 0;
			for (h = 0; h < 1<<hashBits; h++) {
				for (each = hashTable[h]; each; each = each->other) {
					if (each->inRow == ncols - 1 && each->col[a].count == 0)
						i++;
				}
			}
			jx_append(row, jx_key(keys[a], jx_from_int(i)));
		}
		jx_append(result, row);

		/* "in" counts across rows */
		row = jx_object();
		jx_append(row, jx_key("value", jx_string("spread", -1)));
		for (a = 0; keys[a]; a++) {
			i = 0;
			for (h = 0; h < 1<<hashBits; h++) {
				for (each = hashTable[h]; each; each = each->other) {
					if (each->inRow == a + 1)
						i++;
				}
			}
			jx_append(row, jx_key(keys[a], jx_from_int(i)));
		}
		jx_append(result, row);
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

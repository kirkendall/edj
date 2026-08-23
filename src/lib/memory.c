/* Memory.c
 *
 * This file contains a variety of low-level edj_t allocation functions,
 * and the edj_free() function for deallocating them.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>
#include <math.h>
#include <edj.h>

/* Here we need to access the "real" allocation/free functions */
#ifdef EDJ_DEBUG_MEMORY
# undef edj_free
# undef edj_simple
# undef edj_string
# undef edj_number
# undef edj_from_int
# undef edj_from_double
# undef edj_boolean
# undef edj_null
# undef edj_error_null
# undef edj_array
# undef edj_key
# undef edj_object
# undef edj_defer
# undef edj_first
# undef edj_parse_string
# undef edj_copy
# undef edj_copy_filter
# undef edj_calc
#endif

/* For debugging, this is used to store places that allocate memory, and
 * how many nodes were allocated there without freeing.  If a program links
 * with this library without defining EDJ_DEBUG_MEMORY then this will be
 * unused, but it's fairly small.
 */
typedef struct
{
        const char *file;
        int  line;
        int  count;
} memory_tracker_t;
static memory_tracker_t *memory_tracker;

/* This counts the number of edj_t's currently allocated.  Not threadsafe! */
int edj_debug_count = 0;

/* Return an estimated byte count for a given edj_t tree */
size_t edj_sizeof(edj_t *json)
{
        size_t size = 0;
        size_t len;

        while (json) {
                size += sizeof(edj_t);

                switch (json->type) {
                  case EDJ_STRING:
                  case EDJ_NUMBER:
                  case EDJ_BOOLEAN:
                  case EDJ_NULL:
                  case EDJ_KEY:
                        /* Add the text length including the terminating \0,
                         * tweaked for alignment
                         */
                        len = ((strlen(json->text) - sizeof(json->text)) | 0x1f) + 1;
                        size += len;
                        break;

                  case EDJ_DEFER:
			/* Rough guess: twice the size of an edj_t.  We count
			 * one edj_t after this switch, so we just add 1 here.
			 */
			size += sizeof(edj_t);
			break;

                  case EDJ_ARRAY:
                  case EDJ_OBJECT:
                  case EDJ_BADTOKEN:
                  case EDJ_NEWLINE:
                  case EDJ_ENDARRAY:
                  case EDJ_ENDOBJECT:
                        ; /* Listed just to keep the compiler happy */
                }

                /* Recursively add the "first" data */
                size += edj_sizeof(json->first);

                /* Iteratively add the "next" data */
                json = json->next; /* undeferred */
        }

        return size;
}

/* Free a JSON data tree */
void edj_free(edj_t *json)
{
        edj_t *next;

	/* Defend against NULL */
	if (!json)
		return;

        /* If this was allocated with memory debugging turned on, but freed
         * where it isn't turned on, then complain.
         */
	assert(json->memslot == 0 || memory_tracker[json->memslot].line != 0);

	/* If ->next points to an EDJ_DEFER, that means this edj_t is an
	 * element of a deferred array, currently being scanned.  Free the
	 * resources used for this scan session.
	 */
	if (json->next && json->next->type == EDJ_DEFER) {
		edjdeffns_t *fns = (edjdeffns_t *)json->next;
		if (fns->free)
			(*fns->free)(json);
	}

	/* Iteratively free this node and its siblings */
	while (json) {

		/* If this is a EDJ_DEFER array, then free its resources
		 * specially
		 */
		if (json->first && json->first->type == EDJ_DEFER) {
			edjdef_t *def = (edjdef_t *)json->first;
			if (def->fns->free)
				(*def->fns->free)(json);
		}

		/* Recursively free contained data.  Note that EDJ_NULL nodes
		 * abuse the ->first field to store a pointer into source text
		 * instead of an edj_t.
		 */
		if (json->type != EDJ_NULL)
			edj_free(json->first);

		/* Free this edj_t struct */
		next = json->next; /* undeferred */
		free(json);
		edj_debug_count--;
		json = next;
	}
}

/* Allocate an edj_t node and initialize some fields */
edj_t *edj_simple(const char *str, size_t len, edjtype_t type)
{
	edj_t *json;
	size_t  size;

	/* String is optional.  If omitted then use "" as a placeholder */
	if (!str)
		len = 0;
	else if (len == (size_t)-1)
		len = strlen(str);

        /* Compute the size, rounding up to a multiple of 32 */
        size = sizeof(edj_t) - sizeof json->text + len + 1;
        size = (size | 0x1f) + 1;

	/* Allocate it.  Trust malloc() to be efficient */
        json = malloc(size);

	/* Initialize the fields */
	memset(json, 0, size);
	json->type = type;
	if (str)
		strncpy(json->text, str, len);

	/* return it */
	edj_debug_count++;
	return json;
}

/* Allocate an edj_t for a given string.  Note that any escape sequences
 * such as \n or \u22c8 are handled by the parser via edj_mbs_unescape()
 * so we just get the actual data here.  Passing -1 for len causes it to
 * compute the length via strlen().  "str" is not required to have a
 * terminating '\0' but the returned json->text field will.
 */
edj_t *edj_string(const char *str, size_t len)
{
	return edj_simple(str, len, EDJ_STRING);
}

/* Allocate an edj_t for a given number, expressed as a string.  If you pass
 * -1 for len, it'll compute the length via strlen().
 */
edj_t *edj_number(const char *str, size_t len)
{
	return edj_simple(str, len, EDJ_NUMBER);
}

/* Allocate an edj_t for a given integer */
edj_t *edj_from_int(int i)
{
	edj_t *json = edj_number("", 0);
	json->text[1] = 'i';
	EDJ_INT(json) = i;
	return json;
}

/* Allocate an edj_t for a given floating-point number */
edj_t *edj_from_double(double f)
{
	edj_t *json = edj_number("", 0);
	json->text[1] = 'd';
	EDJ_DOUBLE(json) = f;
	return json;
}

/* Allocate an edj_t for a boolean value. */
edj_t *edj_boolean(int boolean)
{
	if (boolean)
		return edj_simple("true", 4, EDJ_BOOLEAN);
	else
		return edj_simple("false", 5, EDJ_BOOLEAN);
}

/* Allocate an edj_t for a null value */
edj_t *edj_null(void)
{
	return edj_simple("", 0, EDJ_NULL);
}

/* Allocate an edj_t for a null value, encoding an error message */
edj_t *edj_error_null(const char *where, const char *fmt, ...)
{
	char	buf[200], *bigbuf;
	int	len;
	va_list	ap;
	edj_t	*result;

	/* First try it in a modest buffer.  Usually works. */
	va_start(ap, fmt);
	len = vsnprintf(buf, sizeof buf, fmt, ap);
	va_end(ap);
	if (len < 0)
		return edj_null();
	if (len <= sizeof buf - 1) {
		result = edj_simple(buf, len, EDJ_NULL);
		result->first = (edj_t *)where;
		return result;
	}

	/* Allocate a larger buffer to hold the string, and use it */
	bigbuf = malloc(len);
	va_start(ap, fmt);
	vsnprintf(bigbuf, len, fmt, ap);
	va_end(ap);
	result = edj_simple(buf, len, EDJ_NULL);
	free(bigbuf);
	result->first = (edj_t *)where;
	return result;
}

/* Allocate an edj_t for a key.  The value must be non-NULL (though it can be
 * edj_null() ).  Later, you can use edj_append() to change the value.
 */
edj_t *edj_key(const char *key, edj_t *value)
{
	assert(value != NULL);

	/* Allocate it with twice as much space for storing the key's name.
	 * This is so we can also store the simplified version later, if
	 * necessary.
	 */
	edj_t *json = edj_simple(key, strlen(key) * 2 + 1, EDJ_KEY);
	json->first = value;
	return json;
}

/* Allocate an edj_t for an empty object */
edj_t *edj_object()
{
	return edj_simple(NULL, 0, EDJ_OBJECT);
}

/* Allocate an edj_t for an empty array */
edj_t *edj_array()
{
	return edj_simple(NULL, 0, EDJ_ARRAY);
}

/* This is a dummy type of deferred array which is always empty.  It is used
 * when edj_defer() is called with NULL instead of a real list of deferred
 * functions.
 */
static edj_t *dummy(edj_t *node)
{
	return NULL;
};
static edjdeffns_t dummyfns = {
	sizeof(edj_t) + sizeof(edjdeffns_t),	/* size (of the whole more-than-edj_t */
	"Dummy",	/* desc */
	dummy,		/* first */
	dummy,		/* next */
	NULL,		/* islast */
	NULL,		/* free */
	NULL,		/* byindex */
	NULL		/* bykey */
};

/* Allocate a EDJ_DEFER node.  "fns" is a collection of function pointers
 * that implement the desired type of deferred array, or NULL to use a
 * dummy set of functions.
 */
edj_t *edj_defer(edjdeffns_t *fns)
{
	edj_t *json;
	size_t	size;

	/* If no "fns" then use dummy */
	if (!fns)
		fns = &dummyfns;

	/* Allocate it, with extra space.  Note that we must tweak the size
	 * because edj_simple wants to be passed the size of the "text" field,
	 * but fns->size is the size of the whole thing.
	 */
	size = fns->size - sizeof(edj_t) + sizeof json->text;
	json = edj_simple("", size, EDJ_DEFER);

	/* Store the fns pointer, with this deferred array's implementation
	 * functions.  The rest of the edjdef_t is already initialized to 0's
	 */
	((edjdef_t *)json)->fns = fns; 

	/* Return it */
	return json;
}

/******************************************************************************/

/* For debugging memory issues, this function is called when the program exits
 * to check for memory leaks.
 */
static void memory_check_leaks(void)
{
        int     i;
        if (!memory_tracker)
		return;
#ifdef EDJ_DEBUG_MEMORY
	edj_debug_free(__FILE__, __LINE__, edj_system);
#endif
        for (i = 0; i < 4096; i++)
                if (memory_tracker[i].count > 0)
                        fprintf(stderr, "%s:%d: Leaked %d edj_t's\n", memory_tracker[i].file, memory_tracker[i].line, memory_tracker[i].count);
        if (memory_tracker[4096].count > 0)
                fprintf(stderr, "Leaked %d edj_t's from an untracked source\n", memory_tracker[4096].count);
}

/* For debugging, this looks for a slot for counting allocations from a given
 * source line.
 */
static int memory_slot(const char *file, int line)
{
        int     slot, start;

        /* If memory tracking hasn't been initialized, then do so now */
        if (!memory_tracker) {
                /* Allocate memory for the tracker.  Each edj_t has a 12-bit
                 * field for tracking its allocation source, so we want 4096
                 * tracker slots.  We also want one more slot in case there
                 * are more than 4096 source lines that allocate edj_t's.
                 */
                memory_tracker = calloc(4097, sizeof(memory_tracker_t));

                /* Arrange for memory leaks to be reported at exit */
                atexit(memory_check_leaks);
        }

        /* Choose a slot for this source line's counter */
        start = slot = abs(line) % 4096;
        if (slot == 0)
                slot++; /* slot 0 is reserved for uncounted allocations */
        do {
                /* Found an empty slot */
                if (!memory_tracker[slot].file) {
                        memory_tracker[slot].file = file;
                        memory_tracker[slot].line = line;
                        return slot;
                }

                /* Found an existing slot for this file/line */
                if (memory_tracker[slot].file == file && memory_tracker[slot].line == line) {
                        return slot;
		}

                /* Bumped to next slot */
                slot = (slot & 0xfff) + 1;
        } while (slot != start);

        /* If we get here then we looped without ever finding the slot or an
         * empty slot.  All we can do is count it in the overflow area.
         */
        return 0;
}


/* The following are debugging wrappers around the above functions.  They are
 * normally only called if the source program defines EDJ_DEBUG_MEMORY but
 * we want to use the same library whether debugging is being used or not,
 * so these are defined unconditionally.
 */
void edj_debug_free(const char *file, int line, edj_t *json)
{
	edj_t *next;

        /* Iterate over the ->next links */
        for (; json; json = next)
        {
		next = json->next; /* undeferred */

		/* If it looks like it was already freed, then complain.
		 * This isn't reliable!  It won't reject valid free's but it
		 * might miss some invalid ones, if the memory was recycled.
		 */
		if (json->type == EDJ_BADTOKEN) {
			fprintf(stderr, "%s:%d: Attempt to free memory twice\n", file, line);
			if (json->memslot)
				fprintf(stderr, "%s:%d: This is where it was first freed\n", memory_tracker[json->memslot].file, -memory_tracker[json->memslot].line);
		}

		/* We track by where the edj_t is allocated not by where it
		 * is freed.  Decrement the allocation count for that line.
		 */
		int slot = json->memslot;
		if (slot != 0 && memory_tracker[slot].count == 0 ) {
			if (memory_tracker[slot].line < 0)
				fprintf(stderr, "%s:%d: Attempt to re-free memory first freed at %s:%d (slot %d)\n", file, line, memory_tracker[slot].file, -memory_tracker[slot].line, slot);
			else
				fprintf(stderr, "%s:%d: Attempt to re-free memory allocated at %s:%d (slot %d)\n", file, line, memory_tracker[slot].file, memory_tracker[slot].line, slot);
			abort();
		}
		else if (memory_tracker)
			memory_tracker[slot].count--;

		/* Free the ->first link recursively... except that an error
		 * "null" uses ->first for the position of the error, so we
		 * don't want to free that.
		 */
		if (!edj_is_error(json))
			edj_debug_free(file, line, json->first);

		/* Free this node */
		json->first = json->next = NULL; /* undeferred */
		json->memslot = memory_slot(file, -line);
		edj_free(json);
	}
}

edj_t *edj_debug_simple(const char *file, int line, const char *str, size_t len, edjtype_t type)
{
        edj_t  *json;
        int slot = memory_slot(file, line);
        memory_tracker[slot].count++;
        json = edj_simple(str, len, type);
        json->memslot = slot;
        return json;
}

/* Allocate an edj_t for a given string. */
edj_t *edj_debug_string(const char *file, int line, const char *str, size_t len)
{
        return edj_debug_simple(file, line, str, len, EDJ_STRING);
}

/* Allocate an edj_t for a given number, expressed as a string */
edj_t *edj_debug_number(const char *file, int line, const char *str, size_t len)
{
	return edj_debug_simple(file, line, str, len, EDJ_NUMBER);
}

/* Allocate an edj_t for a given integer */
edj_t *edj_debug_from_int(const char *file, int line, int i)
{
	edj_t	*json = edj_debug_number(file, line, "", 0);
	json->text[1] = 'i';
	EDJ_INT(json) = i;
	return json;
}

/* Allocate an edj_t for a given floating-point number */
edj_t *edj_debug_from_double(const char *file, int line, double f)
{
	edj_t	*json = edj_debug_number(file, line, "", 0);
	json->text[1] = 'd';
	EDJ_DOUBLE(json) = f;
	return json;
}


/* Allocate an edj_t for a given boolean */
edj_t *edj_debug_boolean(const char *file, int line, int boolean)
{
	if (boolean)
		return edj_debug_simple(file, line, "true", 4, EDJ_BOOLEAN);
	else
		return edj_debug_simple(file, line, "false", 5, EDJ_BOOLEAN);
}

/* Allocate an edj_t for null */
edj_t *edj_debug_null(const char *file, int line)
{
	return edj_debug_simple(file, line, "", 0, EDJ_NULL);
}

/* Allocate an edj_t for a given error */
edj_t *edj_debug_error_null(const char *file, int line, char *fmt, ...)
{
	char	buf[200], *bigbuf;
	int	len;
	va_list	ap;
	edj_t	*result;

	/* First try it in a modest buffer.  Usually works. */
	va_start(ap, fmt);
	len = vsnprintf(buf, sizeof buf, fmt, ap);
	va_end(ap);
	if (len < 0)
		return edj_debug_null(file, line);
	if (len <= sizeof buf)
		return edj_debug_simple(file, line, buf, len - 1, EDJ_NULL);

	/* Allocate a larger buffer to hold the string, and use it */
	bigbuf = malloc(len);
	va_start(ap, fmt);
	vsnprintf(bigbuf, len, fmt, ap);
	va_end(ap);
	result = edj_debug_simple(file, line, buf, len - 1, EDJ_NULL);
	free(bigbuf);
	return result;
}

/* Allocate an edj_t for a key.  If value is non-NULL, then it will be used
 * as the value associated with the key.
 */
edj_t *edj_debug_key(const char *file, int line, const char *key, edj_t *value)
{
	/* Allocate double the space for the key name, so we have a place to
	 * put the "loose" version from edj_mbs_simple_key().
	 */
	edj_t *json = edj_debug_simple(file, line, key, strlen(key) * 2 + 1, EDJ_KEY);
	json->first = value;
	return json;
}

/* Allocate an edj_t for an empty object */
edj_t *edj_debug_object(const char *file, int line)
{
	return edj_debug_simple(file, line, NULL, 0, EDJ_OBJECT);
}

/* Allocate an edj_t for an empty array */
edj_t *edj_debug_array(const char *file, int line)
{
	return edj_debug_simple(file, line, NULL, 0, EDJ_ARRAY);
}

/* Allocate a EDJ_DEFER node */
edj_t *edj_debug_defer(const char *file, int line, edjdeffns_t *fns)
{
	edj_t *json;
	size_t	size;

	/* If no "fns" then use dummy */
	if (!fns)
		fns = &dummyfns;
		
	/* Allocate it, with extra space for an overall size of fns->size */
	size = fns->size - sizeof json->text;
	json = edj_debug_simple(file, line, "", size, EDJ_DEFER);

	/* Store the fns pointer, with this deferred array's implementation
	 * functions.  The rest of the edjdef_t is already initialized to 0's
	 */
	((edjdef_t *)json)->fns = fns; 

	/* Return it */
	return json;
}


/* This is called via edj_walk() to tweak the source line for tracking memory leaks */
static int fixslot(edj_t *json, void *data)
{
	int	slot = *(int *)data;

	/* If already fixed, leave it */
	if (json->memslot == slot)
		return 0;

	/* Change it, adjusting counts too */
	if (json->memslot != 0)
		memory_tracker[json->memslot].count--;
	if (slot != 0)
		memory_tracker[slot].count++;
	json->memslot = slot;
	return 0;
}

/* Parse a string and return it */
edj_t *edj_debug_parse_string(const char *file, int line, const char *str)
{
        edj_t *json;
        int slot = memory_slot(file, line);
        json = edj_parse_string(str);
        edj_walk(json, fixslot, &slot);
        return json;
}

/* Find the first element of a (possibly deferred) array */
edj_t *edj_debug_first(const char *file, int line, edj_t *array)
{
	int slot = memory_slot(file, line);
	edj_t *json = edj_first(array);
	if (edj_is_deferred_element(json)) {
		fixslot(json, &slot);
		fixslot(json->next, &slot);
	}
	return json;
}

/* Do a deep copy of an edj_t tree */
edj_t *edj_debug_copy(const char *file, int line, edj_t *json)
{
        int slot = memory_slot(file, line);
        json = edj_copy(json);
        edj_walk(json, fixslot, &slot);
        return json;
}

/* Do a deep copy of an edj_t tree, filtering items through function */
edj_t *edj_debug_copy_filter(const char *file, int line, edj_t *json, int (*test)(edj_t *elem))
{
        int slot = memory_slot(file, line);
        json = edj_copy_filter(json, test);
        edj_walk(json, fixslot, &slot);
        return json;
}

/* Evaluate an expression */
edj_t *edj_debug_calc(const char *file, int line, edjcalc_t *calc, edjcontext_t *context, void *agdata)
{
	int slot = memory_slot(file, line);
	edj_t *json = edj_calc(calc, context, agdata);
	edj_walk(json, fixslot, &slot);
	return json;
}

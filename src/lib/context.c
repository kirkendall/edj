#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <assert.h>
#include <sys/stat.h>
#include <edj.h>
#include "error.h"

/* This file defines the context functions.  Contexts are used in edj_calc()
 * to provide access to outside data, in the manner of variables.  The
 * functions here let you manage a stack of contexts, and search for named
 * values defined within the context, and assign new values to variables.
 */

/* This datatype is used to maintain a list functions to call when creating
 * a new context.  The list is built by edjc_context_hook, and used by
 * edj_context_std().
 */
typedef struct contexthook_s {
	struct contexthook_s *other;
	edjcontext_t *(*addcontext)(edjcontext_t *context);
} contexthook_t;

static contexthook_t *extralayers = NULL;

/* Add a function which may add 0 or more layers to the standard context.
 * This is mostly intended to allow plugins to define symbols that should
 * be globally accessible to edjcalc.  The edjcalc program itself may
 * use it for adding the autoload directory.
 */
void edj_context_hook(edjcontext_t *(*addcontext)(edjcontext_t *context))
{
	contexthook_t *hook = malloc(sizeof(contexthook_t));
	hook->addcontext = addcontext;
	hook->other = extralayers;
	extralayers = hook;
}

/* Dump the context stack in a human-readable format, for debugging.  In the
 * GDB debugger, you can invoke this as "call edj_context_dump(context)"
 */
void edj_context_dump(edjcontext_t *context)
{
	char	*str;
	for (; context; context = context->older) {
		if (!context->data)
			str = "";
		else if (context->data == edj_system)
			str = "edj_system";
		else
			str = (context->data ? edj_serialize(context->data, NULL) : "");
		printf("%c%c%c%c%c%c%c%c%c%c%c %s\n",
			context->autoload ? 'a' : ' ',
			context->modified ? 'm' : ' ',
			context->flags & EDJ_CONTEXT_NOFREE ? 'F' : ' ',
			context->flags & EDJ_CONTEXT_VAR ? 'V' : ' ',
			context->flags & EDJ_CONTEXT_CONST ? 'C' : ' ',
			context->flags & EDJ_CONTEXT_GLOBAL ? 'G' : ' ',
			context->flags & EDJ_CONTEXT_THIS ? 'T' : ' ',
			context->flags & EDJ_CONTEXT_DATA ? 'D' : ' ',
			context->flags & EDJ_CONTEXT_ARGS ? 'A' : ' ',
			context->flags & EDJ_CONTEXT_NOCACHE ? 'N' : ' ',
			context->flags & EDJ_CONTEXT_MODIFIED ? 'M' : ' ',
			str);
		if (*str && *str != 'j')
			free(str);
	}
}

/* This callback is called if the current file's parsed data gets modified */
static void data_modified(edjcontext_t *layer, edjcalc_t *lvalue)
{
	/* Users/scripts can modify data either via the "data" variable,
	 * or by modifying "this" layer above it.  They both refer to the
	 * contents of the current file.  For consistency, we only want to
	 * set the MODIFIED flag on the "data" layer, so if we're given
	 * the "this" layer then switch to the "data" layer.
	 */
	if (layer->flags & EDJ_CONTEXT_THIS)
		layer = layer->older;

	/* Set the MODIFIED flag */
	layer->flags |= EDJ_CONTEXT_MODIFIED;
}

/* Free a single layer of context.  Also frees the data associated with it,
 * unless context->flags has the EDJ_CONTEXT_NOFREE bit set.  Returns the
 * next layer down in the context stack.
 */
edjcontext_t *edj_context_free(edjcontext_t *context)
{
        edjcontext_t *older;

        /* Defend against NULL */
        if (!context)
                return NULL;

        /* If supposed to free data, do that. */
        if ((context->flags & EDJ_CONTEXT_NOFREE) == 0) {
		edj_free(context->data);
	}

        /* Free the context */
        older = context->older;
        free(context);

        return older;
}

/* Add a context.  "older" is a lower context layer or NULL if there is none.
 * "data" is data (usually an object) associated with the layer, for things
 * like local variables; it may be NULL too.  "flags" is a bitwise-OR of the
 * following, but in practice is often 0:
 *   EDJ_CONTEXT_NOFREE  Don't free the data when context is freed
 *   EDJ_CONTEXT_VAR	  Variable -- use with GLOBAL for non-local
 *   EDJ_CONTEXT_CONST	  Const -- like variable but can't assign
 *   EDJ_CONTEXT_GLOBAL  Context is accessible everywhere
 *   EDJ_CONTEXT_THIS    Context can be "this" or "that"
 *   EDJ_CONTEXT_ARGS    Function arguments and local vars/consts
 *   EDJ_CONTEXT_NOCACHE Try autoload() before *data
 * Additionally, you might want to set autoload() and/or modified() callback
 * functions on the context, to handle those situations.
 */
edjcontext_t *edj_context(edjcontext_t *older, edj_t *data, edjcontextflags_t flags)
{
        edjcontext_t *context;

        /* Allocate it */
        context = malloc(sizeof(edjcontext_t));
        memset(context, 0, sizeof(edjcontext_t));

        /* Initialize it. */
        context->older = older;
        context->data = data;
        context->flags = flags;

        /* Return it */
        return context;
}

/* Locate a context layer that satisfies the "flags" argument.  If no such
 * layer exists, then insert it into the context stack in an appropriate
 * position.  Returns the new context, and may also adjust the top of the
 * context stack -- notice that we pass a reference to the stack pointer
 * instead of the pointer itself like we do for most context functions.
 */
edjcontext_t *edj_context_insert(edjcontext_t **refcontext, edjcontextflags_t flags)
{
	edjcontext_t *scan, *lag;

	/* For EDJ_CONTEXT_ARGS we always allocate a new layer on top */
	if (flags == EDJ_CONTEXT_ARGS) {
		*refcontext = edj_context(*refcontext, edj_object(), flags);
		return *refcontext;
	}

	/* Local? */
	if ((flags & EDJ_CONTEXT_GLOBAL) == 0) {
		/* Scan for an existing layer that works */
		for (lag = NULL, scan = *refcontext;
		     scan && scan->flags != EDJ_CONTEXT_ARGS;
		     lag = scan, scan = scan->older) {
			if (((flags ^ scan->flags) & (EDJ_CONTEXT_VAR|EDJ_CONTEXT_CONST)) == 0)
				/* We found an existing layer we can use */
				return scan;
		}

		/* No layer found, so insert one between "lag" and "scan" */
		scan = edj_context(scan, edj_object(), flags);
		if (lag)
			lag->older = scan;
		else
			*refcontext = scan;
		return scan;
	}

	/* Global!  Scan for an existing global layer that works */
	for (lag = NULL, scan = *refcontext;
	     scan;
	     lag = scan, scan = scan->older) {
		if (((flags ^ scan->flags) & (EDJ_CONTEXT_VAR|EDJ_CONTEXT_CONST|EDJ_CONTEXT_GLOBAL)) == 0)
			/* We found an existing layer we can use */
			return scan;
	}

	/* No layer found, so insert one between "lag" and "scan" */
	scan = edj_context(scan, edj_object(), flags);
	if (lag)
		lag->older = scan;
	else
		*refcontext = scan;
	return scan;
}

/* This is an autoload handler for supplying the current date/time.  If the
 * time hasn't changed since the last call, it simply returns NULL so the
 * cached value can be reused.
 */
static edj_t *stdcurrent(char *key)
{
	static time_t lastnow;
	static struct tm localtm, utctm;
	time_t now;
	char buf[30];

	/* For "now", return the current time as a number */
	if (!strcasecmp(key, "now")) {
		time(&now);
		return edj_from_int((int)now);
	}

	/* Are we looking for a formatted time value? */
	if (!strcasecmp(key, "current_date")
	 || !strcasecmp(key, "current_time")
	 || !strcasecmp(key, "current_datetime")
	 || !strcasecmp(key, "current_timestamp")
	 || !strcasecmp(key, "current_tz")) {
		/* If the time has changed since the last call, then refresh
		 * the "struct tm" buffers.  Two things to note here: First,
		 * The "lastnow" variable is initialized to 0, and the current
		 * time certainly is not 0, so the first call will definitely
		 * fetch new time values.  Second, if multiple stacks use this
		 * function simultaneously then they'll be doing it for the
		 * same "now" value so there is no race condition.  Threadsafe!
		 */
		time(&now);
		if (now != lastnow) {
			(void)gmtime_r(&now, &utctm);
			(void)localtime_r(&now, &localtm);
			lastnow = now;
		}

		/* Format it as appropriate */
		if (!strcasecmp(key, "current_date")) {
			snprintf(buf, sizeof buf, "%04d-%02d-%02d",
				localtm.tm_year + 1900,
				localtm.tm_mon + 1,
				localtm.tm_mday);
		} else if (!strcasecmp(key, "current_time")) {
			snprintf(buf, sizeof buf, "%02d:%02d:%02d",
				localtm.tm_hour,
				localtm.tm_min,
				localtm.tm_sec);
		} else if (!strcasecmp(key, "current_datetime")) {
			snprintf(buf, sizeof buf, "%04d-%02d-%02dT%02d:%02d:%02d",
				localtm.tm_year + 1900,
				localtm.tm_mon + 1,
				localtm.tm_mday,
				localtm.tm_hour,
				localtm.tm_min,
				localtm.tm_sec);
		} else if (!strcasecmp(key, "current_timestamp")) {
			snprintf(buf, sizeof buf, "%04d-%02d-%02dT%02d:%02d:%02dZ",
				utctm.tm_year + 1900,
				utctm.tm_mon + 1,
				utctm.tm_mday,
				utctm.tm_hour,
				utctm.tm_min,
				utctm.tm_sec);
		} else /* current_tz */ {
			int hours, minutes;

			/* Find the difference in hours and minutes between
			 * UTC and local time.  Start by assuming date is the
			 * same.
			 */
			hours = localtm.tm_hour - utctm.tm_hour;
			minutes = localtm.tm_min - utctm.tm_min;

			/* Time zone should be within +/- 12 hours.  If it's
			 * outside that range, it's probably due to shifting
			 * dates.  Tweak it.
			 */
			if (hours < -12)
				hours += 24;
			else if (hours > 12)
				hours -= 24;

			/* We want minutes to be the same sign as hours.
			 * If different, adjust hours and minutes.
			 */
			if (hours * minutes < 0) { /* opposite signs */
				if (minutes < 0) {
					minutes += 60;
					hours--;
				} else {
					minutes -= 60;
					hours++;
				}
			}

			/* Format it */
			if (hours < 0)
				snprintf(buf, sizeof buf, "-%02d:%02d", -hours, -minutes);
			else
				snprintf(buf, sizeof buf, "+%02d:%02d", hours, minutes);
		}

		/* Return it as a string */
		return edj_string(buf, -1);
	}

	/* No other special names */
	return NULL;
}

/* This creates the first several layers of a typical context stack.
 *
 * "args" should be an object containing members that should be accessible
 * in scripts via the "global.args" or simply "args" pseudo-variables.  The
 * "args" data will be freed when the last context layer is freed.  You may
 * also pass NULL as the "args" value to skip all that.
 */
edjcontext_t *edj_context_std(edj_t *args)
{
	edj_t	*base, *global, *vars, *consts, *thisdata;
	edjcontext_t *context = NULL;
	contexthook_t *hook;
	int	assignable;

	/* Start with a layer of system consts.  These are not automatically
	 * freed, because they could be used by multiple context stacks.
	 */
	context = edj_context(context, edj_system, EDJ_CONTEXT_CONST|EDJ_CONTEXT_NOFREE);

	/* Generate the data for the base context.  This is an object of the
	 * form {global:{vars:{},consts:{},args:{},files:[],data:null}
	 */
	base = edj_object();
	global = edj_object();
	vars = edj_object();
	consts = edj_object();
	edj_append(global, edj_key("vars", vars));
	edj_append(global, edj_key("consts", consts));
	if (args)
		edj_append(global, edj_key("args", args));
	edj_append(global, edj_key("files", edj_array()));
	edj_append(base, edj_key("global", global));

	/* Create the base layer of the context */
	context = edj_context(context, base, EDJ_CONTEXT_GLOBAL);

	/* Make it autoload the time variables.  These should not  be cached.
	 * Also add dummy versions of all time variables, mostly for the
	 * benefit of name completion.
	 */
	context->autoload = stdcurrent;
	context->flags |= EDJ_CONTEXT_NOCACHE;
	edj_append(base, edj_key("now", edj_null()));
	edj_append(base, edj_key("current_date", edj_null()));
	edj_append(base, edj_key("current_time", edj_null()));
	edj_append(base, edj_key("current_datetime", edj_null()));
	edj_append(base, edj_key("current_timestamp", edj_null()));
	edj_append(base, edj_key("current_tz", edj_null()));

	/* Allow plugins or applications to add their own layers */
	for (hook = extralayers; hook; hook = hook->other)
		context = (*hook->addcontext)(context);

	/* Create a layer to contain the "data" variable.  The layer should be
	 * assignable (EDJ_CONTEXT_VAR) if edj_system contains "update:true".
	 */
	assignable = 0; 
	if (edj_is_true(edj_by_key(edj_system, "update")))
		assignable = EDJ_CONTEXT_VAR;
	thisdata = edj_object();
	edj_append(thisdata, edj_key("data", edj_null()));
	context = edj_context(context, thisdata, EDJ_CONTEXT_GLOBAL | assignable | EDJ_CONTEXT_DATA);
	context->modified = data_modified;

	/* Create a layer above that for the contents of "global", mostly the
	 * "vars" and "consts" symbols.  Since these will be freed when the
	 * lower level is freed, we don't want to free them when this layer
	 * is freed.
	 */
	context = edj_context(context, global, EDJ_CONTEXT_GLOBAL | EDJ_CONTEXT_NOFREE);

	/* Create two or three more layers, exposing the contents of "vars"
	 * and "consts", and maybe "args".  Again, we don't free it when this
	 * context is freed because they're defined in a lower layer.
	 */
	if (args)
		context = edj_context(context, args, EDJ_CONTEXT_GLOBAL | EDJ_CONTEXT_CONST | EDJ_CONTEXT_NOFREE);
	context = edj_context(context, consts, EDJ_CONTEXT_GLOBAL | EDJ_CONTEXT_CONST | EDJ_CONTEXT_NOFREE);
	context = edj_context(context, vars, EDJ_CONTEXT_GLOBAL | EDJ_CONTEXT_VAR | EDJ_CONTEXT_NOFREE);

	/* DONE! Return the context stack. */
	return context;
}

/* This adds context layers for a user function call.  "fn" identifies the
 * function being called, mostly so we can see what arguments it uses.
 * "args" is an array containing the argument values.
 *
 * The context will contain COPIES of the argument values.  Those copies will
 * be automatically freed when the context layer is freed.
 */
edjcontext_t *edj_context_func(edjcontext_t *context, edjfunc_t *fn, edj_t *args)
{
	edj_t	*cargs, *name, *value;
	edj_t	*vars, *consts;

	/* Build an object containing the actual arguments */
	cargs = edj_copy(fn->userparams);
	for (name = fn->userparams->first, value = edj_first(args);
	     name && value;
	     name = name->next, value = edj_next(value)) { /* object */
		edj_append(cargs, edj_key(name->text, edj_copy(value)));
	}

	/* It's possible that we hit the end of names before values or
	 * vice-versa.  It's also possible that one of the lists is a
	 * deferred array.  We need to clean up.
	 */
	edj_break(name);
	edj_break(value);

	/* Also "vars" and "consts" to the arguments object */
	vars = edj_object();
	edj_append(cargs, edj_key("vars", vars));
	consts = edj_object();
	edj_append(cargs, edj_key("consts", consts));

	/* Add an "args" context layer containing those arguments */
	context = edj_context(context, cargs, EDJ_CONTEXT_ARGS);

	/* Add a "this" context containing the value of the first arg */
	if (args->first)
		context = edj_context(context, edj_copy(args->first), EDJ_CONTEXT_THIS);

	/* Add other layers exposing the contents of "vars" and "consts".
	 * Since that data will be freed when the "args" layer is freed,
	 * we don't want to free them when these layers are freed.
	 */
	context = edj_context(context, consts, EDJ_CONTEXT_CONST | EDJ_CONTEXT_NOFREE);
	context = edj_context(context, vars, EDJ_CONTEXT_VAR | EDJ_CONTEXT_NOFREE);

	/* Return the modified context */
	return context;
}


/******************************************************************************/

/* Select a new current file, and optionally append a filename to the files
 * array.  The context must have been created by edj_context_std() so this
 * function can know where to stuff the file info.  The *refcurrent parameter
 * is either an index into the files array, or EDJ_CONTEXT_FILE_NEXT to move
 * forward 1, EDJ_CONTEXT_FILE_SAME to stay on the same entry, or
 * EDJ_CONTEXT_FILE_PREVIOUS to move back one entry. Returns the files array,
 * and stuffs the current index into *refcurrent.  You do NOT need to free
 * the returned list since it'll be freed when the context is freed.
 *
 * If you switch files, and there was a previous file you're switching from,
 * and that file is writable, then it'll call edj_file_update() on it.
 */
edj_t *edj_context_file(edjcontext_t *context, const char *filename, int writable, int *refcurrent)
{
	edjcontext_t *globals, *datacontext, *thiscontext;
	edj_t	*files, *j, *f;
	int	current_file, noref, i, bits;
	struct stat st;
	struct tm tm;
	char	isobuf[24];

	/* Defend against empty context */
	if (!context)
		return NULL;

	/* If refcurrent is NULL, then assume no move */
	if (!refcurrent) {
		refcurrent = &noref;
		*refcurrent = EDJ_CONTEXT_FILE_SAME;
	}

	/* Locate the globals context at the bottom of the context stack,
	 * and also the "this" layer which contains the "data" variable.
	 */
	thiscontext = NULL;
	for (globals = context; globals->older && globals->older->older; globals = globals->older) {
		/* Also look for the global "this" and "data" layers, which
		 * store the context for the current file's contents.
		 */
		if (globals->older && (globals->older->flags & EDJ_CONTEXT_DATA)) {
			thiscontext = globals;
			datacontext = globals->older;
		}
	}

	/* If no global "this" layer was found, it must not be a std context */
	if (!thiscontext)
		return NULL;

	/* Locate the "files" array within "global" */
	files = edj_by_expr(globals->data, "global.files", NULL, NULL, NULL); /* undeferred */
	if (!files)
		return NULL;

	/* If given a filename, check for it in the list.  If it's there, then
	 * set *refcurrent to its index; else add it to the list.  Each file
	 * gets an object containing the filename and other useful info.
	 */
	if (filename) {
		/* Scan the files table for this filename */
		for (i = 0, j = files->first; j; j = j->next, i++) /* undeferred */
			if ((f = edj_by_key(j, "filename")) != NULL
			 && f->type == EDJ_STRING
			 && !strcmp(f->text, filename))
				break;

		/* Did we find it? */
		if (j) {
			/* Select it as the current file */
			if (*refcurrent != EDJ_CONTEXT_FILE_SAME)
				*refcurrent = i;
		} else {
			/* Fetch info about the file */
			if (!strcmp(filename, "-"))
				i = fstat(0, &st);
			else
				i = stat(filename, &st);
			if (i != 0)
				bits = 0222; /* New so assume writable */
			else if (!S_ISREG(st.st_mode) && !S_ISFIFO(st.st_mode))
				bits = 0; /* can't read or write a non-file */
			else if (st.st_uid == geteuid())
				bits = st.st_mode & 0700;
			else if (st.st_gid == getegid())
				bits = st.st_mode & 0070;
			else
				bits = st.st_mode & 0007;
			if (i == 0) {
				localtime_r(&st.st_mtime, &tm);
				snprintf(isobuf, sizeof isobuf, "%04d-%02d-%02dT%02d:%02d:%02d",
					tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
					tm.tm_hour, tm.tm_min, tm.tm_sec);
			} else
				*isobuf = '\0';

			/* Create the entry for the "files" array */
			edj_t *entry = edj_object();
			edj_append(entry, edj_key("filename", edj_string(filename, -1)));
			edj_append(entry, edj_key("size", edj_from_int(bits == 0 ? 0 : st.st_size)));
			edj_append(entry, edj_key("mtime", edj_string(isobuf, -1)));
			edj_append(entry, edj_key("readable", edj_boolean(bits & 0444)));
			edj_append(entry, edj_key("writable", edj_boolean(writable && (bits & 0222))));

			/* Add it to the "files" list */
			edj_append(files, entry);

			/* Make it be the current file */
			if (*refcurrent != EDJ_CONTEXT_FILE_SAME)
				*refcurrent = edj_length(files) - 1;
		}
	}

	/* Get the current_file index, if any, and use that to adjust the
	 * "current" parameter.
	 */
	j = edj_by_key(globals->data, "current_file");
	if (j) {
		current_file = edj_int(j);
		if (*refcurrent < 0)
			*refcurrent = current_file + 2 + *refcurrent;
	} else {
		current_file = 0;
	}
	switch (*refcurrent) {
	case EDJ_CONTEXT_FILE_NEXT:	*refcurrent = current_file + 1; break;
	case EDJ_CONTEXT_FILE_SAME:	*refcurrent = current_file;	break;
	case EDJ_CONTEXT_FILE_PREVIOUS:*refcurrent = current_file - 1; break;
	}

	/* If there was a previous file, and it is writable, and modified,
	 * and we're switching away from it, then write it now.
	 */
	if ((datacontext->flags & EDJ_CONTEXT_MODIFIED) != 0
	 && current_file >= 0
	 && current_file != *refcurrent
	 && (j = edj_by_index(files, current_file)) != NULL /* undeferred */
	 && edj_is_true(edj_by_key(j, "writable"))) {
		char *filename = edj_text_by_key(j, "filename");
		if (filename) {
			/* Tweak the formatting rules */
			edjformat_t tweaked = edj_format_default;
			strcat(tweaked.table, "json"); /* Write tables in JSON */
			tweaked.string = 0;  /* No raw strings */
			tweaked.elem = 0;    /* Not one array member per line */
			tweaked.pretty = 0;  /* Not pretty-printed */
			tweaked.sh = 0;	     /* Not shell quoted */

			/* Open the data file for writing */
			tweaked.fp = edj_file_update(filename);
			if (tweaked.fp) {
				/* Write the data */
				edj_print(edj_by_key(datacontext->data, "data"), &tweaked);
				fclose(tweaked.fp);

				/* Turn off the MODIFIED flag */
				datacontext->flags &= ~EDJ_CONTEXT_MODIFIED;
			}
		}
	}

	/* If "current" is out of range for the "files" array, clip it */
	if (*refcurrent < 0)
		*refcurrent = 0;
	else if (*refcurrent >= edj_length(files))
		*refcurrent = edj_length(files) - 1;

	/* Load the new current file, if the numbers are different or no
	 * file was loaded before.  Note that we don't need to explicitly
	 * free the old data, because it gets freed when we reassign the
	 * "data" member (the first edj_append() below).
	 */
	if (current_file != *refcurrent || !j) {
		/* Load the data.  If it couldn't be loaded then say why */
		char *currentname = edj_text_by_key(edj_by_index(files, *refcurrent), "filename"); /* undeferred */
		edj_t *data;
		if (!currentname)
			data = edj_error_null(0, "There is no current file");
		else {
			data = edj_parse_file(currentname);
			if (!data)
				data = edj_error_null(0, "File \"%s\" is unreadable", currentname);
		}

		/* Store it as the variable "data".  This also has the
		 * side-effect of freeing the old data.
		 */
		edj_append(datacontext->data, edj_key("data", data));

		/* Update "current_file" number */
		edj_append(globals->data, edj_key("current_file", edj_from_int(*refcurrent)));
	}

	/* Return the list of files */
	return files;
}



/******************************************************************************/


/* Scan items in a context list for given name, and return its value.  If not
 * found, return NULL.  As special cases, the name "this" returns the most
 * recently added item with EDJ_CONTEXT_THIS set, and "that" returns the
 * second-most-recent.
 *
 * Some context layers can have autoloaders.  If the name isn't found in the
 * layer already, then the autoloader is given a shot... except that if
 * reflayer is not null (used for assignments) then we skip that because
 * autoloaded items can't be modified.
 *
 * context is the top of the context stack to search.
 * key is the member name to search for.
 * reflayer, if not NULL, will be be set to the context layer containing key.
 * Also, a non-NULL reflayer implies that we're trying to assign, so only
 * contexts marked with EDJ_CONTEXT_VAR or EDJ_CONTEXT_CONST will be checked.
 */
edj_t *edj_context_by_key(edjcontext_t *context, char *key, edjcontext_t **reflayer)
{
        edj_t  *val;
	int	firstthis;
	int	otherlocal;

        firstthis = 1;
        otherlocal = 0;
        while (context) {
		/* Skip if local to some other function */
		if ((context->flags & EDJ_CONTEXT_GLOBAL) != 0)
			otherlocal = 0;
		if (otherlocal) {
			context = context->older;
			continue;
		}

		/* If we're looking to assign, skip context that isn't a
		 * "var" or "const" layer.
		 */
		if (reflayer && (context->flags & (EDJ_CONTEXT_VAR|EDJ_CONTEXT_CONST)) == 0) {
			context = context->older;
			continue;
		}

                /* "this" returns the most recently added item in its entirety*/
                if (context->flags & EDJ_CONTEXT_THIS) {
			if (!strcasecmp(key, "this")
			 || (!strcasecmp(key, "that") && !firstthis)) {
				if (reflayer)
					*reflayer = context;
				return context->data;
			}
			firstthis = 0;
		}

                /* If there's an "autoload" handler and this layer doesn't
                 * cache results of autoloading, then give it a shot
                 */
                if (context->autoload && !reflayer && (context->flags & EDJ_CONTEXT_NOCACHE) != 0) {
                        val = (*context->autoload)(key);
                        if (val) {
				/* Add it to the autoload object.  Since this
				 * layer doesn't cache results, the only reason
				 * for doing this is so the value can be freed
				 * later, when the context is freed.  That's
				 * a very good reason though!
				 */
                                edj_append(context->data, edj_key(key, val));
                                return val;
                        }
                }

                /* If context data is an object, check for a member */
                if (context->data && context->data->type == EDJ_OBJECT) {
                        val = edj_by_key(context->data, key);
                        if (val) {
				if (reflayer)
					*reflayer = context;
                                return val;
			}
                }

                /* If there's an "autoload" handler, give it a shot */
                if (context->autoload && !reflayer && (context->flags & EDJ_CONTEXT_NOCACHE) == 0) {
                        val = (*context->autoload)(key);
                        if (val) {
				/* Add it to the autoload object */
                                edj_append(context->data, edj_key(key, val));
                                return val;
                        }
                }

                /* Not found here.  Try older contexts */
                if (context->flags & EDJ_CONTEXT_ARGS)
			otherlocal = 1;
                context = context->older;
        }

        /* Nope, not in any context */
        return NULL;
}

/* For a given L-value, find its layer, container, and value.  This is used
 * for assigning or appending to variables.  Return NULL on success, or "" on
 * success when *refkey needs to be freed, or an error format on failure.
 *
 * That "" business is a hack.  When assigning to a member of an object,
 * *refkey should identify the member to be assigned.  But when assigning by
 * a subscript ("object[string]" instead of "object.key") then the key is a
 * computed value, and we evaluate/free the subscript before returning, so we
 * need to make a dynamically-allocated copy of the key.  And then the calling
 * function needs to free it.  I decided to make this function be static just
 * because that hack is so obnoxious, at least this way it's only used by a
 * couple of functions within the same source file.
 */
static const char *edjlvalue(edjcalc_t *lvalue, edjcontext_t *context, edjcontext_t **reflayer, edj_t **refcontainer, edj_t **refvalue, char **refkey)
{
	edj_t	*value, *t, *v, *m;
	char	*skey;
	edjcalc_t *sub;
	const char *err, *ret;
	edjcontext_t *layer;

	switch (lvalue->op) {
	case EDJOP_NAME:
	case EDJOP_LITERAL:
		/* Literal can only be a string, serving as a quoted name */
		if (lvalue->op == EDJOP_LITERAL && lvalue->u.literal->type != EDJ_STRING)
			return "BadLValue:Invalid assignment";

		/* Get the name */
		if (lvalue->op == EDJOP_NAME)
			*refkey = lvalue->u.text;
		else
			*refkey = lvalue->u.literal->text;

		/* Look for it in the context */
		value = edj_context_by_key(context, *refkey, &layer);

		/* If not found, be sad */
		if (!value) {
			/* NOT FOUND!  Try again without &layer as a way to
			 * distinguish between const and unknown var
			 */
			if (edj_context_by_key(context, *refkey, NULL))
				return "Const:Attempt to change const \"%s\"";
			return "UnknownVar:Unknown variable \"%s\"";
		}

		/* Found!  Since we're doing assignments, if this is a
		 * deferred array then we need to undefer it.
		 */
		edj_undefer(layer->data);
		edj_undefer(value);

		/* Return what we found */
		if (reflayer)
			*reflayer = layer;
		if (refcontainer)
			*refcontainer = layer->data;
		if (refvalue)
			*refvalue = value;
		return NULL;

	case EDJOP_DOT:
		/* Recursively look up the left side of the dot */
		if ((err = edjlvalue(lvalue->u.param.left, context, reflayer, refcontainer, &value, refkey)) != NULL && *err)
			return err;
		if (err) { /* "" */
			free(*refkey);
			return "UnknownMember:Object has no member \"%s\"";
		}
		if (value == NULL)
			return "UnknownVar:Unknown variable \"%s\"";

		/* If lhs of dot isn't an object, that's a problem */
		if (value->type != EDJ_OBJECT)
			return "notObject:Attempt to access member in a non-object";

		/* For DOT, the right parameter should just be a name */
		if (lvalue->u.param.right->op == EDJOP_NAME)
			*refkey = lvalue->u.param.right->u.text;
		else if (lvalue->u.param.right->op == EDJOP_LITERAL
		      && lvalue->u.param.right->u.literal->type == EDJ_STRING)
			*refkey = lvalue->u.param.right->u.literal->text;
		else
			/* invalid rhs of a dot */
			return "NotKey:Attempt to use a non-key as a member key";

		/* Look for the name in this object.  If not found, "t" will
		 * be null, which is a special type of failure.  This failure
		 * only happens if we expected to find a value (e.g., to append)
		 */
		t = edj_by_key(value, *refkey);
		if (!t && !refvalue)
			return "UnknownMember:Object has no member \"%s\"";

		/* Since we're doing this to assign a value, we can't use
		 * deferred arrays.
		 */
		edj_undefer(value);

		/* The value becomes the container, and "t" becomes the value */
		if (refcontainer)
			*refcontainer = value;
		if (refvalue)
			*refvalue = t;
		return NULL;

	case EDJOP_SUBEXPR:
		/* Recursively look up the left side of the subscript (the
		 * expression to search in)
		 */
		if ((err = edjlvalue(lvalue->u.param.left, context, reflayer, refcontainer, &value, refkey)) != NULL && *err)
			return err;
		if (err) { /* "" */
			free(*refkey);
			return "UnknownMember:Object has no member \"%s\"";
		}
		if (value == NULL)
			return "UnknownVar:Unknown variable \"%s\"";

		/* Evaluate the index expression.  It should return an
		 * expression string -- the expression to search for.
		 */
		t = edj_calc(lvalue->u.param.right, context, NULL);
		if (t->type != EDJ_STRING)
			return "badExpr:The [[expression]] didn't return a string";

		/* Locate the desired node */
		v = edj_by_expr(value, t->text, NULL, &value, refkey);
		edj_free(t);

		/* Return what we found */
		if (refcontainer)
			*refcontainer = value;
		if (refvalue)
			*refvalue = v;
		/* *refkey has already been set */
		return NULL;

	case EDJOP_SUBSCRIPT:
		/* Recursively look up the left side of the subscript */
		if ((err = edjlvalue(lvalue->u.param.left, context, reflayer, refcontainer, &value, refkey)) != NULL && *err)
			return err;
		if (err) { /* "" */
			free(*refkey);
			return "unknownMember:Object has no member \"%s\"";
		}
		if (value == NULL)
			return "unknownVar:Unknown variable \"%s\"";

		/* The [key:value] style of subscripts is handled specially */
		sub = lvalue->u.param.right;
		if (sub->op == EDJOP_COLON) {
			/* The array[key:value] case */

			/* Get the key */
			if (sub->u.param.left->op == EDJOP_NAME)
				skey = sub->u.param.left->u.text;
			else if (sub->u.param.left->op == EDJOP_LITERAL
			 && sub->u.param.left->u.literal->type == EDJ_STRING)
				skey = sub->u.param.left->u.literal->text;
			else /* invalid key */
				return "BadSubKey:Invalid key for [key:value] subscript";

			/* Evaluate the value */
			t = edj_calc(sub->u.param.right, context, NULL);
			/*!!! should I try to detect "null" with error text? */

			/* Scan the array for an element with that member key
			 * and value.
			 */
			for (v = edj_first(value); v; v = edj_next(v)) {
				if (v->type == EDJ_OBJECT) {
					m = edj_by_key(v, skey);
					if (m && edj_equal(m, t))
						break;
				}
			}

			/* If not found, fail */
			if (!v)
				return "UnknownSub:No element found with the requested subscript";

			/* Return what we found */
			if (refcontainer)
				*refcontainer = value;
			if (refvalue)
				*refvalue = v;
			return NULL;

		} else {
			/* Use edj_calc() to evaluate the subscript */
			t = edj_calc(lvalue->u.param.right, context, NULL);

			/* Look it up.  array[number] or object[string] */
			if (value->type == EDJ_ARRAY && t->type == EDJ_NUMBER)
				v = edj_by_index(value, edj_int(t));
			else if (value->type == EDJ_OBJECT && t->type == EDJ_STRING) {
				*refkey = t->text;
				v = edj_by_key(value, *refkey);
			} else if (value->type == EDJ_OBJECT && t->type == EDJ_NUMBER) {
				/* Number could be text or binary.  If binary
				 * then we need to convert it to text.
				 */
				if (t->text[0])
					*refkey = t->text;
				else
					*refkey = edj_serialize(t, NULL);
				v = edj_by_key(value, *refkey);
			} else {
				/* Bad subscript */
				edj_free(t);
				return "SubType:Subscript has invalid type";
			}

			/* Okay, we're done with the subscript "t"... EXCEPT
			 * that if it's a string that will serve as an array
			 * subscript then we need to keep it the string for
			 * a while.
			 */
			ret = NULL;
			if (t->type == EDJ_STRING) {
				*refkey = strdup(t->text);
				ret = "";
			} else if (t->type == EDJ_NUMBER && !t->text[0] && value->type == EDJ_OBJECT) {
				/* *refkey is already dynamically allocated */
				ret = "";
			}
			edj_free(t);

			/* Not finding a value is bad... unless we don't need
			 * one.  Its okay to assign new members to an existing
			 * object.
			 */
			if (!v && (*refvalue) && value->type != EDJ_OBJECT)
				return "UnknownSub:No element found with the requested subscript";

			/* Return what we found */
			if (refcontainer)
				*refcontainer = value;
			if (refvalue)
				*refvalue = v;
			return ret;
		}

	default:
		/* Invalid operation in an l-value */
		return "BadLValue:Invalid assignment";
	}
}


/* Assign a variable.  Returns NULL on success, or an edj_t "null" with an
 * error message if it fails.
 *
 * "rvalue" will be freed when the context is freed.  This function DOES NOT
 * make a copy of it; it uses "rvalue" directly.  If that's an issue then the
 * calling function should make a copy.
 */
edj_t *edj_context_assign(edjcalc_t *lvalue, edj_t *rvalue, edjcontext_t *context)
{
	edjcontext_t	*layer;
	edj_t	*container, *value, *scan;
	char	*key;
	const char	*err;

	/* We can't use a value that's already part of something else. */
	assert(rvalue->next == NULL); /* undeferred */

	/* Get the details on what to change.  Note that for new members,
	 * value may be NULL even if edjlvalue() returns 1.
	 */
	if ((err = edjlvalue(lvalue, context, &layer, &container, &value, &key)) != NULL && *err)
		return edj_error_null(0, err, key);

	/* If it's const then fail */
	if (layer->flags & EDJ_CONTEXT_CONST) {
		value = edj_error_null(0, "Const:Attempt to change const \"%s\"", key);
		if (err) /* "" */
			free(key);
		return value;
	}

	/* Objects are easy, arrays are hard */
	if (container->type == EDJ_OBJECT) {
		edj_append(container, edj_key(key, rvalue));
		if (err) /* "" */
			free(key);
	} else if (container->type == EDJ_ARRAY) {
		if (!value)
			return edj_error_null(0, "UnknownSub:No element found with the requested subscript", key);

		/* If it's a deferred array, convert to undeferred */
		edj_undefer(container);

		/* Use the tail of the array with the new value */
		rvalue->next = value->next; /* undeferred */

		/* Replace either the head of the array or a later element */
		if (value == container->first) {
			/* Replace the first element with a new one */
			container->first = rvalue;
		} else {
			/* Scan for the element before the changed one */
			for (scan = container->first; scan->next != value; scan = scan->next) { /* undeferred */
			}

			/* Replace the element after it with a new one */
			scan->next = rvalue; /* undeferred */
		}

		/* Free the old value (but not its siblings) */
		value->next = NULL; /* undeferred */
		edj_free(value);
	} else {
		/* Not something that can be assigned */
		return NULL;
	}

	/* If this layer has a callback for modifications, call it */
	if (layer->modified)
		(*layer->modified)(layer, lvalue);

	/* All good! */
	return NULL;
}


/* Append to an array variable.  Returns NULL on success, or an edj_t "null"
 * with an error message if it fails.
 *
 * "rvalue" will be freed when the context is freed.  This function DOES NOT
 * make a copy of it; it uses "rvalue" directly.  If that's an issue then the
 * calling function should make a copy.
 */
edj_t *edj_context_append(edjcalc_t *lvalue, edj_t *rvalue, edjcontext_t *context)
{
	edjcontext_t	*layer;
	edj_t	*container, *value;
	char	*key;
	const char *err;

	/* We can't use a value that's already part of something else. */
	assert(rvalue->next == NULL); /* undeferred */

	/* Get the details on what to change */
	if ((err = edjlvalue(lvalue, context, &layer, &container, &value, &key)) != NULL)
		return edj_error_null(0, err, key);
	if (value == NULL)
		return edj_error_null(0, "unknownVar:Unknown variable \"%s\"", key);

	/* If it's const then fail */
	if (layer->flags & EDJ_CONTEXT_CONST)
		return edj_error_null(0, "const:Attempt to change const \"%s\"", key);

	/* We can only append to arrays */
	if (value->type != EDJ_ARRAY)
		return edj_error_null(0, "append:Can't append to %s \"%s\"", edj_typeof(value, 0), key);

	/* If its a deferred array, convert to undeferred */
	edj_undefer(value);

	/* Append! */
	edj_append(value, rvalue);

	/* If this layer has a callback for modifications, call it */
	if (layer->modified)
		(*layer->modified)(layer, lvalue);

	return NULL;
}

/* Delete a variable or part of a variable (an array element or object member).
 * Returns NULL on success or an edj_t "null" with an error message if it fails.
 * NOTE: The semantics of "delete" in JavaScript say deleting something that
 * doesn't exist is NOT an error, so errors returned by this function are
 * often ignored.
 */
edj_t *edj_context_delete(edjcalc_t *lvalue, edjcontext_t *context)
{
	edjcontext_t *layer;	/* Context layer containing the lvalue */
	edj_t	    *container;	/* The parent of the item to delete */
	edj_t	    *value;	/* The item to delete */
	edj_t	    *scan,*lag;	/* Used for scanning elements/members */
	char	    *key;	/* The name of the item to delete */
	const char  *err;

	/* Get the details on what to delete */
	if ((err = edjlvalue(lvalue, context, &layer, &container, &value, &key)) != NULL)
		return edj_error_null(0, err, key);
	if (value == NULL) {
		//return edj_error_null(0, "UnknownVar:Unknown variable \"%s\"", key);
		return NULL; // JavaScript silently ignores this type of error
	}
	if ((layer->flags & EDJ_CONTEXT_CONST) != 0)
		return edj_error_null(0, "const:Attempt to change const \"%s\"", key);

	/* Different technique for deleting from an object vs an array */
	if (container->type == EDJ_OBJECT) {
		/* Scan the array for the value */
		for (lag = NULL, scan = container->first;
		     scan && scan->first != value;
		     lag = scan, scan = scan->next) {
			assert(scan->type == EDJ_KEY);
		}

	} else /* container->type == EDJ_ARRAY */ {
		/* Scan the array for the value */
		for (lag = NULL, scan = container->first;
		     scan && scan != value;
		     lag = scan, scan = scan->next) {
		}

	}

	/* We should ALWAYS find it. */
	assert(scan);

	/* Remove it from the array/object and free it.  For object members,
	 * this will free both the key and the value.
	 */
	if (lag)
		lag->next = scan->next;
	else
		container->first = scan->next;
	edj_free(scan);

	/* For arrays, this should reduce the "length" that's stuffed into
	 * the container->text field.
	 */
	if (container->type == EDJ_ARRAY && EDJ_ARRAY_LENGTH(container) > 0)
		EDJ_ARRAY_LENGTH(container) = EDJ_ARRAY_LENGTH(container) - 1;

	return NULL;
}

/* Add a variable.  If necessary (as indicated by "flags") then add a context
 * for it too.  Return maybe-changed top of the context stack.  "flags" can be
 * one of the following:
 *   EDJ_CONTEXT_GLOBAL_VAR	A global variable
 *   EDJ_CONTEXT_GLOBAL_CONST	A global constant
 *   EDJ_CONTEXT_LOCAL_VAR	A local variable
 *   EDJ_CONTEXT_LOCAL_CONST	A local constant
 *
 * Return 1 on success, or 0 if the key was already declared.
 */
int edj_context_declare(edjcontext_t **refcontext, char *key, edj_t *value, edjcontextflags_t flags)
{
	edjcontext_t	*layer, *scan;

	/* Find the context layer.  Also scan for duplicate name. */
	for (layer = NULL, scan = *refcontext; scan; scan = scan->older) {

		/* Skip layers that don't have an object as their data */
		if (!scan->data || scan->data->type != EDJ_OBJECT)
			continue;

		/* If this layer holds args/consts/vars, and one of them is
		 * the name we're trying to declare, then return 0.  This
		 * intentionally does NOT protect against the case where
		 * "this" contains a member with the name that you want to
		 * declare.
		 */
		if ((scan->flags & (EDJ_CONTEXT_VAR | EDJ_CONTEXT_CONST | EDJ_CONTEXT_ARGS)) != 0
		 && edj_by_key(scan->data, key))
			return 0;

		/* Stop after the args layer */
		if (scan->flags & EDJ_CONTEXT_ARGS)
			break;

		/* If this is the layer we want to add a var/const to, then
		 * remember it.
		 */
		if (!layer && (scan->flags & flags) == flags)
			layer = scan;
	}

	/* If we didn't find a layer where we can vars/consts, then fail. */
	if (!layer)
		return 0;

	/* Add it */
	edj_append(layer->data, edj_key(key, value ? value : edj_null()));
	return 1;
}


/* Returns the default table for a "SELECT" statement.
 *
 * In SQL "SELECT" statements, the "FROM" clause is optional.  If it is omitted
 * then we need to choose a default table.  If "this" is a table, that's what
 * we want.  Otherwise we scan for the OLDEST table in the context, which is
 * probably a file from the command line.  If we don't find one, then we scan
 * for the oldest object containing a "filename" member, and then scan for am
 * array in the members of that.  If no table is found anywhere, return NULL.
 *
 * The returned edj_t is part of the context, so you can assume it'll be
 * freed as part of the context.  You don't need to free it, but you can't
 * modify it either.
 *
 * refexpr is usually NULL.  If it is non-NULL, then it should point to a
 * (char*) variable which will be set to NULL on failure, or a dynamically-
 * allocated string containing an expression for finding the default table
 * on success.  The calling function is responsible for freeing the string
 * when it is no longer needed.
 */
edj_t *edj_context_default_table(edjcontext_t *context, char **refexpr)
{
	edj_t	*found;

	/* Defend against NULL */
	if (!context) {
		if (refexpr)
			*refexpr = NULL;
		return NULL;
	}

	/* If "this" is a table, use it */
	found = edj_context_by_key(context, "this", NULL);
	if (found && edj_is_table(found)) {
		if (refexpr)
			*refexpr = strdup("this");
		return found;
	}

	/* Locate the current file.  If none, then there is no default table */
	found = edj_context_by_key(context, "data", NULL);
	if (found) {
		/* If it's a table, use it */
		if (edj_is_table(found)) {
			if (refexpr)
				*refexpr = strdup("data");
			return found;
		}

		/* If it's an object, scan it for a table */
		if (found->type == EDJ_OBJECT) {
			for (found = found->first; found; found = found->next) { /* object */
				if (edj_is_table(found->first)) {
					if (refexpr) {
						/* Determine whether the table
						 * name needs quoting by looking
						 * for non-alphanumeric or
						 * non-ASCII characters.
						 */
						char *scan = found->text;
						while (*scan == '_'
						   || (*scan >= 'A' && *scan <= 'Z')
						   || (*scan >= 'a' && *scan <= 'z')
						   || (*scan >= '0' && *scan <= '9'))
							scan++;
						if (*found->text >= '0' && *found->text <= '9')
							scan = "";

						/* Allocate storage for the combined string */
						*refexpr = malloc(8 + strlen(found->text));

						/* Generate the string */
						strcpy(*refexpr, "data.");
						if (*scan)
							strcat(*refexpr, "`");
						strcat(*refexpr, found->text);
						if (*scan)
							strcat(*refexpr, "`");
					}
					return found->first;
				}
			}
		}
	}

	/* No joy */
	if (refexpr)
		*refexpr = NULL;
	return NULL;
}

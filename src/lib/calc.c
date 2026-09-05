#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <locale.h>
#include <assert.h>
#include <regex.h>
#include <edj.h>

/* Use the real version of edj_calc here, not the debugging macro */
#ifdef edj_calc
# undef edj_calc
#endif

/* BIG NOTE ABOUT MEMORY MANAGEMENT
 *
 * My goals for memory management in edj_calc() are that it should be simple
 * and efficient.  "Efficient" here means that it usually won't allocate new
 * edj_t's if it doesn't have to.
 *
 * To keep it simple though, edj_calc() always returns a freshly allocated
 * edj_t tree.  If you call edj_calc(), then you must eventually call
 * edj_free() on the returned value.
 *
 * edj_calc() is recursive, so it will sometimes allocate and free temporary
 * edj_t's.  Some instances where it DOESN'T need to free an edj_t that it
 * uses are:
 *
 *   Literals.  These are in the edjcalc_t expression as EDJOP_LITERAL
 *		nodes.  They contain an edj_t tree that's allocated by
 *		edj_calc_parse() and freed by edj_calc_free().
 *
 *   Names.	The values associated with names come from the context,
 *		usually retrieved via edj_context_by_key().  They are
 *		allocated by the code that sets up the context, and freed
 *		when the context is freed.  Expressions sometimes create
 *		local contexts and free them, but that's a separate thing.
 *
 * That's all! So when edj_calc() needs to access a left or right operand,
 * if it can fetch a literal or name then it doesn't need to free it;
 * otherwise (when it must recursively call edj_calc()) it must free
 * the temporary values.
 */

/* These make accessing the left and right operands easier/clearer. */
#define LEFT  u.param.left
#define RIGHT u.param.right

/* These two macros fetch the left and right operands.  They always set the
 * "left" and "right" variables.  If the the value is freshly allocated
 * (meaning edj_calc() is responsible for freeing it) then they also set
 * the "freeleft" and "freeright" variables.
 */
#define USE_LEFT_OPERAND(calc)	if ((left = jcsimple(calc->LEFT, context)) == NULL)\
		left = freeleft = edj_calc(calc->LEFT, context, agdata)
#define USE_RIGHT_OPERAND(calc)	if ((right = jcsimple(calc->RIGHT, context)) == NULL)\
		right = freeright = edj_calc(calc->RIGHT, context, agdata)

/* If calc is a literal or name, then we can retrieve the value without
 * allocating anything.  Do that, and return it.  Otherwise return NULL.
 */
static edj_t *jcsimple(edjcalc_t *calc, edjcontext_t *context)
{
	edj_t *tmp;

	/* If literal then return its value */
	if (calc->op == EDJOP_LITERAL)
		return calc->u.literal;

	/* If simple name then look it up */
	if (calc->op == EDJOP_NAME)
		return edj_context_by_key(context, calc->u.text, NULL);

	/* We can do name.name too */
	if ((calc->op == EDJOP_DOT || calc->op == EDJOP_DOUBLEDOT)
	 && calc->RIGHT->op == EDJOP_NAME
	 && (tmp = jcsimple(calc->LEFT, context)) != NULL
	 && tmp->type == EDJ_OBJECT)
		return edj_by_key(tmp, calc->RIGHT->u.text);

	/* We can choose a default table when SELECT is used without FROM */
	if (calc->op == EDJOP_FROM)
		return edj_context_default_table(context, NULL);

	/* No joy */
	return NULL;
}


/* Implement @= natural join, @< left join, and @> right join */
edj_t *jcnjoin(edj_t *jl, edj_t *jr, int left, int right)
{
	edj_t  *scan, *result, *merge, *lmem, *rmem;
	int	leftmatch;
	char	*rightmatch, *r;

	/* We normally loop over the left argument in the outer loop, and the
	 * right argument in the inner loop.  If right is deferred and left
	 * isn't, then it's more efficient to use the right in the outer loop
	 * so switch the.
	 */
	if (!edj_is_deferred_array(jl) && edj_is_deferred_array(jr)) {
		/* Swap pointers */
		scan = jl;
		jl = jr;
		jr = scan;

		/* Swap left/right flags */
		leftmatch = left;
		left = right;
		right = leftmatch;
	}

	/* If we're doing right join, then we need a list of flags to
	 * keep track of which right elements never matched any left
	 */
	rightmatch = right ? calloc(edj_length(jr), sizeof(char)) : NULL;

	/* Start with an empty result array */
	result = edj_array();

	/* For each row from the left table... */
	for (jl = edj_first(jl); jl; jl = edj_next(jl)) {
		/* If interrupted, then discard any result so far and return
		 * an error null.
		 */
		if (edj_interrupt) {
			if (rightmatch)
				free(rightmatch);
			edj_free(result);
			edj_break(jl);
			return edj_error_null(NULL, "intr:Interrupted");
		}

		/* Skip if not an object */
		if (jl->type != EDJ_OBJECT)
			continue;

		/* For each row from the right table... */
		leftmatch = 0;
		for (scan = edj_first(jr), r = rightmatch; scan; scan = edj_next(scan), r++) {
			/* Skip if not an object */
			if (scan->type != EDJ_OBJECT)
				continue;

			/* If any members clash, skip this pairing */
			for (lmem = jl->first; lmem; lmem = lmem->next) { /* object */
				rmem = edj_by_key(scan, lmem->text);
				if (rmem && !edj_equal(lmem->first, rmem))
					break;
			}
			if (lmem)
				continue;

			/* Merge the objects */
			merge = edj_copy(jl);
			for (rmem = scan->first; rmem; rmem = rmem->next) { /* object */
				lmem = edj_by_key(merge, rmem->text);
				if (!lmem)
					edj_append(merge, edj_copy(rmem));
			}

			/* Add the merged object to the result */
			edj_append(result, merge);

			/* Remember that there was a match */
			if (right)
				*r = 1;
			leftmatch = 1;
		}

		/* If doing a left join and left didn't match anything, add it
		 * by itself.
		 */
		if (left && !leftmatch)
			edj_append(result, edj_copy(jl));
	}

	/* If doing a right join, add any right elements that didn't match
	 * anything from the left.
	 */
	if (right) {
		for (scan = edj_first(jr), r = rightmatch; scan; scan = edj_next(scan), r++) {
			if (!*r)
				edj_append(result, edj_copy(scan));
		}
		free(rightmatch);
	}

	/* Return the result */
	return result;
}

/* Combine keys and values */
static edj_t *jcvalues(edj_t *keys, edj_t *values)
{
	edj_t	*key, *value, *vrow, *rowobj, *result;

	/* First argument must be an array of strings to use as keys */
	if (keys->type != EDJ_ARRAY)
		key = keys; /* not NULL marking the end of happy scan */
	else {
		for (key = edj_first(keys); key; key = edj_next(key)) {
			if (key->type != EDJ_STRING) {
				edj_break(key);
				break;
			}
		}
	}
	if (key || !keys->first)
		return edj_error_null(NULL, "valuesL:Left of VALUES must be an array of strings");

	/* Right argument may be either an array of values, or any array of
	 * arrays of values.  If all elements are arrays then assume the latter.
	 */
	if (values->type != EDJ_ARRAY)
		return edj_error_null(NULL, "valuesR:Right of VALUES must be array of values, or array of arrays of values");
	for (value = edj_first(values); value; value = edj_next(value)) {
		if (value->type != EDJ_ARRAY) {
			/* Stop scanning the outer for-loop */
			edj_break(value);

			/* Do the single object version */
			result = edj_object();
			for (key = edj_first(keys), value = edj_first(values); key && value; key = edj_next(key), value = edj_next(value))
				edj_append(result, edj_key(key->text, edj_copy(value)));

			/* If the number keys doesn't match number of values,
			 * then one of the first/next loops ended prematurely.
			 * Clean up!
			 */
			edj_break(key);
			edj_break(value);
			return result;
		}
	}

	/* We'll be doing the table version. */
	result = edj_array();
	for (vrow = edj_first(values); vrow; vrow = edj_next(vrow)) {
		rowobj = edj_object();
		for (key = edj_first(keys), value = edj_first(vrow);
		     key && value;
		     key = edj_next(key), value = edj_next(value))
			edj_append(rowobj, edj_key(key->text, edj_copy(value)));
		edj_break(key);
		edj_break(value);
		edj_append(result, rowobj);
	}
	return result;
}


/* Invoke all aggregates for the current item ("this" in context) */
static void jcag(edjag_t *ag, edjcontext_t *context, void *agdata)
{
	int     i;
	void    *fnag = agdata;
	edj_t  *args;

	/* For each aggregate function... */
	for (i = 0; i < ag->nags; i++) {
		/* Evaluate its parameters */
		args = edj_calc(ag->ag[i]->u.func.args, context, agdata);

		/* Aggregate functions can either accumulate data over rows
		 * of a table *OR* over an array passed as the first argument.
		 * Here we're accumulating, but if the first argument is an
		 * array then the accumulated result will be ignored so we
		 * might as well skip it.
		 */
		if (args->first->type != EDJ_ARRAY) {
			/* Call the aggregator function */
			ag->ag[i]->u.func.jf->agfn(args, fnag);
		}

		/* Free its parameters */
		edj_free(args);

		/* Find the location of the next function's storage */
		fnag = (void *)((char *)fnag + ag->ag[i]->u.func.jf->agsize);
	}
}

/* Free/close anything that this aggregate function needs freed/closed */
static void cleanag(edjfunc_t *jf, void *agdata)
{
	void	**doomed = agdata;

	/* Supposed to free anything?  We could potentially need to free an
	 * (edj_t*) via edj_free(), a (void*) via free(), and a (FILE*) via
	 * fclose(), in that order, using pointers at the start of the ag
	 * data.  "doomed" points to the next pointer to free.
	 *
	 * NOTE: I tried doing each free/increment as a one-liner but got
	 * compiler error messages, probably because with -DEDJ_DEBUG_MEMORY,
	 * those freeing functions are actually macros.
	 */

	/* Maybe free an edj_t via edj_free() */
	if (jf->jfoptions & EDJFUNC_EDJFREE) {
		edj_t **doomed_edj = (edj_t**)doomed;
		edj_free(*doomed_edj);
		doomed_edj++;
		doomed = (void *)doomed_edj;
		/*edj_free(* (((edj_t **)doomed)++) );*/
	}

	/* Maybe free something via free() */
	if ((jf->jfoptions & EDJFUNC_FREE)) {
		void **doomed_void = (void**)doomed;
		free(*doomed_void);
		doomed_void++;
		doomed = (void *)doomed_void;
		/* free(*(((void **)doomed)++));*/
	}

	/* Maybe close a file stream via fclose() */
	if ((jf->jfoptions & EDJFUNC_FCLOSE)) {
		FILE *fp = *(FILE **)doomed;
		if (fp && fp != stdout && fp != stderr)
			fclose(fp);
	}
}
/* If calc uses aggregates, then allocate storage space for them and return
 * a pointer to that... or if existingag is non-NULL then reset it and return
 * it.  Otherwise return NULL to indicate that no aggregates are used.
 * Later, you can free the memory by calling edj_calc_ag(NULL, ag) even if
 * ag is NULL.
 */
void *edj_calc_ag(edjcalc_t *calc, void *existingag)
{
	/* If passed an existingag, then we're either about to free it or
	 * reset it.  Either way, maybe some functions want us to free up
	 * some of allocated data for them.
	 */
	if (existingag) {
		/* There's a list of ag function calls before the data.  Get it.
		 *
		 */
		edjcalc_t *ag = ((edjcalc_t **)existingag)[-1];
		int	i;
		char	*data;

		/* For each function call... */
		for (i = 0, data = (char *)existingag; i < ag->u.ag->nags; data += ag->u.ag->ag[i++]->u.func.jf->agsize) {
			edjfunc_t *jf = ag->u.ag->ag[i]->u.func.jf;
			cleanag(jf, data);
		}
	}

	/* Passing NULL for calc just means we should free existingag, if any */
	if (!calc) {
		if (existingag)
			free((char*)existingag - sizeof(edjcalc_t**));
		return NULL;
	}

	/* If no aggregates are used, then return NULL */
	if (calc->op != EDJOP_AG)
		return NULL;

	/* If no existingag, then allocate it now.  Also add space for a pointer */
	if (!existingag) {
		existingag = malloc(sizeof(edjcalc_t **) + calc->u.ag->agsize) + sizeof(edjcalc_t ***);
	}

	/* Reset it */
	memset(existingag, 0, calc->u.ag->agsize);
	((edjcalc_t **)existingag)[-1] = calc;
	return existingag;
}
 
/* If an edjcalc_t uses aggregate functions, then incorporate this row's
 * data into the aggregates.  If it doesn't use aggregates then do nothing.
 */
void edj_calc_ag_row(edjcalc_t *calc, edjcontext_t *context, void *agdata, edj_t *row)
{
	edjcontext_t *local;

	/* If no aggregates, then do nothing. */
	if (calc->op != EDJOP_AG)
		return;

	/* We must have agdata if we're using aggregates. */
	assert(agdata != NULL);

	/* Create a context with this row's data in it, and evaluate all
	 * aggretators with that.
	 */
	local = edj_context(context, row, EDJ_CONTEXT_THIS | EDJ_CONTEXT_NOFREE);
	jcag(calc->u.ag, local, agdata);
	edj_context_free(local);
}

/* This implements the @ and @@ operators.  "arr" is normally an array of items
 * to loop over, but it can also be a single item to treat as a singleton array.
 * "expr" is an expression to apply to each member of the array (which may
 * include aggregate functions), and op is either EDJOP_EACH, EDJOP_GROUP, or
 * EDJOP_FIRST.
 */
edj_t *jceach(edj_t *arr, edjcalc_t *calc, edjcontext_t *context, edjop_t op)
{
	edj_t	*scan, *gscan;
	edj_t	*result, *tmp;
	edjcontext_t *local;
	void *ag, **groupag;
	int	ngroups, nongroup, g;

	/* The array may include subarrays to indicate grouping. If grouping
	 * is used, there may or may not be ungrouped items.  We'll need
	 * separate aggregate data for each group, and one for the ungrouped
	 * aggregates.  STEP ONE: Allocate overall ag data, as a way to detect
	 * whether aggregates are indeed used.
	 */
	ngroups = 0;
	groupag = NULL;
	ag = edj_calc_ag(calc, NULL);
	if (ag) {
		/* STEP 1: Count groups, and watch for any ungrouped elements */
		for (ngroups = nongroup = 0, scan = edj_first(arr); scan; scan = edj_next(scan)) {
			if (scan->type == EDJ_ARRAY)
				ngroups++;
			else
				nongroup++;
		}

		/* STEP 2: Allocate an array to hold groups' aggregate data */
		if (ngroups > 0) {
			groupag = calloc(ngroups, sizeof(void *));
			for (g = 0; g < ngroups; g++)
				groupag[g] = edj_calc_ag(calc, NULL);
		}

		/* STEP 3: Loop over the array to generate aggregate data. */
		for (g = 0, scan = edj_first(arr); scan; scan = edj_next(scan)) {
			if (edj_interrupt) {
				edj_break(scan);
				result = edj_error_null(NULL, "intr:Interrupted");
				goto ReturnResult;
			}

			/* Is this element a nested array? */
			if (scan->type == EDJ_ARRAY) {
				/* Loop over the array elements */
				for (gscan = edj_first(scan); gscan; gscan = edj_next(gscan)) {
					if (edj_interrupt) {
						edj_break(gscan);
						edj_break(scan);
						result = edj_error_null(NULL, "intr:Interrupted");
						goto ReturnResult;
					}

					/* Invoke the aggregators on "this" */
					assert(groupag != NULL);
					local = edj_context(context, gscan, EDJ_CONTEXT_THIS | EDJ_CONTEXT_NOFREE);
					jcag(calc->u.ag, local, groupag[g]);
					if (nongroup)
						jcag(calc->u.ag, local, ag);
					edj_context_free(local);
				}

				/* Prepare for next group */
				g++;
			} else {
				/* Invoke the aggregators on "this" */
				local = edj_context(context, scan, EDJ_CONTEXT_THIS | EDJ_CONTEXT_NOFREE);
				jcag(calc->u.ag, local, ag);
				edj_context_free(local);
			}
		}
	}

	/* Loop over the array.  For each element, make it "this" and
	 * evaluate the right operand.  Collect the results in a new
	 * array.
	 */
	result = edj_array();
	for (g = 0, scan = edj_first(arr); scan; scan = edj_next(scan)) {
		/* Is it a group (nested array) ? */
		if (scan->type == EDJ_ARRAY) {
			/* Process the group using the group's own aggregate
			 * data.  For EACH process all of them, for GROUP only
			 * process the first.
			 */
			for (gscan = edj_first(scan); gscan; gscan = (op == EDJOP_EACH ? edj_next(gscan) : NULL)) {
				/* If interrupted then discard results so far
				 * and return an error null.
				 */
				if (edj_interrupt) {
					edj_break(gscan);
					edj_break(scan);
					edj_free(result);
					result = edj_error_null(NULL, "intr:Interrupted");
					goto ReturnResult;
				}

				/* Evaluate with element as "this" */
				local = edj_context(context, gscan, EDJ_CONTEXT_THIS | EDJ_CONTEXT_NOFREE);
				tmp = edj_calc(calc, local, groupag ? groupag[g] : NULL);
				edj_context_free(local);

				/* If null/false, skip it, if true add element*/
				if (tmp->type == EDJ_NULL || tmp->type == EDJ_BOOLEAN) {
					/* Skip for null or false, add for true */
					if (edj_is_true(tmp))
						edj_append(result, edj_copy(gscan));
					edj_free(tmp);
				} else {
					/* Not a symbol, append whatever it is */
					edj_append(result, tmp);
				}
			}
			edj_break(gscan); /* since EDJOP_GROUP/EDJOP_FIRST stops early */

			/* Prepare for the next group */
			g++;
		} else {
			/* If interrupted then discard results so far
			 * and return an error null.
			 */
			if (edj_interrupt) {
				result = edj_error_null(NULL, "intr:Interrupted");
				goto ReturnResult;
			}

			local = edj_context(context, scan, EDJ_CONTEXT_THIS | EDJ_CONTEXT_NOFREE);
			tmp = edj_calc(calc, local, ag);
			edj_context_free(local);
			if (tmp->type == EDJ_NULL || tmp->type == EDJ_BOOLEAN) {
				/* Skip for null or false, add for true */
				if (edj_is_true(tmp))
					edj_append(result, edj_copy(scan));
				edj_free(tmp);
			} else {
				/* Not a symbol, append whatever it is */
				edj_append(result, tmp);
			}
		}

		/* If EDJOP_FIRST and we've found the first, then stop */
		if (op == EDJOP_FIRST && result->first) {
			edj_break(scan);
			break;
		}
	}

ReturnResult:
	/* Clean up */
	edj_calc_ag(NULL, ag);
	if (ngroups > 0) {
		for (g = 0; g < ngroups; g++)
			edj_calc_ag(NULL, groupag[g]);
		free(groupag);
	}

	/* For EDJOP_FIRST, lift the first result out of the array.  If there
	 * is no first result, then use null.
	 */
	if (op == EDJOP_FIRST) {
		/* Lift the first element from the result array */
		tmp = result;
		result = result->first;
		tmp->first = NULL;
		edj_free(tmp);

		/* If no first element, then use null */
		if (!result)
			result = edj_null();
	}

	/* Done! */
	return result;
}


/* Evaluate an expression and return the result.
 *   calc       The expression to evaluate.  This should be obtained from a 
 *              previous call to edj_calc_parse().
 *   context    A list of objects providing context for the expression.
 *              The first element is "this".  Any element that's an object
 *              can be scanned to obtain variable names.  May be NULL.
 *   agdata     Storage space for aggregate functions, allocated by
 *              edj_calc_ag(calc, NULL), freed by edj_calc_ag(NULL, ag);
 *
 * NOTE: For runtime errors, this mostly returns a "null" edj_t node
 * containing an error message in ->text, and the error code in ->first.
 */
edj_t *edj_calc(edjcalc_t *calc, edjcontext_t *context, void *agdata)
{
	edj_t *left, *right, *freeleft, *freeright;
	edj_t *result;
	edjcalc_t *tmp;
	edj_t  *scan, *found;
	double  nl, nr;
	int     il,ir;
	char    *str;
	void    *localag;
	edjfuncextra_t recon;

	/* If interrupted then simply return an error null */
	if (edj_interrupt)
		return edj_error_null(NULL, "intr:Interrupted");

	/* Start with freeleft and freeleft set to NULL.  The USE_LEFT_OPERAND
	 * and USE_RIGHT_OPERAND macros will set them if appropriate.
	 */
	freeleft = freeright = result = NULL;

	/* Process the expression */
	switch (calc->op)
	{
	  case EDJOP_LITERAL:
		result = edj_copy(calc->u.literal);
		break;

	  case EDJOP_NAME:
		result = edj_copy(edj_context_by_key(context, calc->u.text, NULL));
		break;

	  case EDJOP_ENVIRON:
		/* Either $name or $name[subscr].  calc->LEFT is always a
		 * EDJOP_NAME, and calc->RIGHT is NULL or subscript expression.
		 */
		assert(calc->LEFT->op == EDJOP_NAME);
		if (calc->RIGHT) {
			USE_RIGHT_OPERAND(calc);
			if (right->type == EDJ_STRING
			 || right->type == EDJ_BOOLEAN
			 || (right->type == EDJ_NUMBER && right->text[0])) {
				char name[strlen(calc->LEFT->u.text) + strlen(right->text) + 1];
				strcpy(name, calc->LEFT->u.text);
				strcat(name, right->text);
				str = getenv(name);
			} else {
				char *sub = edj_serialize(right, NULL);
				char name[strlen(calc->LEFT->u.text) + strlen(sub) + 1];
				strcpy(name, calc->LEFT->u.text);
				strcat(name, sub);
				str = getenv(name);
				free(sub);
			}
		} else {
			str = getenv(calc->LEFT->u.text);
		}

		/* If we found a value, then convert it to an edj_t;
		 * otherwise return a null.
		 */
		if (str)
			result = edj_string(str, -1);
		else
			result = edj_null();
		str = NULL;
		break;

	  case EDJOP_ARRAY:
		/* Append the value of each element into an array. */
		result = edj_array();
		if (calc->LEFT)
			edj_append(result, edj_calc(calc->LEFT, context, agdata));
		for (tmp = calc->RIGHT; tmp; tmp = tmp->RIGHT)
			edj_append(result, edj_calc(tmp->LEFT, context, agdata));
		break;

	  case EDJOP_OBJECT:
		/* Append name:value pairs into an object */
		result = edj_object();
		if (calc->LEFT) {
			/* calc->LEFT is the first name:value.
			 * tmp is the name, with op=EDJOP_NAME.
			 * right is the value, to evaluate via edj_calc()
			 */
			tmp = calc->LEFT->LEFT;
			found = edj_calc(calc->LEFT->RIGHT, context, agdata);
			if (calc->LEFT->op != EDJOP_MAYBEMEMBER || !edj_is_null(found))
				edj_append(result, edj_key(tmp->u.text, found));
			else
				edj_free(found);
		}
		for (calc = calc->RIGHT; calc; calc = calc->RIGHT) {
			/* calc->LEFT is the next name:value.
			 * tmp is the name, with op=EDJOP_NAME.
			 * right is the value, to evaluate via edj_calc()
			 */
			tmp = calc->LEFT->LEFT;
			found = edj_calc(calc->LEFT->RIGHT, context, agdata);
			if (calc->LEFT->op != EDJOP_MAYBEMEMBER || !edj_is_null(found))
				edj_append(result, edj_key(tmp->u.text, found));
			else
				edj_free(found);
		}
		return result;

	  case EDJOP_SUBEXPR:
		USE_LEFT_OPERAND(calc);
		USE_RIGHT_OPERAND(calc);

		/* Left must be array or object, right must be a string */
		if (left->type != EDJ_ARRAY && left->type != EDJ_OBJECT)
			break;
		if (right->type != EDJ_STRING)
			break;

		/* Find the value */
		result = edj_by_expr(left, right->text, NULL, NULL, NULL);

		/* Return a copy of it */
		if (result)
			result = edj_copy(result);
		break;

	  case EDJOP_SUBSCRIPT:
		USE_LEFT_OPERAND(calc);
		if (calc->RIGHT->op == EDJOP_COLON) {
			char *key;

			/* Subscript by name:value, scans an array of objects
			 * for a given member and value.
			 */
			if (left->type != EDJ_ARRAY)
				break;

			/* Evaluate the value of name:value.  Also fetch name */
			USE_RIGHT_OPERAND(calc->RIGHT);
			key = calc->RIGHT->LEFT->u.text;

			/* Scan array for element with that member name:value */
			str = NULL;
			for (scan = edj_first(left); scan; scan = edj_next(scan)) {
				if (scan->type != EDJ_OBJECT)
					continue;
				found = edj_by_key(scan, key);
				if (found && found->type == EDJ_STRING && right->type == EDJ_STRING) {
					/* String comparison is case-insensitive */
					if (!edj_mbs_casecmp(found->text, right->text)) {
						result = edj_copy(scan);
						edj_break(scan);
						break;
					}
				} else if (found && found->type == EDJ_STRING && right->type != EDJ_STRING) {
					/* This handles the special case where
					 * the value we're searching for is a
					 * number, but the data we're comparing
					 * it to is a string.  We convert the
					 * search value to a string ONCE, and
					 * do a string comparison.
					 */
					if (!str)
						str = edj_serialize(right, NULL);
					if (!edj_mbs_casecmp(found->text, str)) {
						result = edj_copy(scan);
						edj_break(scan);
						break;
					}

				} else if (found && edj_equal(found, right)) {
					result = edj_copy(scan);
					edj_break(scan);
					break;
				}
			}
			if (str)
				free(str);
			break;
		} else {
			/* Evaluate the subscript.  Strings only work for
			 * objects, numbers only work for arrays or strings.
			 */
			USE_RIGHT_OPERAND(calc);
			if (left->type == EDJ_OBJECT && right->type == EDJ_STRING)
				result = edj_by_key(left, right->text);
			else if (left->type == EDJ_OBJECT) {
				/* convert to a string, use it as the key */
				str = edj_serialize(right, NULL);
				result = edj_by_key(left, str);
				free(str);
			} else if (left->type == EDJ_ARRAY && right->type == EDJ_NUMBER)
				result = edj_by_index(left, edj_int(right));
			else if (left->type == EDJ_STRING && right->type == EDJ_NUMBER) {
				size_t len = strlen(left->text);
				size_t end = 1; /* single character */
				ir = edj_int(right);
				if (ir < 0)
					ir += len;
				if (ir >= 0 && ir < len) {
					const char *str = edj_mbs_substr(left->text, ir, &end);
					result = edj_string(str, end);
					break;
				}
			}
		}

		/* Use a copy of the result.  Also, call edj_break() on it,
		 * just in case it came from a deferred array.
		 */
		found = edj_copy(result);
		edj_break(result);
		result = found;
		break;

	  case EDJOP_FNCALL:
		/* Collect parameter values into an array */
		freeleft = left = edj_calc(calc->u.func.args, context, agdata);

		/* Aggregate functions are special, if the first parameter is
		 * an array.  (The parser can't always tell whether the first
		 * parameter is going to be an array, so it'll create a
		 * EDJOP_AG node above this which may result it data being
		 * accumulated that way.  But if passed an array, it'll ignore
		 * that aggregated data and create new aggregated data from
		 * the array.)
		 */
		if (left->first->type == EDJ_ARRAY && calc->u.func.jf->agfn) {
			edjfunc_t *jf = calc->u.func.jf;

			/* Allocate storage for the function */
			localag = malloc(jf->agsize);
			memset(localag, 0, jf->agsize);

			/* For each element of the array, create a new parameter
			 * list and call the aggregator.  Note that we don't
			 * need to create a new context, because all parameters
			 * have already been calculated.
			 */
			found = edj_array();
			for (scan = edj_first(left->first); scan; scan = edj_next(scan)) { /* undeferred */
				/* Create a new argument list.  The first is an
				 * element from the array, and any other args
				 * are used unchanged. To accomplish this, we
				 * will temporarily mangle scan's "next".
				 */
				edj_t *scannext = scan->next; /* undeferred */
				scan->next = left->first->next; /* undeferred */
				found->first = scan;

				/* Invoke the aggregator */
				(*jf->agfn)(found, localag);

				/* Restore scan's "next" pointer */
				scan->next = scannext; /* undeferred */
			}
			found->first = NULL;
			edj_free(found);

			/* Invoke the function */
			result = (*jf->fn)(left, localag);

			/* Clean up */
			cleanag(jf, localag);
			free(localag);
		} else {
			/* Non-aggregate built-in functions may take a regular
			 * expression.  Since that isn't a JSON data type,
			 * the args list will just contain "null" there; we
			 * need to scan the argument array generator for a
			 * EDJOP_REGEX... but only for non-aggregate built-ins.
			 */
			localag = (void *)((char *)agdata + calc->u.func.agoffset);
			if (!calc->u.func.jf->agfn && !calc->u.func.jf->user) {
				recon.context = context;
				recon.regex = NULL;
				if (!calc->u.func.jf->user) {
					tmp = calc->u.func.args;
					if (tmp->LEFT && tmp->LEFT->op == EDJOP_REGEX)
						tmp = tmp->LEFT;
					else for (tmp = tmp->RIGHT; tmp; tmp = tmp->RIGHT)
						if (tmp->LEFT->op == EDJOP_REGEX) {
							tmp = tmp->LEFT;
							break;
					}
					recon.regex = (void *)tmp;
				}
				localag = (void *)&recon;
			}

			/* Invoke the function. For built-ins, call the
			 * function directly ("jf->fn").  For user-defined
			 * functions, call edj_cmd_fncall() to do it.
			 */
			if (calc->u.func.jf->user)
				result = edj_cmd_fncall(left, calc->u.func.jf, context);
			else if (calc->u.func.jf->fn)
				result = (*calc->u.func.jf->fn)(left, localag);
			else
				result = NULL; /* probably an empty user func */
		}
		break;

	  case EDJOP_AG:
		/* We always expect agdata when we're using aggregates, but
		 * if we aren't given agdata then use blank agdata.  We won't
		 * get useful results that way, but at least we won't dump core.
		 */
		if (!agdata) {
			/* Evaluate using blank agdata */
			localag = edj_calc_ag(calc, NULL);
			result = edj_calc(calc->u.ag->expr, context, localag);
			(void)edj_calc_ag(NULL, localag);
		} else {
			/* Evaluate expr, but use *this* agdata to do it */
			result = edj_calc(calc->u.ag->expr, context, agdata);
		}
		break;

	  case EDJOP_FIND:
		/* Evaluate the left operand.  Then pass that result and the
		 * right operand to edj_find_calc() to build the result table.
		 */
		USE_LEFT_OPERAND(calc);
		if (edj_is_null(left))
			result = left;
		else
			result = edj_find_calc(left, calc->RIGHT, context);
		break;

	  case EDJOP_EACH:
	  case EDJOP_GROUP:
	  case EDJOP_FIRST:
		/* Evaluate the left operand.  If null then return an empty
		 * array.  If it is an array then set scan to its first
		 * element; if not an array then set scan to it directly,
		 * so it'll effectively be treated like a single-element array.
		 */
		USE_LEFT_OPERAND(calc);
		if (edj_is_null(left)) {
			result = edj_array();
			break;
		}

		/* Do the thing */
		result = jceach(left, calc->RIGHT, context, calc->op);
		break;

	  case EDJOP_NJOIN:
	  case EDJOP_LJOIN:
	  case EDJOP_RJOIN:
		/* Natural join of left and right arrays.  The pairing-up
		 * logic is implemented in jcnjoin(), but we still have a bit
		 * of operand evaluation and cleanup to worry about here.
		 */
		USE_LEFT_OPERAND(calc);
		USE_RIGHT_OPERAND(calc);
		result = jcnjoin(left, right, calc->op == EDJOP_LJOIN, calc->op == EDJOP_RJOIN);
		/* NOTE: jcnjoin() always copies any data it uses.  Nothing
		 * in it could still be used by jl or jr.
		 */
		break;

	  case EDJOP_DOT:
	  case EDJOP_DOUBLEDOT:
		/* NOTE: Function calls of the form data.func(args...) are
		 * transformed to func(data, args...) during parsing, so we
		 * only see the . operator while looking for a member of an
		 * object.
		 */
		assert(calc->RIGHT->op == EDJOP_NAME);
		USE_LEFT_OPERAND(calc);
		if (left->type == EDJ_OBJECT && (result = edj_by_key(left, calc->RIGHT->u.text)) != NULL)
			result = edj_copy(result);
		else if (!strcasecmp(calc->RIGHT->u.text, "length")) {
			/* The "length" attribute is computed, for strings and
			 * arrays.  To simplify processing of data that was
			 * converted from XML, we also return 0 for null.length
			 * and 1 for anything_else.length -- XML doesn't do
			 * arrays very well.
			 */
			if (left->type == EDJ_ARRAY)
				result = edj_from_int(edj_length(left));
			else if (left->type == EDJ_STRING)
				result = edj_from_int(edj_mbs_len(left->text));
			else if (edj_is_null(left))
				result = edj_from_int(0);
			else
				result = edj_from_int(1);
		}
		break;

	  case EDJOP_DEEPDOT:
		USE_LEFT_OPERAND(calc);
		if (left->type == EDJ_OBJECT && calc->RIGHT->op == EDJOP_NAME)
			result = edj_copy(edj_by_deep_key(left, calc->RIGHT->u.text));
		break;

	  case EDJOP_ELLIPSIS:
		USE_LEFT_OPERAND(calc);
		USE_RIGHT_OPERAND(calc);
		if (left->type == EDJ_NUMBER && right->type == EDJ_NUMBER) {
			il = edj_int(left);
			ir = edj_int(right);
#if 0
			result = edj_array();
			for (; il <= ir; il++) {
				edj_append(result, edj_from_int(il));
			}
#else
			result = edj_defer_ellipsis(il, ir);
#endif
		}
		break;

	  case EDJOP_COALESCE:
		/* If left arg is non-null, return it */
		USE_LEFT_OPERAND(calc);
		if (!edj_is_null(left)) {
			if (freeleft) {
				result = left;
				freeleft = NULL;
			} else
				result = edj_copy(left);
			break;
		}

		/* Else return right arg */
		USE_RIGHT_OPERAND(calc);
		if (freeright) {
			result = right;
			freeright = NULL;
		} else
			result = edj_copy(right);
		break;

	  case EDJOP_QUESTION:
		USE_LEFT_OPERAND(calc);
		/* Can be test?then or test?them:else */
		if (calc->RIGHT->op == EDJOP_COLON) {
			if (edj_is_true(left))
				result = edj_calc(calc->RIGHT->LEFT, context, agdata);
			else
				result = edj_calc(calc->RIGHT->RIGHT, context, agdata);
		} else {
			if (edj_is_true(left)) {
				USE_RIGHT_OPERAND(calc);
				if (freeright) {
					result = freeright;
					freeright = NULL;
				} else {
					result = edj_copy(right);
				}
			}
		}
		break;

	  case EDJOP_COLON:
		/* Shouldn't happen. */
		abort();

	  case EDJOP_ISNULL:
		USE_RIGHT_OPERAND(calc);
		result = edj_boolean(edj_is_null(right));
		break;

	  case EDJOP_ISNOTNULL:
		USE_RIGHT_OPERAND(calc);
		result = edj_boolean(!edj_is_null(right));
		break;

	  case EDJOP_NEGATE:
		USE_RIGHT_OPERAND(calc);
		if (right->type == EDJ_NUMBER || right->type == EDJ_STRING)
			result = edj_from_double(-edj_double(right));
		break;

	  case EDJOP_ADD:
		USE_LEFT_OPERAND(calc);
		USE_RIGHT_OPERAND(calc);
		if (edj_is_date(left) && edj_is_period(right)) {
			/* ISO datetime+period.  Add the period to the
			 * datetime, and return the resulting datetime.
			 * Note that both the datetime and period are strings.
			 */
			char buf[50];
			edj_datetime_add(buf, left->text, right->text);
			buf[10] = '\0';
			result = edj_string(buf, -1);
		} else if ((edj_is_date(left) || edj_is_datetime(left)) && edj_is_period(right)) {
			/* ISO datetime+period.  Add the period to the
			 * datetime, and return the resulting datetime.
			 * Note that both the datetime and period are strings.
			 */
			char buf[50];
			edj_datetime_add(buf, left->text, right->text);
			result = edj_string(buf, -1);
		} else if (left->type == EDJ_STRING || right->type == EDJ_STRING) {
			/* String version.  If one of the operands is a
			 * non-string, then convert it to a string.
			 * One minor optimization is that if a number is in
			 * text form, or a boolean, then we can treat it as
			 * a string already.
			 */
			if ((left->type == EDJ_STRING || left->type == EDJ_BOOLEAN || (left->type == EDJ_NUMBER && *left->text))
			 && (right->type == EDJ_STRING || right->type == EDJ_BOOLEAN || (right->type == EDJ_NUMBER && *right->text))) {
				/* Both are strings, or at least stringy */
				result = edj_string(left->text, strlen(left->text) + strlen(right->text));
				strcat(result->text, right->text);
			} else if (left->type == EDJ_NULL) {
				if (freeright) {
					result = right;
					freeright = NULL;
				} else
					result = edj_copy(right);
			} else if (right->type == EDJ_NULL) {
				if (freeleft) {
					result = left;
					freeleft = NULL;
				} else
					result = edj_copy(left);
			} else if (left->type != EDJ_STRING) {
				/* Left operand needs to be converted */
				str = edj_serialize(left, NULL);
				result = edj_string(str, strlen(str) + strlen(right->text));
				strcat(result->text, right->text);
				free(str);
			} else { /* Right is not stringy */
				/* Right operand needs to be converted */
				str = edj_serialize(right, NULL);
				result = edj_string(left->text, strlen(left->text) + strlen(str));
				strcat(result->text, str);
				free(str);
			}
		}
		else if (left->type == EDJ_NUMBER && right->type == EDJ_NUMBER) {
			/* Number version */
			result = edj_from_double(edj_double(left) + edj_double(right));
		}
		break;

	  case EDJOP_SUBTRACT:
		USE_LEFT_OPERAND(calc);
		USE_RIGHT_OPERAND(calc);
		if (edj_is_date(left) && edj_is_period(right)) {
			/* ISO date-period.  Subtract the period from the
			 * datetime, and return the resulting datetime.
			 * Note that both the datetime and period are strings.
			 */
			char buf[50];
			edj_datetime_subtract(buf, left->text, right->text);
			buf[10] = '\0';
			result = edj_string(buf, -1);
		} else if (edj_is_datetime(left) && edj_is_period(right)) {
			/* ISO datetime-period.  Subtract the period from the
			 * datetime, and return the resulting datetime.
			 * Note that both the datetime and period are strings.
			 */
			char buf[50];
			edj_datetime_subtract(buf, left->text, right->text);
			result = edj_string(buf, -1);
		} else if ((edj_is_date(left) || edj_is_datetime(left))
		        && (edj_is_date(right) || edj_is_datetime(right))) {
			/* ISO datetime-datetime.  Find the difference between
			 * two dates, and return it as a period.
			 * Note that the datetimes are strings.
			 */
			char buf[50];
			edj_datetime_diff(buf, left->text, right->text);
			result = edj_string(buf, -1);
		} else if (left->type == EDJ_STRING || right->type == EDJ_STRING) {
			/* String version.  If one of the operands is a
			 * non-string, then convert it to a string.
			 * One minor optimization is that if a number is in
			 * text form, or a boolean, then we can treat it as
			 * a string already.
			 */
			char	*leftstr, *rightstr;
			size_t	leftlen;
			str = NULL;
			if ((left->type == EDJ_STRING || left->type == EDJ_BOOLEAN || (left->type == EDJ_NUMBER && *left->text))
			 && (right->type == EDJ_STRING || right->type == EDJ_BOOLEAN || (right->type == EDJ_NUMBER && *right->text))) {
				/* Both are strings, or at least stringy */
				leftstr = left->text;
				rightstr = right->text;
			} else if (left->type == EDJ_NULL) {
				if (freeright) {
					result = right;
					freeright = NULL;
				} else
					result = edj_copy(right);
				break;
			} else if (right->type == EDJ_NULL) {
				if (freeleft) {
					result = left;
					freeleft = NULL;
				} else
					result = edj_copy(left);
				break;
			} else if (left->type != EDJ_STRING) {
				/* Left operand needs to be converted */
				str = edj_serialize(left, NULL);
				leftstr = str;
				rightstr = right->text;
			} else { /* Right is not stringy */
				/* Right operand needs to be converted */
				str = edj_serialize(right, NULL);
				leftstr = left->text;
				rightstr = str;
			}

			/* Trim trailing spaces from leftstr and leading spaces
			 * from rightstr.
			 */
			for (leftlen = strlen(leftstr); leftlen > 0 && leftstr[leftlen - 1] == ' '; leftlen--) {
			}
			while (*rightstr == ' ')
				rightstr++;

			/* Allocate a response big enough for both trimmed
			 * strings and a space between them.  Copy text.
			 */
			result = edj_string(leftstr, leftlen + 1 + strlen(rightstr));
			if (leftlen > 0 && *rightstr)
				result->text[leftlen++] = ' ';
			strcpy(result->text + leftlen, rightstr);

			/* If we had to serialize a value, free that now */
			if (str)
				free(str);
		}
		else if (left->type == EDJ_NUMBER && right->type == EDJ_NUMBER) {
			/* Number version */
			result = edj_from_double(edj_double(left) - edj_double(right));
		}
		break;

	  case EDJOP_MULTIPLY:
	  case EDJOP_DIVIDE:
	  case EDJOP_MODULO:
		USE_LEFT_OPERAND(calc);
		USE_RIGHT_OPERAND(calc);
		if (left->type == EDJ_NUMBER && right->type == EDJ_NUMBER) {
			/* Convert to binary */
			nl = edj_double(left);
			nr = edj_double(right);

			/* Do the math */
			if (calc->op == EDJOP_MULTIPLY)
				result = edj_from_double(nl * nr);
			else if (nr == 0.0)
				result = edj_error_null(NULL, "div0:division by 0");
			else if (calc->op == EDJOP_DIVIDE)
				result = edj_from_double(nl / nr);
			else if ((int)nr == 0)
				result = edj_error_null(NULL, "mod0:modulo by 0");
			else /* EDJOP_MODULO */
				result = edj_from_double((int)nl % (int)nr);
		}
		break;

	  case EDJOP_BITNOT:
		USE_RIGHT_OPERAND(calc);
		if (right->type == EDJ_NUMBER)
			result = edj_from_int(~edj_int(right));
		break;

	  case EDJOP_BITLEFT:
	  case EDJOP_BITRIGHT:
		USE_LEFT_OPERAND(calc);
		USE_RIGHT_OPERAND(calc);
		if (left->type == EDJ_NUMBER && right->type == EDJ_NUMBER) {
			/* Convert to binary */
			il = edj_int(left);
			ir = edj_int(right);

			/* Do the bitwise math */
			if (calc->op == EDJOP_BITLEFT)
				result = edj_from_int(il << ir);
			else /* EDJOP_BITRIGHT */
				result = edj_from_int(il >> ir);
		}
		break;

	  case EDJOP_BITAND:
	  case EDJOP_BITOR:
	  case EDJOP_BITXOR:
		USE_LEFT_OPERAND(calc);
		USE_RIGHT_OPERAND(calc);
		if (left->type == EDJ_NUMBER && right->type == EDJ_NUMBER) {
			/* Convert to binary */
			il = edj_int(left);
			ir = edj_int(right);

			/* Do the bitwise math */
			if (calc->op == EDJOP_BITAND)
				result = edj_from_int(il & ir);
			else if (calc->op == EDJOP_BITOR)
				result = edj_from_int(il | ir);
			else /* EDJOP_BITOR */
				result = edj_from_int(il ^ ir);
		} else if (left->type == EDJ_OBJECT && right->type == EDJ_OBJECT) {
			if (calc->op == EDJOP_BITAND) {
				/* Keep left keys/values only if same key is in right */
				result = edj_object();
				for (scan = left->first; scan; scan = scan->next) { /* object */
					if (edj_by_key(right, scan->text))
						edj_append(result, edj_copy(scan));
				}
			} else if (calc->op == EDJOP_BITOR) {
				/* Merge right keys/values into left */
				result = edj_copy(left);
				for (scan = right->first; scan; scan = scan->next) { /* object */
					edj_append(result, edj_copy(scan));
				}
			} else { /* EDJOP_BITOR */
				/* Keep left keys/values only if key is NOT in right */
				result = edj_object();
				for (scan = left->first; scan; scan = scan->next) { /* object */
					if (!edj_by_key(right, scan->text))
						edj_append(result, edj_copy(scan));
				}
			}
		}
		break;

	  case EDJOP_NOT:
		USE_RIGHT_OPERAND(calc);
		result = edj_boolean(!edj_is_true(right));
		break;

	  case EDJOP_AND:
	  case EDJOP_OR:
		USE_LEFT_OPERAND(calc);
		il = edj_is_true(left);
		if (calc->op == (il ? EDJOP_AND : EDJOP_OR)) {
			USE_RIGHT_OPERAND(calc);
			il = edj_is_true(right);
		}
		result = edj_boolean(il);
		break;

	  case EDJOP_EQSTRICT:
	  case EDJOP_NESTRICT:
		USE_LEFT_OPERAND(calc);
		USE_RIGHT_OPERAND(calc);

		/* Compare them using edj_equal(), which checks data types.
		 * It also does a "deep" comparison, allowing you to compare
		 * the contents of arrays, or of objects.
		 */
		il = edj_equal(left, right);
		if (calc->op == EDJOP_NESTRICT)
			il = !il;
		result = edj_boolean(il);
		break;

	  case EDJOP_LT:
	  case EDJOP_LE:
	  case EDJOP_EQ:
	  case EDJOP_NE:
	  case EDJOP_GE:
	  case EDJOP_GT:
	  case EDJOP_ICEQ:
	  case EDJOP_ICNE:
		USE_LEFT_OPERAND(calc);
		USE_RIGHT_OPERAND(calc);

		/* Arrays and objects can't be compared this way.  They can
		 * be compared for strict equality, but not this.
		 */
		if (left->type == EDJ_ARRAY || left->type == EDJ_OBJECT
		 || right->type == EDJ_ARRAY || right->type == EDJ_OBJECT) {
			result = edj_error_null(NULL, "cmpObjArr:Can't compare objects/arrays except via === or !==");
			break;
		}

		/* Compare them in an appropriate way */
		if (left->type == EDJ_NUMBER && right->type == EDJ_NUMBER) {
			nl = edj_double(left);
			nr = edj_double(right);
			if (nl < nr)
				il = -1;
			else if (nl > nr)
				il = 1;
			else
				il = 0;
		} else if ((left->type == EDJ_BOOLEAN || right->type == EDJ_BOOLEAN)
		        && (calc->op == EDJOP_EQ || calc->op == EDJOP_NE)) {
			/* Compare as booleans, but only for equality */
			il = edj_is_true(left) != edj_is_true(right);
		} else if (left->type == EDJ_NULL || right->type == EDJ_NULL){
		        /* We allow equality comparisons to null. Anything else
		         * is always false.
		         */
		        if (calc->op == EDJOP_EQ || calc->op == EDJOP_NE)
				il = (left->type != right->type);
			else {
				result = edj_boolean(0);
				break;
			}
		} else if ((left->type == EDJ_NUMBER && right->type == EDJ_STRING)
			|| (left->type == EDJ_STRING && right->type == EDJ_NUMBER)) {
			/* When comparing strings and numbers, convert the
			 * string to a number.
			 */
			if (left->type == EDJ_NUMBER)
				nl = edj_double(left);
			else {
				nl = strtod(left->text, &str);
				if (*str) {
					/* Not a clean conversion, so not equal */
					result = edj_boolean(calc->op == EDJOP_NE || calc->op == EDJOP_ICNE);
					break;
				}
			}
			if (right->type == EDJ_NUMBER)
				nr = edj_double(right);
			else {
				nr = strtod(right->text, &str);
				if (*str) {
					/* Not a clean conversion, so not equal */
					result = edj_boolean(calc->op == EDJOP_NE || calc->op == EDJOP_ICNE);
					break;
				}
			}
			if (nl < nr)
				il = -1;
			else if (nl > nr)
				il = 1;
			else
				il = 0;
		} else {/* hopefully string, but other types work too */
			if (calc->op == EDJOP_ICEQ || calc->op == EDJOP_ICNE){
				size_t lenl, lenr, spacesl, spacesr;

				/* The tricky thing here is that we want to
				 * ignore trailing spaces.  This sounds like
				 * a job for edj_mbs_ncasecmp(), but that
				 * function specifies length by character
				 * count, but trailing spaces are easier to
				 * find via byte count, so we kind of have to
				 * do it both ways.
				 */

				/* Find the string lengths, in bytes */
				lenl = strlen(left->text);
				lenr = strlen(right->text);

				/* Count trailing spaces */
				for (spacesl = 0; spacesl < lenl && left->text[lenl - spacesl - 1] == ' '; spacesl++) {
				}
				for (spacesr = 0; spacesr < lenr && right->text[lenr - spacesr - 1] == ' '; spacesr++) {
				}

				/* Now we switch from bytes to characters.
				 * For the spacesl and spacesr variables,
				 * no conversion is needed since spaces are
				 * 1 byte each, always.  But lenl and lenr
				 * need to be recounted.
				 */
				lenl = edj_mbs_len(left->text);
				lenr = edj_mbs_len(right->text);

				/* Compare trimmed lengths.  If not the same
				 * then the strings don't match.  Otherwise we
				 * need to check the characters.
				 */
				if (lenl - spacesl != lenr - spacesr)
					il = 1;	/* trimmed lengths differ */
				else if (lenl - spacesl == 0)
					il = 0; /* both are empty */
				else
					il = edj_mbs_ncasecmp(left->text, right->text, lenl - spacesl);
			} else
				il = strcmp(left->text, right->text);
		}

		/* Choose a comparison */
		switch (calc->op) {
		  case EDJOP_EQ:
		  case EDJOP_ICEQ: ir = (il == 0); break;
		  case EDJOP_NE:
		  case EDJOP_ICNE: ir = (il != 0); break;
		  case EDJOP_LT:   ir = (il < 0);  break;
		  case EDJOP_LE:   ir = (il <= 0); break;
		  case EDJOP_GE:   ir = (il >= 0); break;
		  default: /* GT */ ir = (il > 0);  break;
		}

		/* Set the result */
		result = edj_boolean(ir);
		break;

	  case EDJOP_BETWEEN:
		assert(calc->RIGHT->op == EDJOP_AND);
		USE_LEFT_OPERAND(calc);

		/* Test lower bound. Note that we have to use USE_LEFT_OPERAND()
		 * again since calc->RIGHT is the entire "AND" clause and we
		 * want the left branch of that.  So first we juggle variables
		 * a bit...
		 */
		scan = left;
		found = freeleft;
		USE_LEFT_OPERAND(calc->RIGHT);
		right = left;
		freeright = freeleft;
		left = scan;
		freeleft = found;
		if (left->type == EDJ_NUMBER && right->type == EDJ_NUMBER) {
			if (edj_double(left) < edj_double(right))
				result = edj_boolean(0);
		} else if (left->type == EDJ_STRING && right->type == EDJ_STRING) {
			if (edj_mbs_casecmp(left->text, right->text) < 0)
				result = edj_boolean(0);
		} else if ((left->type == EDJ_NUMBER && right->type == EDJ_STRING)
			|| (left->type == EDJ_STRING && right->type == EDJ_NUMBER)) {
			/* When comparing strings and numbers, convert the
			 * string to a number.
			 */
			if (left->type == EDJ_NUMBER)
				nl = edj_double(left);
			else {
				nl = strtod(left->text, &str);
				if (*str) {
					/* Not a clean conversion */
					result = edj_boolean(0);
					break;
				}
			}
			if (right->type == EDJ_NUMBER)
				nr = edj_double(right);
			else {
				nr = strtod(right->text, &str);
				if (*str) {
					/* Not a clean conversion */
					result = edj_boolean(0);
					break;
				}
			}
			if (nl < nr)
				result = edj_boolean(0);
		} else
			result = edj_error_null(NULL, "between:BETWEEN only works on strings and numbers");

		if (freeright) {
			edj_free(freeright);
			freeright = NULL;
		}

		/* Test upper bound.  If we already know the tested value is
		 * below the lower bound, we can skip this.
		 */
		if (!result) {
			USE_RIGHT_OPERAND(calc->RIGHT);
			if (left->type == EDJ_NUMBER && right->type == EDJ_NUMBER) {
				if (edj_double(left) > edj_double(right))
					result = edj_boolean(0);
			} else if (left->type == EDJ_STRING && right->type == EDJ_NUMBER) {
				if (edj_mbs_casecmp(left->text, right->text) > 0)
					result = edj_boolean(0);
			} else if ((left->type == EDJ_NUMBER && right->type == EDJ_STRING)
				|| (left->type == EDJ_STRING && right->type == EDJ_NUMBER)) {
				/* When comparing strings and numbers, convert
				 * the string to a number.
				 */
				if (left->type == EDJ_NUMBER)
					nl = edj_double(left);
				else {
					nl = strtod(left->text, &str);
					if (*str) {
						/* Not a clean conversion */
						result = edj_boolean(0);
						break;
					}
				}
				if (right->type == EDJ_NUMBER)
					nr = edj_double(right);
				else
					nr = strtod(right->text, &str);
				if (nl > nr) {
					result = edj_boolean(0);
					if (*str) {
						/* Not a clean conversion */
						result = edj_boolean(0);
						break;
					}
				}
			} else
				result = edj_error_null(NULL, "between:BETWEEN only works on strings and numbers");
		}

		/* If no result, I guess we're okay */
		if (!result)
			result = edj_boolean(1);

		break;

	  case EDJOP_LIKE:
	  case EDJOP_NOTLIKE:
		USE_LEFT_OPERAND(calc);
		if (calc->RIGHT->op == EDJOP_REGEX) {
			regmatch_t matches[10];
			if (left->type == EDJ_STRING
			 && regexec((regex_t *)calc->RIGHT->u.regex.preg, left->text, 10, matches, 0) == 0)
				result = edj_boolean(1);
			else
				result = edj_boolean(0);
		} else  {
			USE_RIGHT_OPERAND(calc);
			if (left->type != EDJ_STRING || right->type != EDJ_STRING) {
				result = edj_boolean(0);
			} else {
				il = edj_mbs_like(left->text, right->text);
				if (calc->op == EDJOP_NOTLIKE)
					il = !il;
				result = edj_boolean(il);
			}
		}
		break;

	  case EDJOP_IN:
	  case EDJOP_NOTIN:
		USE_LEFT_OPERAND(calc);
		USE_RIGHT_OPERAND(calc);

		/* Scan the right-hand list, looking for an exact match */
		if (right->type == EDJ_ARRAY) {

			if (left->type == EDJ_STRING) {
				/* Is "right" a single-column table? */
				if (edj_is_table(right)) {
					/* If single column, check value */
					for (scan = edj_first(right); scan; scan = edj_next(scan)) {
						if (scan->first->first
						 && scan->first->first->type == EDJ_STRING
						 && scan->first->next == NULL /* object */
						 && !edj_mbs_casecmp(left->text, scan->first->first->text))
							break;
					}
				} else {
					/* compare strings to strings */
					for (scan = edj_first(right); scan; scan = edj_next(scan)) {
						if (scan->type == EDJ_STRING
						 && !edj_mbs_casecmp(left->text, scan->text))
							break;
					}
				}

			} else if (left->type == EDJ_NUMBER && edj_is_table(right)) {
				/* If single column, compare to value */
				for (scan = edj_first(right); scan; scan = edj_next(scan)) {
					if (scan->first->next == NULL /* object */
					 && edj_equal(left, scan->first->first))
						break;
				}
			} else {
				for (scan = edj_first(right); scan; scan = edj_next(scan)) {
					if (edj_equal(left, scan))
						break;
				}
			}
			if (calc->op == EDJOP_IN)
				result = edj_boolean(scan != NULL);
			else
				result = edj_boolean(scan == NULL);

			/* Just in case right is a deferred array, and we ended
			 * the scan prematurely...
			 */
			edj_break(scan);
		}
		break;

	  case EDJOP_FROM:
		/* This is used to fetch the default table for a SELECT
		 * statement that has no explicit FROM clause.  It is handled
		 * by jcsimple(), usually as an argument for the @ operator.
		 * When SELECT is used without columns or WHERE, then we end
		 * up here instead.
		 *
		 * When used with @, we can avoid creating a copy of the
		 * table... BUT NOT HERE!  Since edj_calc is returning the
		 * table, it must be something that the calling function can
		 * free.
		 */
		result = jcsimple(calc, context);
		if (result)
			result = edj_copy(result);
		else
			result = edj_error_null(NULL, "noDefTable:There is no default table for SELECT");
		break;

	  case EDJOP_VALUES:
		USE_LEFT_OPERAND(calc);
		USE_RIGHT_OPERAND(calc);
		result = jcvalues(left, right);
		break;

	  case EDJOP_REGEX:
		/* Using a regular expression where it isn't expected is an error */
		break;

	  case EDJOP_ASSIGN:
		USE_RIGHT_OPERAND(calc);

		/* If error, then just return the error.  Don't assign */
		if (right->type == EDJ_NULL && *right->text) {
			/* Errors are never stored in variables, so we know
			 * it's freshly allocated.  Return it as the result
			 * without freeing it.
			 */
			assert(freeright);
			freeright = NULL;
			result = right;
			break;
		}

		/* We always want a copy */
		if (!freeright)
			freeright = right = edj_copy(right);

		result = edj_context_assign(calc->LEFT, right, context);
		if (result == NULL) {
			/* success, so the right value is still used */
			freeright = NULL;
		}
		break;

	  case EDJOP_MAYBEASSIGN:
		USE_RIGHT_OPERAND(calc);

		/* If the right operand is null, do nothing.  Otherwise... */
		if (!edj_is_null(right)) {
			/* We always want a copy */
			if (!freeright)
				freeright = right = edj_copy(right);

			result = edj_context_assign(calc->LEFT, right, context);
			if (result == NULL) {
				/* success, so the right value is still used */
				freeright = NULL;
			}
		}
		break;

	  case EDJOP_APPEND:
		USE_RIGHT_OPERAND(calc);

		/* We always want a copy */
		if (!freeright)
			freeright = right = edj_copy(right);

		result = edj_context_append(calc->LEFT, right, context);
		if (result == NULL) {
			/* success, so the right value is still used */
			freeright = NULL;
		}
		break;

	  case EDJOP_STRING:
	  case EDJOP_NUMBER:
	  case EDJOP_BOOLEAN:
	  case EDJOP_NULL:
	  case EDJOP_STARTPAREN:
	  case EDJOP_ENDPAREN:
	  case EDJOP_STARTARRAY:
	  case EDJOP_ENDARRAY:
	  case EDJOP_STARTOBJECT:
	  case EDJOP_ENDOBJECT:
	  case EDJOP_COMMA:
	  case EDJOP_INVALID:
	  case EDJOP_SELECT:
	  case EDJOP_AS:
	  case EDJOP_DISTINCT:
	  case EDJOP_WHERE:
	  case EDJOP_GROUPBY:
	  case EDJOP_HAVING:
	  case EDJOP_ORDERBY:
	  case EDJOP_DESCENDING:
	  case EDJOP_LIMIT:
	  case EDJOP_MAYBEMEMBER:
		/* These are only used during parsing, not evaluation */
		abort();
	}

	/* If no result, then use null */
	if (!result)
		result = edj_null();

	/* Free operands, if appropriate */
	edj_free(freeleft);
	edj_free(freeright);

	/* Return the result */
	return result;
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <edj.h>

/* This module exists to define some Math.xxx() functions. */

/* Most of the Math.functions are pretty similar.  They take an optional
 * Math object as their first parameter, and a single number as their only
 * meaningful parameter.  We can handle most of that efficiently here.
 */
static edj_t *common(edj_t *args, char *name)
{
	edj_t	*arg;
	double	d;

	/* Skip over the Math object, if given */
	arg = args->first;
	if (arg->type == EDJ_OBJECT)
		arg = arg->next;

	/* Must be a single number */
	if (arg->type != EDJ_NUMBER || arg->next)
		return edj_error_null(0, "The %s() function expects single number as its argument", name);

	/* Convert to binary */
	d = edj_double(arg);

	/* Clear the error code, so we can detect math errors */
	errno = 0;

	/* Do the thing... */
	switch (name[0]) {
	case 'a':
		if (!strcmp(name, "acos"))
			d = acos(d);
		else if (!strcmp(name, "acosh"))
			d = acosh(d);
		else if (!strcmp(name, "asin"))
			d = asin(d);
		else if (!strcmp(name, "asinh"))
			d = asinh(d);
		else if (!strcmp(name, "atan"))
			d = asin(d);
		else if (!strcmp(name, "atanh"))
			d = asinh(d);
		else
			goto InvalidName;
		break;
	case 'c':
		if (!strcmp(name, "cbrt"))
			d = cbrt(d);
		else if (!strcmp(name, "ceil"))
			d = ceil(d);
		else if (!strcmp(name, "cos"))
			d = cos(d);
		else if (!strcmp(name, "cosh"))
			d = cosh(d);
		else
			goto InvalidName;
		break;
	case 'e':
		d = exp(d);
		break;
	case 'f':
		d = floor(d);
		break;
	case 'l':
		if (!strcmp(name, "log"))
			d = log(d);
		else if (!strcmp(name, "log10"))
			d = log10(d);
		else if (!strcmp(name, "log2"))
			d = log2(d);
		else
			goto InvalidName;
		break;
	case 'r':
		d = round(d);
		break;
	case 's':
		if (!strcmp(name, "sin"))
			d = sin(d);
		else if (!strcmp(name, "sinh"))
			d = sinh(d);
		else if (!strcmp(name, "sqrt"))
			d = sqrt(d);
		else
			goto InvalidName;
		break;
	case 't':
		if (!strcmp(name, "tan"))
			d = tan(d);
		else if (!strcmp(name, "trunc"))
			d = trunc(d);
		else
			goto InvalidName;
		break;
	default:
		goto InvalidName;
	}

	/* If an error occurred, say to */
	switch (errno) {
	case 0:		break; /* no error */
	case EDOM:	return edj_error_null(0, "Domain error in %s() function", name);
	case ERANGE:	return edj_error_null(0, "Range error in %s() function", name);
	default:	return edj_error_null(0, "Error in %s() function", name);
	}

	/* Return the result */
	return edj_from_double(d);

InvalidName:
	fprintf(stderr, "Invalid Math.function named \"%s\" encountered in the math plugin", name);
	abort();
}

static edj_t *jfn_acos(edj_t *args, void *agdata)
{
	return common(args, "acos");
}

static edj_t *jfn_acosh(edj_t *args, void *agdata)
{
	return common(args, "acosh");
}

static edj_t *jfn_asin(edj_t *args, void *agdata)
{
	return common(args, "asin");
}

static edj_t *jfn_asinh(edj_t *args, void *agdata)
{
	return common(args, "asinh");
}

static edj_t *jfn_atan(edj_t *args, void *agdata)
{
	return common(args, "atan");
}

static edj_t *jfn_atanh(edj_t *args, void *agdata)
{
	return common(args, "atanh");
}

static edj_t *jfn_cbrt(edj_t *args, void *agdata)
{
	return common(args, "cbrt");
}

static edj_t *jfn_ceil(edj_t *args, void *agdata)
{
	return common(args, "ceil");
}

static edj_t *jfn_cos(edj_t *args, void *agdata)
{
	return common(args, "cos");
}

static edj_t *jfn_cosh(edj_t *args, void *agdata)
{
	return common(args, "cosh");
}

static edj_t *jfn_exp(edj_t *args, void *agdata)
{
	return common(args, "exp");
}

static edj_t *jfn_floor(edj_t *args, void *agdata)
{
	return common(args, "floor");
}

static edj_t *jfn_log(edj_t *args, void *agdata)
{
	return common(args, "log");
}

static edj_t *jfn_log10(edj_t *args, void *agdata)
{
	return common(args, "log10");
}

static edj_t *jfn_log2(edj_t *args, void *agdata)
{
	return common(args, "log2");
}

static edj_t *jfn_round(edj_t *args, void *agdata)
{
	return common(args, "round");
}

static edj_t *jfn_sin(edj_t *args, void *agdata)
{
	return common(args, "sin");
}

static edj_t *jfn_sinh(edj_t *args, void *agdata)
{
	return common(args, "sinh");
}

static edj_t *jfn_sqrt(edj_t *args, void *agdata)
{
	return common(args, "sqrt");
}

static edj_t *jfn_tan(edj_t *args, void *agdata)
{
	return common(args, "tan");
}

static edj_t *jfn_trunc(edj_t *args, void *agdata)
{
	return common(args, "trunc");
}

/* The following are different, in that they take 2 arguments */

static edj_t *jfn_atan2(edj_t *args, void *agdata)
{
	edj_t	*arg;
	double	x, y;

	/* Skip over the Math object, if given */
	arg = args->first;
	if (arg->type == EDJ_OBJECT)
		arg = arg->next;

	/* Must be two numbers */
	if (arg->type != EDJ_NUMBER || !arg->next || arg->next->type != EDJ_NUMBER || arg->next->next)
		return edj_error_null(0, "The %s() function expects two numbers number as its arguments", "atan2");

	/* Convert to binary */
	y = edj_double(arg);
	x = edj_double(arg->next);

	/* Clear errno so we can detect errors */
	errno = 0;

	/* Do it */
	x = atan2(y, x);

	/* If error, say so */
	if (errno)
		return edj_error_null(0, "Error in %s() function", "atan2");

	/* Return the result */
	return edj_from_double(x);
}

static edj_t *jfn_hypot(edj_t *args, void *agdata)
{
	edj_t	*arg;
	double	d, sumsquared;

	/* Skip over the Math object, if given */
	arg = args->first;
	if (arg->type == EDJ_OBJECT)
		arg = arg->next;

	/* If given a single number, just return its absolute value */
	if (arg->type == EDJ_NUMBER && !arg->next)
		return edj_from_double(abs(edj_double(arg)));

	/* Reset errno so we can detect errors */
	errno = 0;

	/* Sum up the squares of the arguments. */
	for(sumsquared = 0; arg && errno == 0; arg = arg->next) {
		d = edj_double(arg);
		sumsquared += d * d;
	}

	/* Take the square root */
	if (!errno)
		d = sqrt(sumsquared);

	/* If error, say so */
	if (errno)
		return edj_error_null(0, "Error in %s() function", "hypot");

	/* Return the result */
	return edj_from_double(d);

BadArgs:
	return edj_error_null(0, "The %s() function expects at least two numbers number as its arguments", "hypot");
}

static edj_t *jfn_pow(edj_t *args, void *agdata)
{
	edj_t	*arg;
	double	base, power;

	/* Skip over the Math object, if given */
	arg = args->first;
	if (arg->type == EDJ_OBJECT)
		arg = arg->next;

	/* Must be two numbers */
	if (arg->type != EDJ_NUMBER || !arg->next || arg->next->type != EDJ_NUMBER || arg->next->next)
		return edj_error_null(0, "The %s() function expects two numbers number as its arguments", "pow");

	/* Convert to binary */
	base = edj_double(arg);
	power = edj_double(arg->next);

	/* Clear errno so we can detect errors */
	errno = 0;

	/* Do it */
	base = pow(base, power);

	/* If error, say so */
	if (errno)
		return edj_error_null(0, "Error in %s() function", "pow");

	/* Return the result */
	return edj_from_double(base);
}

/* This is the init function.  It registers all of the above functions, and
 * adds some constants to the Math object.
 */
char *pluginmath()
{
	edj_t	*math;

	/* Register the functions */
	edj_calc_function_hook("acos",  "n:number", "number", jfn_acos);
	edj_calc_function_hook("acosh", "n:number", "number", jfn_acosh);
	edj_calc_function_hook("asin",  "n:number", "number", jfn_asin);
	edj_calc_function_hook("asinh", "n:number", "number", jfn_asinh);
	edj_calc_function_hook("atan",  "n:number", "number", jfn_atan);
	edj_calc_function_hook("atanh", "n:number", "number", jfn_atanh);
	edj_calc_function_hook("cbrt",  "n:number", "number", jfn_cbrt);
	edj_calc_function_hook("ceil",  "n:number", "number", jfn_ceil);
	edj_calc_function_hook("cos",   "n:number", "number", jfn_cos);
	edj_calc_function_hook("cosh",  "n:number", "number", jfn_cosh);
	edj_calc_function_hook("exp",   "n:number", "number", jfn_exp);
	edj_calc_function_hook("floor", "n:number", "number", jfn_floor);
	edj_calc_function_hook("log",   "n:number", "number", jfn_log);
	edj_calc_function_hook("log10", "n:number", "number", jfn_log10);
	edj_calc_function_hook("log2",  "n:number", "number", jfn_log2);
	edj_calc_function_hook("round", "n:number", "number", jfn_round);
	edj_calc_function_hook("sin",   "n:number", "number", jfn_sin);
	edj_calc_function_hook("sinh",  "n:number", "number", jfn_sinh);
	edj_calc_function_hook("sqrt",  "n:number", "number", jfn_sqrt);
	edj_calc_function_hook("tan",   "n:number", "number", jfn_tan);
	edj_calc_function_hook("trunc", "n:number", "number", jfn_trunc);

	edj_calc_function_hook("atan2", "y:number, x:number", "number", jfn_atan2);
	edj_calc_function_hook("hypot", "n1:number, ...", "number", jfn_hypot);
	edj_calc_function_hook("pow",   "base:number, power:number", "number", jfn_pow);

	/* Insert constants into the Math object */
	math = edj_by_key(edj_system, "Math");
	if (!math) {
		math = edj_object();
		edj_append(edj_system, edj_key("Math", math));
	}
	edj_append(math, edj_key("E", edj_from_double(M_E)));
	edj_append(math, edj_key("LN10", edj_from_double(M_LN10)));
	edj_append(math, edj_key("LN2", edj_from_double(M_LN2)));
	edj_append(math, edj_key("LOG10E", edj_from_double(M_LOG10E)));
	edj_append(math, edj_key("LOG2E", edj_from_double(M_LOG2E)));
	edj_append(math, edj_key("PI", edj_from_double(M_PI)));
	edj_append(math, edj_key("SQRT1_2", edj_from_double(M_SQRT1_2)));
	edj_append(math, edj_key("SQRT2", edj_from_double(M_SQRT2)));

	/* Success */
	return NULL;
}

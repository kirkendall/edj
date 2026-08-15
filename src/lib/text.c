#include <stdlib.h>
#include <stdio.h>
#include <edj.h>

static char *defaultvalue;

/* Set the default value for edj_text, when no data is found.  Returns the
 * old default.
 */
char *edj_default_text(char *newdefault)
{
	char	*olddefault = defaultvalue;
	defaultvalue = newdefault;
	return olddefault;
}

/* Return the value of an edj_t.  If given NULL, then it returns the default
 * value as set by edj_default_text().
 */
char *edj_text(edj_t *json)
{
	if (!json)
		return defaultvalue;
	/* Maybe complain here if given an object, array, or key? */
	return json->text;
}

/* Return the value of a number as a double */
double edj_double(edj_t *json)
{
	if (!json || json->type != EDJ_NUMBER)
		return -1.0;
	if (json->text[0] == '\0' && json->text[1] == 'i')
		return (double)EDJ_INT(json);
	if (json->text[0] == '\0' && json->text[1] == 'd')
		return EDJ_DOUBLE(json);
	return atof(json->text);
}

/* Return the value of a number as an int */
int edj_int(edj_t *json)
{
	if (!json || json->type != EDJ_NUMBER)
		return -1;
	if (json->text[0] == '\0' && json->text[1] == 'i')
		return EDJ_INT(json);
	if (json->text[0] == '\0' && json->text[1] == 'd')
		return (int)EDJ_DOUBLE(json);
	return atoi(json->text);
}

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <edj.h>

/* Predict the size of the string returned by edj_serialize.  If buf is passed
 * then also store the actual characters there.  Note that the terminating '\0'
 * character is *NOT* included in the count, so you'll need to add 1 to the
 * returned size when allocating a buffer.
 */
static size_t jcseriallen(edj_t *json, char *buf, edjformat_t *format)
{
	size_t	len, sublen;
	edj_t	*scan;
	char	*tmp, number[40];

	len = 0;
	switch (json->type)
	{
	  case EDJ_OBJECT:
		if (buf) *buf++ = '{';
		len = 2; /* for the opening and closing brackets/braces */
		for (scan = json->first; scan; scan = scan->next) /* object */
		{
			sublen = jcseriallen(scan, buf, format);
			if (buf) buf += sublen;
			len += sublen;
			if (!edj_is_last(scan))
			{
				len++; /* for the comma between members */
				if (buf) *buf++ = ',';
			}
		}
		if (buf) *buf++ = '}';
		break;

	  case EDJ_ARRAY:
		if (buf) *buf++ = '[';
		len = 2; /* for the opening and closing brackets/braces */
		for (scan = edj_first(json); scan; scan = edj_next(scan))
		{
			sublen = jcseriallen(scan, buf, format);
			if (buf) buf += sublen;
			len += sublen;
			if (!edj_is_last(scan))
			{
				len++; /* for the comma between elements */
				if (buf) *buf++ = ',';
			}
		}
		if (buf) *buf++ = ']';
		break;

	  case EDJ_KEY:
		len = 3; /* Quotes around the key, and a colon after it */
		len += strlen(json->text);
		if (buf)
		{
			*buf++ = '"';
			strcpy(buf, json->text);
			buf += strlen(buf);
			*buf++ = '"';
			*buf++ = ':';
		}
		len += jcseriallen(json->first, buf, format);
		break;

	  case EDJ_STRING:
	  	if (buf)
	  	        *buf++ = '"';
	  	len = 2; /* Quotes around the string */
                sublen = edj_mbs_escape(buf, json->text, -1, '"', format);
                len += sublen;
	  	if (buf)
	  	        buf[sublen] = '"';
		break;

	  case EDJ_NUMBER:
		if (json->text[0] == '\0' && json->text[1] == 'i')
			snprintf(tmp = number, sizeof number, "%i", EDJ_INT(json));
		else if (json->text[0] == '\0' && json->text[1] == 'd')
			snprintf(tmp = number, sizeof number, "%.*g", format->digits, EDJ_DOUBLE(json));
		else
			tmp = json->text;
		len += strlen(tmp);
		if (buf)
			strcpy(buf, tmp);
		break;

	  case EDJ_BOOLEAN:
		len += strlen(json->text); /* simple value */
		if (buf)
			strcpy(buf, json->text);
		break;

	  case EDJ_NULL:
		len += 4;
		if (buf)
			strcpy(buf, "null");
		break;

	  default:
		; /* can't happen */
	}
	return len;
}


/* Return a dynamically-allocated JSON string for a given object.  */
char *edj_serialize(edj_t *json, edjformat_t *format)
{
	size_t len;
	char	*buf;

	/* Defend against NULL */
	if (!json)
		return strdup("null");

	/* If no format specified, use the default */
	if (!format)
		format = &edj_format_default;

	/* Determine how much string we need */
	len = jcseriallen(json, NULL, format);
	len++; /* for the terminating NUL */

	/* Allocate the buffer */
	buf = malloc(len);

	/* Fill the buffer */
	len = jcseriallen(json, buf, format);
	buf[len] = '\0';

	/* return it */
	return buf;
}

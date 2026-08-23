/* gentemplate.c */

/* This file is not meant to be compiled separately -- it is included in
 * xml.c, and compiled as part of that.  Hence the lack of #includes.
 */


/* This datatype is used to track the state of the XML generator. */
typedef struct {
	edj_t	*stack[100];	/* Tracks nested loops */
	int	sp;		/* Stack pointer */
	const char *scan;	/* Position within the template */
	char	*build;		/* Where to store generated text (may be NULL)*/
	size_t	len;		/* Length of generated text so far */
	size_t	maxLen;		/* Maximum "len", useful to allocate a buffer */
	int	nsubs;		/* Number of non-empty substitutions made */
} xmlTemplate_t;

static const char *template_tag(xmlTemplate_t *tmp);
static const char *template_loop(xmlTemplate_t *tmp);
static const char *template_content(xmlTemplate_t *tmp);

/* Search for a key in the nesting stack */
static edj_t *template_key(xmlTemplate_t *tmp, int arrayOk)
{
	size_t	symlen;
	char	*sym;
	int	i;
	edj_t	*found;

	/* Collect characters.  Can be anything except ":<>=. '\"\0".  */
	for (symlen = 0; tmp->scan[symlen] > ' ' && !strchr(":<>=.'\"", tmp->scan[symlen]); symlen++) {
	}

	/* If length is 0, return NULL */
	if (symlen == 0)
		return NULL;

	/* "this" and "that" are special */
	if (symlen == 4 && (!strncmp(tmp->scan, "this", 4) || !strncmp(tmp->scan, "that", 4))) {
		/* Look for the first or second instance of a non-object/array
		 * in the loop nest.
		 */
		int skipFirst = !strncmp(tmp->scan, "that", 4);
		tmp->scan += 4; /* move past "this" or "that" */
		for (i = tmp->sp; i >= 0; i--) {
			if ((tmp->stack[i]->type != EDJ_ARRAY || arrayOk)
			 && tmp->stack[i]->type != EDJ_OBJECT) {
				if (skipFirst)
					skipFirst = 0;
				else
					return tmp->stack[i];
			}
		}
		return NULL;
	}

	/* Copy the symbol into a temporary buffer */
	sym = malloc(symlen + 1);
	strncpy(sym, tmp->scan, symlen);
	sym[symlen] = '\0';
	tmp->scan += symlen;

	/* Look for the data in each layer of nested loops */
	for (i = tmp->sp; i >= 0; i--) {
		if (tmp->stack[i]->type == EDJ_OBJECT) {
			found = edj_by_key(tmp->stack[i], sym);
			if (found) {
				free(sym);
				return found;
			}
		}
	}

	/* Not found */
	free(sym);
	return NULL;
}

/* Copy a string to the build buffer, using entities */
static void template_entity_copy(xmlTemplate_t *tmp, const char *str)
{
	char	*entity;
	size_t	len;

	for (; *str; str++) {
		/* Decide whether to use an entity */
		switch (*str) {
		case '&':	entity = "&amp;";	len = 5;	break;
		case '<':	entity = "&lt;";	len = 4;	break;
		case '>':	entity = "&gt;";	len = 4;	break;
		case '"':	entity = "&quot;";	len = 6;	break;
		default:	entity = NULL;
		}

		/* Do it */
		if (entity) {
			if (tmp->build) {
				strcpy(tmp->build, entity);
				tmp->build += len;
			}
			tmp->len += len;
		} else {
			if (tmp->build)
				*tmp->build++ = *str;
			tmp->len++;
		}
	}
	if (tmp->len > tmp->maxLen)
		tmp->maxLen = tmp->len;
}

/* Process a single tag and its contents.  This assumes tmp->scan points to
 * the '<' at the start of the tag.  Returns NULL on success, or an error
 * message on failure.
 */
static const char *template_tag(xmlTemplate_t *tmp)
{
	int	nsubsBeforeTag;	/* used to detect substitutions in tag */
	size_t	lenBeforeTag;	/* position of build before processing tag */
	int	nsubsBeforeAttr;/* used to detect substitutions in attribute */
	size_t	lenBeforeAttr;	/* Position of build before processing attribute */
	const char *tagName;	/* Start of the tag name */
	size_t	tagLen;		/* Length of the tag name */
	int	ifSubsTag;	/* Is this <*tag> ? */
	int	ifSubsAttr;	/* Is this attr="$*key"? */
	int	content;	/* 1 if we expect content and </tag> */
	int	quote;		/* 1 if in quotes for attribute valuer */
	edj_t	*value;		/* Value of a $key */
	const char *error;	/* Error from a recursive parsing call */
	const char *text;

	/* Move past the "<" */
	tmp->scan++;

	/* Remember the number of substitutions before this, so we can detect
	 * whether any happened while processing the tag.
	 */
	nsubsBeforeTag = tmp->nsubs;
	lenBeforeTag = tmp->len;
	ifSubsTag = 0;
	if (*tmp->scan == '*') {
		tmp->scan++;
		ifSubsTag = 1;
	}

	/* Don't expect content for <?tag> or <!tag> */
	content = (*tmp->scan != '?' && *tmp->scan != '!');

	/* Process the tag itself, looking for attributes */
	if (tmp->build)
		*tmp->build++ = '<';
	tmp->len++;
	tagName = tmp->scan;
	tagLen = 0;
	quote = 0;
	ifSubsAttr = 0;
	while (*tmp->scan && *tmp->scan != '>') {
		/* A "/" at the end means no content is expected */
		if (*tmp->scan == '/') {
			if (tmp->scan[1] != '>')
				return "xmlSlash:Misused <tag/> in template";
			content = 0;
			tmp->scan++;
			if (tmp->build)
				*tmp->build++ = '/';
			tmp->len++;
			continue;
		}

		/* "&dollar;" is converted to a "$" */
		if (!strncmp(tmp->scan, "&dollar;", 8)) {
			if (tmp->build)
				*tmp->build++ = '$';
			tmp->len++;
			tmp->scan += 8;
			continue;
		}

		/* Unquoted whitespace marks the start of an attribute.
		 * The first one marks the end of the tag name
		 */
		if (!quote && isspace(*tmp->scan)) {
			/* If this is the first whitespace, then this is the
			 * end of the tag name.  Remember its length.
			 */
			if (tagLen == 0)
				tagLen = (size_t)(tmp->scan - tagName);

			/* If there were no substitutions in previous
			 * *attr="$key", then discard previous attr entirely.
			 */
			if (ifSubsAttr && nsubsBeforeAttr == tmp->nsubs) {
				if (tmp->build)
					tmp->build -= tmp->len - lenBeforeAttr;
				if (tmp->len > tmp->maxLen)
					tmp->maxLen = tmp->len;
				tmp->len = lenBeforeAttr;
			}

			/* Remember where this tag's build begins */
			lenBeforeAttr = tmp->len;
			ifSubsAttr = 0;
			nsubsBeforeAttr = tmp->nsubs;
		}

		/* An asterisk outside of quotes means attr is conditional */
		if (!quote && *tmp->scan == '*') {
			ifSubsAttr = 1;
			tmp->scan++;
			continue;
		}

		/* Watch for quotes. */
		if (*tmp->scan == '"') {
			quote = !quote;
			if (tmp->build)
				*tmp->build++ = '"';
			tmp->len++;
			tmp->scan++;
			continue;
		}

		/* Anything outside of quotes is copied literally.  Inside
		 * quotes, anything other than $key is copied literally.
		 */
		if (!quote || *tmp->scan != '$') {
			if (tmp->build)
				*tmp->build++ = *tmp->scan;
			tmp->len++;
			tmp->scan++;
			continue;
		}

		/* OKAY! We've found a $key to try substituting */

		/* Get a value */
		tmp->scan++;
		text = tmp->scan;
		value = template_key(tmp, 0);
		if (!value && text == tmp->scan)
			return "xmlNoKey:Missing key in template";
		if (!value || value->type ==EDJ_NULL || (value->type == EDJ_STRING && !*value->text))
			continue; /* ignoring missing data */

		/* Copy it to the buffer using entities */
		if (value->type == EDJ_STRING) {
			template_entity_copy(tmp, value->text);
		} else {
			/* Convert it to a string, then add it */
			char *text = edj_serialize(value, NULL);
			template_entity_copy(tmp, text);
			free(text);
		}
		tmp->nsubs++;
	}

	/* If last attribute was conditional and had to substitutions, then
	 * delete it.
	 */
	if (ifSubsAttr && nsubsBeforeAttr == tmp->nsubs) {
		if (tmp->build)
			tmp->build -= tmp->len - lenBeforeAttr;
		if (tmp->len > tmp->maxLen)
			tmp->maxLen = tmp->len;
		tmp->len = lenBeforeAttr;
	}

	/* If there were no attributes at all, then we still need to know the
	 * length of the tag name.
	 */
	if (tagLen == 0)
		tagLen = (size_t)(tmp->scan - tagName);

	/* Include the ">" at the end of the tag */
	if (tmp->build)
		*tmp->build++ = '>';
	tmp->len++;
	tmp->scan++;

	/* Process the content and closing tag, if needed */
	if (content) {
		/* Process content */
		error = template_content(tmp);
		if (error)
			return error;

		/* Expect </tag> after content */
		if (tmp->scan[0] != '<'
		 || tmp->scan[1] != '/'
		 || strncmp(&tmp->scan[2], tagName, tagLen)
		 || tmp->scan[2 + tagLen] != '>')
			return "xmlNest:Mismatched tag in template";
		if (tmp->build) {
			strncpy(tmp->build, tmp->scan, 3 + tagLen);
			tmp->build += 3 + tagLen;
		}
		tmp->len += 3 + tagLen;
		tmp->scan += 3 + tagLen;
	}

	/* If the whole tag was conditional and no substitutions happened,
	 * then delete it.
	 */
	if (tmp->len > tmp->maxLen)
		tmp->maxLen = tmp->len;
	if (ifSubsTag && nsubsBeforeTag == tmp->nsubs) {
		if (tmp->build)
			tmp->build -= tmp->len - lenBeforeTag;
		tmp->len = lenBeforeTag;
	}

	/* Consume any whitespace after the tag */
	while (*tmp->scan && isspace(*tmp->scan))
		tmp->scan++;
	return NULL;
}

/* Process a loop.  This assumes tmp->scan points to the "$[" at the start of
 * the loop, and that that's outside of a tag.  Nested loops are detected and
 * processed recursively. Returns NULL on success, or an error message on
 * failure.
 */
static const char *template_loop(xmlTemplate_t *tmp)
{
	edj_t	*loop, array;
	const char *scan;
	int	nest;
	const char *error;

	/* Look for the value we're looping over */
	tmp->scan += 2; /* to skip "$[" */
	scan = tmp->scan;
	loop = template_key(tmp, 1);
	if (!loop && scan == tmp->scan)
		return "xmlNoKey:Missing key in template";

	/* If no data, then skip the whole loop */
	if (!loop || (loop->type == EDJ_ARRAY && !loop->first)) {
		for (nest = 1; nest > 0; tmp->scan++) {
			if (tmp->scan[0] == '$' && tmp->scan[1] == '[')
				nest++;
			else if (tmp->scan[0] == '$' && tmp->scan[1] == ']')
				nest--;
		}
		tmp->scan += 2; /* for the closing "$]" */
		if (tmp->len > tmp->maxLen)
			tmp->maxLen = tmp->len;
		return NULL;
	}

	/* If not an array, then pretend it's a single-element array.  Note
	 * that the $[key notation doesn't allow you to select a single element
	 * from an array, so loop->next is guaranteed to be NULL.  That
	 * simplifies things.
	 */
	if (loop->type != EDJ_ARRAY) {
		array.type = EDJ_ARRAY;
		array.first = loop;
		loop = &array;
	}

	/* For each element of the array... */
	tmp->sp++;
	scan = tmp->scan;
	for (loop = edj_first(loop); loop; loop = edj_next(loop)) {
		/* Store it in the nesting stack */
		tmp->stack[tmp->sp] = loop;

		/* Process the content of the loop */
		tmp->scan = scan;
		error = template_content(tmp);
	}

	/* We always expect "$]" at the end of the loop */
	if (strncmp(tmp->scan, "$]", 2))
		return "xmlLoopEnd:XML Template loop doesn't end with $]";
	tmp->scan += 2;
	if (tmp->len > tmp->maxLen)
		tmp->maxLen = tmp->len;
	return NULL;
}

/* Copy text outside of tags.  Do $key and $$key substitution.  If <tag> is
 * encountered then call template_tag() to handle it.  If $[key is encountered,
 * then call template_loop() to handle it.
 */
static const char *template_content(xmlTemplate_t *tmp)
{
	const char	*error, *text;
	char	*mustfree;
	edj_t	*value;
	size_t	len;

	/* Copy bytes up to the end of template or "</" */
	while (*tmp->scan && (tmp->scan[0] != '<' || tmp->scan[1] != '/')) {
		if (tmp->scan[0] == '$' && tmp->scan[1] == '[') {
			/* Process the loop */
			error = template_loop(tmp);
			if (error)
				return error;
		} else if (tmp->scan[0] == '$' && tmp->scan[1] == ']') {
			/* End if the loop */
			if (tmp->len > tmp->maxLen)
				tmp->maxLen = tmp->len;
			return NULL;
		} else if (tmp->scan[0] == '$' && tmp->scan[1] == '$') {
			/* Get a value */
			tmp->scan += 2;
			text = tmp->scan;
			value = template_key(tmp, 0);
			if (!value && text == tmp->scan)
				return "xmlNoKey:Missing key in template";
			if (!value || value->type ==EDJ_NULL || (value->type == EDJ_STRING && !*value->text))
				continue; /* ignoring missing data */

			/* Copy it to the buffer literally */
			if (value->type == EDJ_STRING) {
				len = strlen(value->text);
				if (tmp->build) {
					strcpy(tmp->build, value->text);
					tmp->build += len;
				}
				tmp->len += len;
			} else {
				/* Convert it to a string, then add it */
				char *text = edj_serialize(value, NULL);
				len = strlen(text);
				if (tmp->build) {
					strcpy(tmp->build, text);
					tmp->build += len;
				}
				tmp->len += len;
				free(text);
			}
			tmp->nsubs++;
		} else if (tmp->scan[0] == '$') {
			/* Get a value */
			tmp->scan++;
			text = tmp->scan;
			value = template_key(tmp, 0);
			if (!value && text == tmp->scan)
				return "xmlNoKey:Missing key in template";
			if (!value || value->type ==EDJ_NULL || (value->type == EDJ_STRING && !*value->text))
				continue; /* ignoring missing data */

			/* Copy it to the buffer using entities */
			if (value->type == EDJ_STRING) {
				template_entity_copy(tmp, value->text);
			} else {
				/* Convert it to a string, then add it */
				char *text = edj_serialize(value, NULL);
				template_entity_copy(tmp, text);
				free(text);
			}
			tmp->nsubs++;
		} else if (!strncmp(tmp->scan, "&dollar;", 8)) {
			/* Copy a literal dollar sign */
			tmp->scan += 8;
			if (tmp->build)
				*tmp->build++ = '$';
			tmp->len++;
		} else if (*tmp->scan == '<') {
			/* Copy the tag content */
			error = template_tag(tmp);
			if (error)
				return error;
		}
	}
	if (tmp->len > tmp->maxLen)
		tmp->maxLen = tmp->len;
	return NULL;
}

/* This file converts an edj_t object to an XML string.  Returns the size of
 * the buffer needed to generate the string, which may be somewhat larger than
 * the string itself.  THIS IS DIFFERENT FROM xml_unparse() which returns the
 * string length.  You may pass NULL for the "buf" to return the required size
 * without generating the string.  The expected sequence is:
 *  1) Call xml_template(NULL, ...) to find the required buffer size.
 *  2) Detect errors, by examining *referror.
 *  3) Allocate the buffer.
 *  4) Call xml_template(buf, ...) to actually generate the string.
 *  5) Use edj_string(buf, -1) to make a "right size" copy of the string.
 *  6) Free the buffer.
 */
static size_t xml_template(char *buf, edj_t *data, const char *template, const char **referror)
{
	xmlTemplate_t tmp;

	/* Set up a template state */
	tmp.stack[0] = data;
	tmp.sp = 0;
	tmp.scan = template;
	tmp.build = buf;
	tmp.len = 0;
	tmp.nsubs = 0;

	/* Invoke the recursive generator.  Watch for errors. */
	*referror = template_content(&tmp);

	/* Terminate the string */
	if (buf)
		buf[tmp.len] = '\0';

	/* Return the required buffer size */
	return tmp.maxLen + 1; /* "+ 1" for the terminating '\0' */
}

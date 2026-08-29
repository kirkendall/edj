#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#undef _XOPEN_SOURCE
#undef __USE_XOPEN
#include <wchar.h>
#include <edj.h>

/* This plugin adds support for XML parsing and generation */

/* Include the other source files in this one.  The reason we don't compile
 * them separately is, we want everything to be "static" scope except for
 * the pluginxml() function.
 */
#include "parse.c"
#include "unparse.c"
#include "template.c"

/*----------------------------------------------------------------------------*/

/* These are the default settings */
static char *SETTINGS = "{"
	"\"attributeSuffix\":\"_\","
	"\"parseNumber\":true,"
	"\"strictPair\":true,"
	"\"generateCRLF\":true,"
	"\"empty-list\":[\"string\",\"object\",\"array\"],"
	"\"empty\":\"string\","
	"\"entity\":{"
	    "\"quot\":\"\\\"\","
	    "\"apos\":\"'\","
	    "\"amp\":\"&\","
	    "\"lt\":\"<\","
	    "\"gt\":\">\""
	"}"
"}";

/*----------------------------------------------------------------------------*/

/* Converts an edj_t object to XML.  Basically the inverse of the parser. */
static edj_t *jfn_toXML(edj_t *args, void *agdata)
{
	size_t	len;
	edj_t	*result;
	const char *template = NULL;
	const char *error;
	char	*buf;

	/* Only works on objects, unless you also pass a template */
	if (args->first->type != EDJ_OBJECT && !args->first->next)
		return edj_error_null(NULL, "toXmlObj:The %s() function only works on objects", "toXML");
	if (args->first->next) {
		if (args->first->next->type != EDJ_STRING)
			return edj_error_null("toXmlTmplate:%s() template must be a string", "toXML");
		template = args->first->next->text;
	}

	/* Predict the length, allocate a string, and generate it */
	if (template) {
		/* With a template */
		len = xml_template(NULL, args->first, template, &error);
		if (error)
			return edj_error_null("%s", error);
		char buf[len];
		(void)xml_template(buf, args->first, template, &error);
		result = edj_string(buf, -1);
	} else {
		/* Without a template */
		len = xml_unparse(NULL, args->first);
		result = edj_string("", len);
		(void)xml_unparse(result->text, args->first);
	}

	return result;
}

/* Generates an XML document from a template. */
static edj_t *jfn_toTemplateXML(edj_t *args, void *agdata)
{
	return NULL;
}

/*----------------------------------------------------------------------------*/

static edjcmdname_t *jcn_xmlEntity;

/* Parse an xmlEntity command */
static edjcmd_t *xmlEntity_parse(edjsrc_t *src, edjcmdout_t **referr)
{
	edjsrc_t	start;
	char		*key = NULL;
	edjcalc_t	*calc = NULL;
	const char	*err;
	edjcmd_t	*cmd;

	/* xmlEntity with no arguments is legitimate.  It will dump the table */
	start = *src;
	edj_cmd_parse_whitespace(src);
	if (!*src->str || *src->str == ';' || *src->str == '}') {
		return edj_cmd(&start, jcn_xmlEntity);
	}

	/* Parse the key.  If no key, that's an error */
	key = edj_cmd_parse_key(src, 0);
	if (!key)
		goto Error;

	/* Parse the "=".  If no "=", that's an error. */
	edj_cmd_parse_whitespace(src);
	if (*src->str != '=')
		goto Error;
	src->str++;
	edj_cmd_parse_whitespace(src);

	/* Parse the value, as an expression */
	calc = edj_calc_parse(src->str, &src->str, &err, FALSE);
	if (!calc || err || (*src->str && !strchr(";},", *src->str)))
		goto Error;

	/* Construct the command */
	cmd = edj_cmd(&start, jcn_xmlEntity);
	cmd->key = key;
	cmd->calc = calc;
	return cmd;

Error:
	if (key)
		free(key);
	if (calc)
		edj_calc_free(calc);
	*referr = edj_cmd_error(start.str, "xmlEntity:The %s command expects an entity=value argument");
	return NULL;
}

/* Run an xmlEntity command */
static edjcmdout_t *xmlEntity_run(edjcmd_t *cmd, edjcontext_t **refcontext)
{
	edjcmd_t *dump;
	edjcmdout_t *result;
	edj_t	*value, *entity;

	/* If no key, then dump the entity list */
	if (!cmd->key) {
		dump = edj_cmd_parse_string("config.plugin.xml.entity.keysValues().orderBy('key') # {entity:'&'+key+';',codepoint:value.length==1?('U+'+(value.charCodeAt()).hex(5)),text:value}");
		result = edj_cmd_run(dump, refcontext);
		edj_cmd_free(dump);
		return result;
	}

	/* Evaluate the value.  Watch for errors. */
	value = edj_calc(cmd->calc, *refcontext, NULL);
	if (edj_is_error(value)) {
		result = edj_cmd_error(cmd->where, "%s", value->text);
		edj_free(value);
		return result;
	}

	/* If the value is a number, convert it to a single character string */
	if (value->type == EDJ_NUMBER) {
		wchar_t	number = (wchar_t)edj_int(value);
		int	in;
		edj_free(value);
		value = edj_string("", MB_CUR_MAX);
		in = wctomb(value->text, number);
		if (in > 0)
			value->text[in] = '\0';
	}

	/* If the value still isn't a string, that's an error */
	if (value->type != EDJ_STRING)
		return edj_cmd_error(cmd->where, "xmlEntityType:The value of an entity should be either a string or a number");

	/* Add/update the entity list */
	entity = edj_by_expr(edj_config, "plugin.xml.entity", NULL, NULL, NULL);
	edj_append(entity, edj_key(cmd->key, value));

	/* Success! */
	return NULL;
}


/*----------------------------------------------------------------------------*/

/* This is the init function.  It registers all of the options, functions and
 * parsers.
 */
char *pluginxml()
{
	edj_t	*section, *settings;

	/* Register the settings */
	settings = edj_parse_string(SETTINGS);
	section = edj_by_key(edj_config, "plugin");
	edj_append(section, edj_key("xml", settings));

	/* Register the functions */
	edj_calc_function_hook("toXML", "document:object", "string", jfn_toXML);
	edj_calc_function_hook("toTemplateXML", "data:any, template:string", "string", jfn_toTemplateXML);

	/* Register the commands */
	jcn_xmlEntity = edj_cmd_hook(NULL, "xmlEntity", xmlEntity_parse, xmlEntity_run);

	/* Register the XML data parser */
	edj_parse_hook("xml", "xml", ".xml", "application/xml", xml_test, xml_parse, NULL);

	/* Success */
	return NULL;
}

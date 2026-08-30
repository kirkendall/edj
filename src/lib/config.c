#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <assert.h>
#include <edj.h>
#include "version.h"

static const char *defaultconfig = "{"
	"\"interactive\":{"
		"\"tab\":2,"
		"\"oneline\":70,"
		"\"digits\":12,"
		"\"table\":\"grid\","
		"\"table-list\":[\"json\",\"grid\",\"sh\"],"
		"\"string\":false,"
		"\"pretty\":true,"
		"\"elem\":false,"
		"\"quote\":false,"
		"\"errors\":true,"
		"\"ascii\":false,"
		"\"color\":true,"
		"\"quick\":false,"
		"\"graphic\":true,"
		"\"prefix\":\"\","
		"\"null\":\"\","
	"},"
	"\"batch\":{"
		"\"tab\":2,"
		"\"oneline\":0,"
		"\"digits\":12,"
		"\"table\":\"json\","
		"\"table-list\":[\"json\",\"grid\",\"sh\"],"
		"\"string\":false,"
		"\"pretty\":false,"
		"\"elem\":false,"
		"\"quote\":false,"
		"\"errors\":true,"
		"\"ascii\":false,"
		"\"color\":false,"
		"\"quick\":false,"
		"\"graphic\":false,"
		"\"prefix\":\"\","
		"\"null\":\"\","
	"},"
	"\"diffstyle\":13," /* EDJ_DIFF_BESIDE|EDJ_DIFF_VALUE|EDJ_DIFF_EDIT */
	"\"common\":{"
		"\"in\":\"Y\","
		"\"out\":\"\","
		"\"style\":4,"
		"\"hashTable\":10"
	"},"
	"\"emptyobject\":\"object\","
	"\"defersize\":10000000,"
	"\"deferexplain\":100,"
	"\"firsthint\":true,"
	"\"styles\": ["
		"{"
			"\"style\":\"normal\","
			"\"bold\":false,"
			"\"dim\":false,"
			"\"italic\":false,"
			"\"underlined\":false,"
			"\"blinking\":false,"
			"\"boxed\":false,"
			"\"strike\":false"
			"\"fg\":\"normal\","
			"\"fg-list\":[\"normal\",\"black\",\"red\",\"green\",\"yellow\",\"blue\",\"magenta\",\"cyan\",\"white\"],"
			"\"bg\":\"on normal\","
			"\"bg-list\":[\"on normal\",\"on black\",\"on red\",\"on green\",\"on yellow\",\"on blue\",\"on magenta\",\"on cyan\",\"on white\"],"
			"\"stderr\":false"
		"},{"
			"\"style\":\"prompt\","
			"\"bold\":true,"
			"\"dim\":false,"
			"\"italic\":false,"
			"\"underlined\":false,"
			"\"blinking\":false,"
			"\"boxed\":false,"
			"\"strike\":false"
			"\"fg\":\"cyan\","
			"\"fg-list\":[\"normal\",\"black\",\"red\",\"green\",\"yellow\",\"blue\",\"magenta\",\"cyan\",\"white\"],"
			"\"bg\":\"on normal\","
			"\"bg-list\":[\"on normal\",\"on black\",\"on red\",\"on green\",\"on yellow\",\"on blue\",\"on magenta\",\"on cyan\",\"on white\"],"
			"\"stderr\":false"
		"},{"
			"\"style\":\"result\","
			"\"bold\":false,"
			"\"dim\":false,"
			"\"italic\":false,"
			"\"underlined\":false,"
			"\"blinking\":false,"
			"\"boxed\":false,"
			"\"strike\":false"
			"\"fg\":\"normal\","
			"\"fg-list\":[\"normal\",\"black\",\"red\",\"green\",\"yellow\",\"blue\",\"magenta\",\"cyan\",\"white\"],"
			"\"bg\":\"on normal\","
			"\"bg-list\":[\"on normal\",\"on black\",\"on red\",\"on green\",\"on yellow\",\"on blue\",\"on magenta\",\"on cyan\",\"on white\"],"
			"\"stderr\":false"
		"},{"
			"\"style\":\"error\","
			"\"bold\":true,"
			"\"dim\":false,"
			"\"italic\":false,"
			"\"underlined\":false,"
			"\"blinking\":false,"
			"\"boxed\":false,"
			"\"strike\":false"
			"\"fg\":\"red\","
			"\"fg-list\":[\"normal\",\"black\",\"red\",\"green\",\"yellow\",\"blue\",\"magenta\",\"cyan\",\"white\"],"
			"\"bg\":\"on normal\","
			"\"bg-list\":[\"on normal\",\"on black\",\"on red\",\"on green\",\"on yellow\",\"on blue\",\"on magenta\",\"on cyan\",\"on white\"],"
			"\"stderr\":true"
		"},{"
			"\"style\":\"debug\","
			"\"bold\":true,"
			"\"dim\":false,"
			"\"italic\":false,"
			"\"underlined\":false,"
			"\"blinking\":false,"
			"\"boxed\":false,"
			"\"strike\":false"
			"\"fg\":\"blue\","
			"\"fg-list\":[\"normal\",\"black\",\"red\",\"green\",\"yellow\",\"blue\",\"magenta\",\"cyan\",\"white\"],"
			"\"bg\":\"on normal\","
			"\"bg-list\":[\"on normal\",\"on black\",\"on red\",\"on green\",\"on yellow\",\"on blue\",\"on magenta\",\"on cyan\",\"on white\"],"
			"\"stderr\":true"
		"},{"
			"\"style\":\"gridhead\","
			"\"bold\":true,"
			"\"dim\":false,"
			"\"italic\":false,"
			"\"underlined\":true,"
			"\"blinking\":false,"
			"\"boxed\":false,"
			"\"strike\":false"
			"\"fg\":\"blue\","
			"\"fg-list\":[\"normal\",\"black\",\"red\",\"green\",\"yellow\",\"blue\",\"magenta\",\"cyan\",\"white\"],"
			"\"bg\":\"on normal\","
			"\"bg-list\":[\"on normal\",\"on black\",\"on red\",\"on green\",\"on yellow\",\"on blue\",\"on magenta\",\"on cyan\",\"on white\"],"
			"\"stderr\":false"
		"},{"
			"\"style\":\"gridline\","
			"\"bold\":true,"
			"\"dim\":false,"
			"\"italic\":false,"
			"\"underlined\":false,"
			"\"blinking\":false,"
			"\"boxed\":false,"
			"\"strike\":false"
			"\"fg\":\"blue\","
			"\"fg-list\":[\"normal\",\"black\",\"red\",\"green\",\"yellow\",\"blue\",\"magenta\",\"cyan\",\"white\"],"
			"\"bg\":\"on normal\","
			"\"bg-list\":[\"on normal\",\"on black\",\"on red\",\"on green\",\"on yellow\",\"on blue\",\"on magenta\",\"on cyan\",\"on white\"],"
			"\"stderr\":false"
		"},{"
			"\"style\":\"gridcell\","
			"\"bold\":false,"
			"\"dim\":false,"
			"\"italic\":false,"
			"\"underlined\":false,"
			"\"blinking\":false,"
			"\"boxed\":false,"
			"\"strike\":false"
			"\"fg\":\"normal\","
			"\"fg-list\":[\"normal\",\"black\",\"red\",\"green\",\"yellow\",\"blue\",\"magenta\",\"cyan\",\"white\"],"
			"\"bg\":\"on normal\","
			"\"bg-list\":[\"on normal\",\"on black\",\"on red\",\"on green\",\"on yellow\",\"on blue\",\"on magenta\",\"on cyan\",\"on white\"],"
			"\"stderr\":false"
		"},"
	"],"
	"\"plugin\":{}"
"}";


/* This stores a pointer to the config data */
edj_t *edj_config;

/* This is a combination of all system data.  It is initialized by
 * edj_config_load(), though other code may add to it.
 */
edj_t *edj_system;

static void merge(edj_t *old, edj_t *newload);

/* Merge table "newload" into table "old".  Use the "key" to find the
 * corresponding rows.  We assume both are known to be tables, and that
 * "old" is undeferred.
 */
static void merge_table(edj_t *old, edj_t *newload, char *key)
{
	edj_t *newrow, *oldrow, *tmp;
	char *value;

	/* For each row from newload... */
	for (newrow = edj_first(newload); newrow; newrow = edj_next(newrow)) {
		/* Fetch the key value.  Skip if there is none */
		tmp = edj_by_key(newrow, key);
		if (!tmp || tmp->type != EDJ_STRING)
			continue;
		value = tmp->text;

		/* Look for a corresponding row in the old table */
		for (oldrow = edj_first(old); oldrow; oldrow = edj_next(oldrow)) {
			tmp = edj_by_key(oldrow, key);
			if (!tmp || tmp->type != EDJ_STRING)
				continue;
			if (!strcmp(value, tmp->text))
				break;;
		}

		/* If we found a corresponding row, update it.  Otherwise,
		 * append a copy of the "newrow" row to the "old" table.
		 */
		if (oldrow)
			merge(oldrow, newrow);
		else
			edj_append(old, edj_copy(newload));
	}
}

/* Merge new settings into old settings. */
static void merge(edj_t *old, edj_t *newload)
{
	edj_t *newkey, *oldmem;

	/* Only works on objects */
	if (old->type != EDJ_OBJECT || newload->type != EDJ_OBJECT)
		return;

	/* For each new member */
	for (newkey = newload->first; newkey; newkey = newkey->next) { /* object */
		/* Look for a corresponding old member */
		oldmem = edj_by_key(old, newkey->text);

		/* If no corresponding old member, then add a copy of new */
		if (!oldmem) {
			edj_append(old, edj_key(newkey->text, edj_copy(newkey->first)));
			continue;
		}

		/* If both are objects, merge recursively */
		if (oldmem->type == EDJ_OBJECT && newkey->first->type == EDJ_OBJECT) {
			merge(oldmem, newkey->first);
			continue;
		}

		/* If both are tables, merge the tables. */
		if (edj_is_table(oldmem) && edj_is_table(newkey->first)) {
			merge_table(oldmem, newkey->first, oldmem->first->first->text);
			continue;
		}

		/* If both are some other type, then replace the value */
		if (oldmem->type == newkey->first->type) {
			edj_append(old, edj_key(newkey->text, edj_copy(newkey->first)));
			continue;
		}

		/* Otherwise ignore it.  This could happen if edj's
		 * option format got redefined, so the new settings 
		 */
	}
}

/* Add a directory name to the path, if the directory exists */
static void addpath(edj_t *path, const char *dirname)
{
	if (access(dirname, F_OK) == 0)
		edj_append(path, edj_string(dirname, -1));
}

/* Return an array of directory names to look in for files related to edj
 * -- plugins and documentation mostly.
 */
static edj_t *configpath(const char *envvar)
{
	const char *env;
	edj_t *path, *entry;
	int	isjxpath;
	size_t	len;

	/* If no envvar then just fake it completely */
	if (!envvar) {
		path = edj_array();
		edj_append(path, edj_string("~/.config/edj", -1));
		addpath(path, "/usr/local/lib64/edj");
		addpath(path, "/usr/local/lib/edj");
		addpath(path, "/usr/lib64/edj");
		addpath(path, "/usr/lib/edj");
		addpath(path, "/lib64/edj");
		addpath(path, "/lib/edj");
		addpath(path, "/usr/local/share/edj");
		addpath(path, "/usr/share/edj");
		return path;
	}

	/* If an envvar was specified but it isn't set, return NULL */
	env = getenv(envvar);
	if (!env)
		return NULL;

	/* Distinguish between $EDJPATH and other paths */
	isjxpath = !strcmp(envvar, "EDJPATH");

	/* Start building an array of entries.  If not $EDJPATH then
	 * put the config directory first.
	 */
	path = edj_array();
	if (!isjxpath)
		edj_append(path, edj_string("~/.config/edj", -1));

	/* For each entry in the path... */
	while (*env) {
		/* Find the end of this entry */
		for (len = 0; env[len] && env[len] != EDJ_PATH_DELIM; len++) {
		}

		/* Convert it to a string.  If not from $EDJPATH then
		 * add "/edj" to the string.
		 */
		if (len == 0)
			entry = edj_string(".", 1);
		else if (isjxpath) {
			entry = edj_string(env, len);
		} else {
			entry = edj_string(env, len + 3);
			strcat(entry->text, "/edj");
		}

		/* Add it to the path */
		edj_append(path, entry);

		/* Move to the next entry */
		env += len;
		if (*env == EDJ_PATH_DELIM)
			env++;
	}

	/* If not $EDJPATH then add shared directories */
	if (!isjxpath) {
		addpath(path, "/usr/local/share/edj");
		addpath(path, "/usr/share/edj");
	}
	return path;
}

/* Load the configuration data */
void edj_config_load(const char *name)
{
	char	*pathname;
	edj_t	*conf, *value;

	/* Load the default config */
	edj_config = edj_parse_string(defaultconfig);

	/* If edj_system isn't set up yet, then set it up now */
	if (!edj_system) {
		edj_system = edj_object();

		/* We also want to add the path for plugins and documentation.
		 * This is from $EDJPATH, but if $EDJPATH isn't set
		 * then we want to derive it from $LD_LIBRARY_PATH.  And if
		 * $LD_LIBRARY_PATH isn't set then we simulate that too.
		 */
		value = configpath("EDJPATH");
		if (!value)
			value = configpath("LD_LIBRARY_PATH");
		if (!value)
			value = configpath(NULL);
		edj_append(edj_system, edj_key("path", value));

		edj_append(edj_system, edj_key("config", edj_config));
		pathname = edj_file_path(NULL, NULL, NULL);
		edj_append(edj_system, edj_key("configdir", edj_string(pathname, -1)));
		free(pathname);

		/* Add an empty list of plugins.  As plugins are loaded,
		 * this will become populated.
		 */
		if (!edj_plugins)
			edj_plugins = edj_array();
		edj_append(edj_system, edj_key("plugins", edj_plugins));

		/* Add a table of parsers.  Initially it'll contain only the
		 * built-in JSON parser, but as plugins register their parsers
		 * via edj_parse_hook(), they'll be added here too.
		 */
		conf = edj_array();
		value = edj_object();
		edj_append(value, edj_key("name", edj_string("json", -1)));
		edj_append(value, edj_key("plugin", edj_null()));
		edj_append(value, edj_key("suffix", edj_string(".json", -1)));
		edj_append(value, edj_key("mimetype", edj_string("application/json", -1)));
		edj_append(value, edj_key("writable", edj_boolean(1)));
		edj_append(conf, value);
		value = edj_object();
		edj_append(value, edj_key("name", edj_string("blob", -1)));
		edj_append(value, edj_key("plugin", edj_null()));
		edj_append(value, edj_key("suffix", edj_null()));
		edj_append(value, edj_key("mimetype", edj_string("application/octet-stream", -1)));
		edj_append(value, edj_key("writable", edj_boolean(1)));
		edj_append(conf, value);
		edj_append(edj_system, edj_key("parsers", conf));

		/* Add empty JSON and Math objects, for JS compatibility. */
		edj_append(edj_system, edj_key("JSON", edj_object()));
		edj_append(edj_system, edj_key("Math", edj_object()));

		/* Add a "Blob" object with constants selecting how to handle
		 * binary data.
		 */
		value = edj_object();
		edj_append(value, edj_key("any", edj_from_int(EDJ_BLOB_ANY)));
		edj_append(value, edj_key("string", edj_from_int(EDJ_BLOB_STRING)));
		edj_append(value, edj_key("utf8", edj_from_int(EDJ_BLOB_UTF8)));
		edj_append(value, edj_key("latin1", edj_from_int(EDJ_BLOB_LATIN1)));
		edj_append(value, edj_key("bytes", edj_from_int(EDJ_BLOB_BYTES)));
		edj_append(edj_system, edj_key("Blob", value));

		/* Add a "Diff" object with constants for selecting the format */
		value = edj_object();
		edj_append(value, edj_key("value", edj_from_int(EDJ_DIFF_VALUE)));
		edj_append(value, edj_key("span", edj_from_int(EDJ_DIFF_SPAN)));
		edj_append(value, edj_key("beside", edj_from_int(EDJ_DIFF_BESIDE)));
		edj_append(value, edj_key("edit", edj_from_int(EDJ_DIFF_EDIT)));
		edj_append(value, edj_key("context", edj_from_int(EDJ_DIFF_CONTEXT)));
		edj_append(value, edj_key("bits", edj_parse_string("[\"value\",\"span\",\"beside\",\"edit\",\"context\"]")));
		edj_append(edj_system, edj_key("Diff", value));

		/* Add a "Common" object with constants for selecting the format */
		value = edj_object();
		edj_append(value, edj_key("index", edj_from_int(EDJ_COMMON_INDEX)));
		edj_append(value, edj_key("count", edj_from_int(EDJ_COMMON_COUNT)));
		edj_append(value, edj_key("check", edj_from_int(EDJ_COMMON_CHECK)));
		edj_append(value, edj_key("in", edj_from_int(EDJ_COMMON_IN)));
		edj_append(value, edj_key("all", edj_from_int(EDJ_COMMON_ALL)));
		edj_append(value, edj_key("inOnly", edj_from_int(EDJ_COMMON_IN_ONLY)));
		edj_append(value, edj_key("outOnly", edj_from_int(EDJ_COMMON_OUT_ONLY)));
		edj_append(value, edj_key("none", edj_from_int(EDJ_COMMON_NONE)));
		edj_append(value, edj_key("mix", edj_from_int(EDJ_COMMON_MIX)));
		edj_append(value, edj_key("stats", edj_from_int(EDJ_COMMON_STATS)));
		edj_append(value, edj_key("noSort", edj_from_int(EDJ_COMMON_NOSORT)));
		edj_append(value, edj_key("force", edj_from_int(EDJ_COMMON_FORCE)));
		edj_append(value, edj_key("recheck", edj_from_int(EDJ_COMMON_RECHECK)));
		edj_append(value, edj_key("bits", edj_parse_string("[[\"index\",\"count\",\"check\",\"in\"],\"all\",[\"inOnly\",\"outOnly\",\"none\",\"mix\"],\"stats\",\"noSort\",\"force\",\"recheck\"]")));
		edj_append(edj_system, edj_key("Common", value));

		/* Add members to edj_system, describing the environment */
		edj_append(edj_system, edj_key("runmode", edj_string("interactive", -1)));
		edj_append(edj_system, edj_key("update", edj_boolean(0)));
		edj_append(edj_system, edj_key("version", edj_number(EDJ_VERSION, -1)));
		edj_append(edj_system, edj_key("copyright", edj_string(EDJ_COPYRIGHT, -1)));

	}

	/* Look for the config file */
	pathname = edj_file_path(NULL, name, ".json");
	if (!pathname)
		return;

	/* Parse it */
	conf = edj_parse_file(pathname);
	free(pathname);
	if (!conf)
		return;
	if (conf->type != EDJ_OBJECT) {
		edj_free(conf);
		return;
	}

	/* Merge its settings into the default config */
	merge(edj_config, conf);

	/* Free the data from the file */
	edj_free(conf);
}

/* Select whether this part of edj_config gets saved.  It should save
 * everything except "batch", members that have names ending with "-list",
 * and a few others.
 */
static int notlist(edj_t *mem)
{
	/* Non-members are always kept */
	if (mem->type != EDJ_KEY)
		return 1;

	/* Members named "batch" or "*-list" are skipped */
	if (!strcmp(mem->text, "batch"))
		return 0;
	if (!strcmp(mem->text, "pluginloaded"))
		return 0;
	if (strlen(mem->text) >= 5
	 && !strcmp(mem->text + strlen(mem->text) - 5, "-list"))
		return 0;

	/* Other members are kept */
	return 1;
}

/* Save the config data */
void edj_config_save(const char *name)
{
	char	*pathname;
	edj_t	*copy;
	FILE	*fp;

	/* We generally want to save options in the first writable directory
	 * in the EDJPATH, even if the "config.json" file doesn't exist
	 * there yet.
	 */
	pathname = edj_file_path(NULL, NULL, NULL);
	if (pathname) {
		char *tmp = malloc(strlen(pathname) + strlen(name) + 6);
		strcpy(tmp, pathname);
		strcat(tmp, name);
		strcat(tmp, ".json");
		free(pathname);
		pathname = tmp;
	} else {
		pathname = edj_file_path(NULL, name, ".json");
		if (!pathname)
			return; /* no place to save it */
	}

	/* We want to save everything EXCEPT members with names ending with
	 * "-list".  Make a copy of the config with "-list" members removed.
	 */
	copy = edj_copy_filter(edj_config, notlist);

	/* Write it to the file */
	fp = edj_file_update(pathname);
	if (fp) {
		edjformat_t fmt = edj_format_default;
		fmt.string = fmt.elem = fmt.sh = fmt.ascii = fmt.color = 0;
		fmt.fp = fp;
		edj_print(copy, &fmt);
		fclose(fp);
	}
	edj_free(copy);
	free(pathname);
}

/* Look up the section in config.styles for a given style.  There are two ways
 * this is used.
 *
 * Plugins can call it with NULL as the second argument, during their config
 * initialization.  Used this way, if the requested style isn't found then it
 * will be added and returned.  It will initially be a copy of "normal" but
 * the plugin can directly modify member values as appropriate.  Note that
 * settings from -s/-S flags or the config file are loaded later.
 *
 * If called with a reference to a (edj_t*) as the second argument, then it'll
 * store the "config.styles" pointer there if the style is found.  If the style
 * is not found, the function will return NULL.  The config code itself uses
 * this method to avoid creating bogus/misspelled names.
 */
edj_t *edj_config_style(const char *name, edj_t **refstyles)
{
	edj_t *styles, *scan, *style;

	/* Find "config.styles" */
	styles = edj_by_key(edj_config, "styles");
	assert(styles != NULL && styles->type == EDJ_ARRAY);

	/* Scan for the requested style.  We don't use edj_by_key_value()
	 * for this because we hope to avoid converting the name from (char *)
	 * to (edj_t *).
	 */
	for (scan = edj_first(styles); scan; scan = edj_next(scan)) {
		assert(scan->type == EDJ_OBJECT);
		style = edj_by_key(scan, "style");
		assert(style && style->type == EDJ_STRING);
		if (!edj_mbs_casecmp(style->text, name)) {
			/* Found!  Return it.  Note that config.styles is
			 * not a deferred array, so we don't need to call
			 * edj_break()
			 */
			if (refstyles)
				*refstyles = styles;
			return scan;
		}
	}

	/* Not found.  If called from the config code below, (i.e. if refstyles
	 * is not NULL) then return NULL.
	 */
	if (refstyles)
		return NULL;

	/* Okay, we were called from a plugin's initialization code, so we need
	 * to add it.  Start with a copy of "normal" (the first element of
	 * config.styles) and stuff the new name into the copy.
	 */
	scan = edj_copy(styles->first);
	edj_append(scan, edj_key("style", edj_string(name, -1)));
	edj_append(styles, scan);
	return scan;
}

/* Get an option from a given section of the settings.  If you pass NULL
 * for the section name, then it'll look in the top level of the config data,
 * or in the "interactive" or "batch" subsection as appropriate.  Returns
 * the edj_t of the found value, or NULL if not found.
 */
edj_t *edj_config_get(const char *section, const char *key)
{
	edj_t *jsect;

	/* Locate the section.  If section is NULL, use the format settings */
	if (section) {
		jsect = edj_by_expr(edj_config, section, NULL, NULL, NULL); /* undeferred */
		if (!jsect)
			return NULL;
	} else {
		jsect = edj_by_key(edj_config, edj_text_by_key(edj_system, "runmode"));
		if (edj_by_key(jsect, key) == NULL)
			jsect = edj_config;
	}

	/* Look for the requested key in that section */
	return edj_by_key(jsect, key);
}

/* Set an option in a given section of the settings.  If you pass NULL for
 * the section name, then it'll put it in the top level of the config data,
 * or in the "interactive" or "batch" subsection as appropriate.  If you
 * pass a non-NULL section name and that name doesn't exist, then it'll be
 * added.  THIS FUNCTION DOESN'T VERIFY THAT NAMES OR DATA TYPES ARE CORRECT.
 * Also, the "value" is incorporated into the edj_config tree, so it can't be
 * used anywhere else in order to avoid memory issues.  Maybe use edj_copy().
 */
void edj_config_set(const char *section, const char *key, edj_t *value)
{
	edj_t	*jsect;

	/* Locate the section.  If section is NULL, use all of edj_config,
	 * or the "interactive" or "batch" subsection, as appropriate.
	 * If a section name is given but the requested section doesn't exist,
	 * then create an empty object to hold that section.
	 */
	if (section) {
		/* Use the named section */
		jsect = edj_by_expr(edj_config, section, NULL, NULL, NULL); /* undeferred */
		if (!jsect) {
			if (!strncmp(section, "plugin.", 7)) {
				jsect = edj_by_key(edj_config, "plugin");
				edj_append(jsect, edj_key(section + 7, edj_object()));
				jsect = edj_by_key(jsect, section + 7);
			} else {
				jsect = edj_object();
				edj_append(edj_config, edj_key(section, jsect));
			}
		}
	} else {
		/* Use the top level of the config... or, in the "interactive"
		 * or "batch" subsection, as appropriate, if the key already
		 * exists there.
		 */
		jsect = edj_by_key(edj_config, edj_text_by_key(edj_system, "runmode"));
		if (!edj_by_key(jsect, key))
			jsect = edj_config;
	}

	/* Append the value to the section */
	edj_append(jsect, edj_key(key, value));
}

/* Parse an option string, and merge its settings into a given section.
 *
 * The "settings" string is essentially a comma-delimited list of name=value
 * settings, except that if you omit the "=value" then it tries to get clever.
 * If the name is a boolean, it'll set it to true; if name as a "no" prefix
 * with a boolean setting's suffix, then it'll set it to false.  If the name
 * appears in a "option-list" member with a corresponding string member
 * named "option", then it'll set the option to the name (e.g., if "fg-list"
 * is an array containing "red", then just saying "red" means "fg=red".
 *
 * Also, "name=value" where name is an object, then it'll be parsed as a
 * space-delimited list of words for setting boolean and "-list" members
 * within that object. (E.g., "format=sh noquote" will set format.table=sh
 * and format.quote=false.)
 *
 * In all cases, "name" may actually include "." to go into nested objects.
 *
 * "refend" is NULL normally.  When called recursively to parse settings for
 * an object (subsection), it stores the end of the parsed value there.  This
 * also has the side-effect if disabling the use of commas as delimiters,
 * so all of the object's settings must be space-delimited.
 *
 * Returns NULL on success, or an edj_t containing an error message if error.
 */
edj_t *edj_config_parse(edj_t *config, const char *settings, const char **refend)
{
	size_t	namelen, namealloc = 0, len;
	int	negate;
	char	*name = NULL;
	const char *value;
	edj_t	*thisconfig, *found, *list, *jvalue;

	/* Passing NULL for config is equivalent to passing edj_config */
	if (!config)
		config = edj_config;

	/* Until we hit the end... */
	while (*settings && (!refend || *settings != ',')) {
		/* Skip whitespace or commas between settings */
		if (isspace(*settings) || *settings == ',') {
			settings++;
			continue;
		}

		/* Note whether there's a "-" */
		negate = 0;
		if (*settings == '-') {
			settings++;
			negate = 1;
		}

		/* Count characters in the name */
		for (namelen = 0; isalnum(settings[namelen]); namelen++) {
		}
		if (namelen == 0) {
			free(name);
			if (refend)
				*refend = settings;
			return edj_error_null(0, "Malformed option \"%s\"", settings);
		}

		/* Make a copy of the name */
		if (namealloc < namelen + 1) {
			namealloc = (namelen | 0x1f) + 1;
			name = realloc(name, namealloc);
		}
		strncpy(name, settings, namelen);
		name[namelen] = 0;

		/* Look for an existing member with that key */
		thisconfig = config;
		found = edj_by_key(config, name);

		/* If not found in "config" then try the whole config, or
		 * in the "styles" array.
		 */
		if (!found) {
			thisconfig = edj_config;
			found = edj_by_key(edj_config, name);
		}
		if (!found) {
			found = edj_config_style(name, &thisconfig);
		}

		/* Followed by "=" ?  Or, equivalently, '.' to make deeply
		 * nested objects more JavaScript-like?
		 */
		if (settings[namelen] == '=' || settings[namelen] == '.') {
			/* If the member doesn't exist, that's an error */
			if (!found) {
				found = edj_error_null(0, "Unknown option \"%s\"", name);
				free(name);
				if (refend)
					*refend = settings;
				return found;
			}

			/* Value is after "=" or '.' */
			value = settings + namelen + 1;

			/* Processing of the value depends on the type */
			switch (found->type) {
			case EDJ_OBJECT:
				{
					/* Recursively parse the object's settings */
					const char *end;
					jvalue = edj_config_parse(found, settings + namelen + 1, &end);
					if (jvalue) {
						/* Oops, we found an error */
						free(name);
						if (refend)
							*refend = settings;
						return jvalue;
					}
					settings = end;
				}
				continue;

			case EDJ_BOOLEAN:
				/* should be true or false */
				if (!strncasecmp(value, "true", 4) && !isalnum(value[4])) {
					strcpy(found->text, "true");
					settings = value + 4;
				} else if (!strncasecmp(value, "false", 5) && !isalnum(value[5])) {
					strcpy(found->text, "false");
					settings = value + 5;
				} else {
					free(name);
					if (refend)
						*refend = settings;
					return edj_error_null(0, "Invalid boolean option value \"%s\"", value);
				}
				continue;

			case EDJ_NUMBER:
				{
					char	*lend, *dend;
					long	l;
					double	d;

					/* May be int or float */
					d = strtod(value, &dend);
					l = strtol(value, &lend, 0);
					if (lend == value) {
						free(name);
						if (refend)
							*refend = settings;
						return edj_error_null(0, "Invalid number value \"%s\"", value);
					}
					if (lend < dend) {
						/* It's floating-point */
						found->text[0] = 0;
						found->text[1] = 'd';
						EDJ_DOUBLE(found) = d;
						settings = dend;
					} else {
						/* It's integer */
						found->text[0] = 0;
						found->text[1] = 'i';
						EDJ_INT(found) = (int)l;
						settings = lend;
					}
				}
				continue;

			case EDJ_STRING:
				/* May be quoted or unquoted */
				if (*value == '"' || *value == '\'') {
					size_t mbslen;

					/* Quoted */
					for (len = 1; value[len] && value[len] != value[0]; len++) {
						if (value[len] == '\\' && value[len + 1])
							len++;
					}
					mbslen = edj_mbs_unescape(NULL, value + 1, len - 1);
					jvalue = edj_string("", mbslen);
					edj_mbs_unescape(jvalue->text, value + 1, len - 1);
					settings = value + len + 1;
				} else {
					/* Unquoted */
					for (len = 0; value[len] && value[len] != ',' && value[len] != ' '; len++) {
					}
					jvalue = edj_string(value, len);
					settings = value + len;
				}
				edj_append(thisconfig, edj_key(name, jvalue));
				continue;

			default:
				found = edj_error_null(0, "Bad type for option \"%s\"", name);
				free(name);
				if (refend)
					*refend = settings;
				return found;
			}

		} else { /* name or noname without = */
			if (found && found->type == EDJ_BOOLEAN) {
				strcpy(found->text, negate ? "false" : "true" );
				settings += namelen;
				continue;
			} else if (found) {
				found = edj_error_null(0, "Option \"%s\" is not boolean", name);
				free(name);
				if (refend)
					*refend = settings;
				return found;
			} else if (!strncmp(name, "no", 2)) {
				thisconfig = config;
				found = edj_by_key(config, name + 2);
				if (!found) {
					thisconfig = edj_config;
					found = edj_by_key(edj_config, name + 2);
				}
				/* NOTE: We don't need to check for a color name
				 * because you can never say "set noprompt".
				 */
				if (found && found->type == EDJ_BOOLEAN) {
					strcpy(found->text, "false");
					settings += namelen;
					continue;
				}
			}

			/* If we get here, then the only remaining possibility
			 * for success is if it's a value from an enumerated
			 * list.  The list could only be in "config", not at
			 * the top-level "edj_config".  Look for lists!
			 */
			for (list = config->first; list; list = list->next) { /* object */
				/* Skip if not "somename-list" array */
				len = strlen(list->text);
				if (len <= 5
				 || strcasecmp(list->text + len - 5, "-list")
				 || list->first->type != EDJ_ARRAY)
					continue;

				/* For each element in the list... */
				for (found = edj_first(list->first); found; found = edj_next(found)) {
					/* Skip if not a string */
					if (found->type != EDJ_STRING)
						continue;

					/* Skip if not a match.  Since the list
					 * elements can contain spaces, we can't
					 * compare to "name" -- we need to look
					 * at the "settings" string.
					 */
					len = edj_mbs_len(found->text);
					if (!edj_mbs_ncasecmp(found->text, settings, len)
						&& !isalnum(settings[strlen(found->text)])) {
						/* FOUND! */
						goto BreakBreak;
					}
				}
			}

			/* Dang.  What is this? */
			found = edj_error_null(0, "Unknown option \"%s\"", name);
			free(name);
			if (refend)
				*refend = settings;
			return found;

BreakBreak:
			/* We found it, list->text is the name but with "-list"
			 * appended, and found->text is the new value.  I would
			 * rather not mangle list-text even temporarily, so
			 * we'll copy it into the "name" buffer first.
			 */
			namelen = strlen(list->text) - 5;
			if (namealloc < namelen + 1) {
				namealloc = (namelen | 0x1f) + 1;
				name = realloc(name, namealloc);
			}
			strncpy(name, list->text, namelen);
			name[namelen] = '\0';

			/* Store the setting as a string */
			edj_append(config, edj_key(name, edj_string(found->text, -1)));

			/* If the list was deferred (unlikely!) then let the
			 * library know that we abandoned the scanning loop
			 * before its end.  The "found" pointer will be invalid
			 * after this.
			 */
			edj_break(found);

			/* Move past the enumerated value */
			settings += strlen(found->text);
			continue;
		}
	}

	/* Hit the end of settings without trouble, hooray! */
	free(name);
	if (refend)
		*refend = settings;
	return NULL;
}

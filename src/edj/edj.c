#include <stdio.h>
#include <stdlib.h>
#include <locale.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <signal.h>
#include <glob.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <sys/stat.h>
#include <edj.h>
#include "edjprog.h"
#include "version.h"



/* getopt() variables not declared in <unistd.h> */
extern char *optarg;
extern int optind, opterr, optopt;
#define OPTFLAGS "c:f:F:ioaus:S:pl:L:d:D:j:r?"

/* -i -- Force interactive mode */
int interactive = 0;

/* -r -- Restricted -- Try to be more secure */
int restricted = 0;

/* -u -- Update -- Allow files named on the command line to be rewritten */
int allow_update = 0;

/* -D -- This is a directory to check for autoload */
edj_t *autoload_list = NULL;

/* This is the context -- a stack of data that is available to expressions. */
edjcontext_t *context;

/* These are lists of commands to execute once, or for each data file. */
edjcmd_t *autocmd = NULL; /* Once, from -Fscript */
edjcmd_t *initcmd = NULL; /* For each data file, from -ccommand or -fscript */


/* Output a debugging flag usage message */
void debug_usage()
{
	puts("The -jflags option controls debugging flags.  The flags are single letters");
	puts("indicating what should be debugged.  The flag letters are:");
	puts("  a  Call abort() when an error is detected.");
	puts("  e  Output info about edj_by_expr() calls.");
	puts("  c  Output info about edj_calc() calls.");
	puts("  t  Trace each command as it is run.");
	puts("");
	puts("You may also put a + or - or = between -j and the flags to alter the way the new");
	puts("flags are combined with existing flags.  The means are:");
	puts("  +  Add the new flags to existing flags. (This is the default.)");
	puts("  =  Turn off all flags except the new flags.");
	puts("  -  Turn off existing flags that are also in new flags.");
}

/* Output a usage message and then exit. */
static void usage(char *fmt, char *data)
{
	puts("Usage: edj [flags] [script] [name=value]... [file.json]...");
	puts("Flags: -c command        Evaluate command for each input file, and quit.");
	puts("       -f/-F script      Run script for each input file, or once persistently.");
	puts("       -i                Interactive.");
	puts("       -o                Object - Assume new files contain an empty object.");
	puts("       -a                Array - Assume new files contain an empty array.");
	puts("       -u                Update - Write back any modified files.");
	puts("       -s/-S settings    Adjust the settings, this session or persistently.");
	puts("       -p                Pretty-print, same as -sjson,pretty,oneline=0.");
	puts("       -l/-L plugin,set  Load & adjust a plugin, this session or persistently.");
	puts("       -d/-D directory   Autoload from directory, this session or persistently.");
	puts("       -j debugflags     Debugging settings, use -j? for details.");
	puts("This program manipulates JSON data. Without one of -ccalc or -ffile, it will");
	puts("assume -c\"this\" or -c\"select\" if -sconfig is given, or -i if no -sconfig.");
	puts("Any name=value parameters on the command line will be added to the context");
	puts("so you can use them like variables. Any data files named on the command line");
	puts("will be processed one at a time for -ccalc or -ffile.  For -i, the first data");
	puts("file is loaded as \"data\" and you can switch to others via a \"file\"command.");
	puts("Most flags may be repeated for a cumulative effect.  Uppercase -Ssettings,");
	puts("-Ddirectory, -Fscript and -Lplugin,settings are persistent across interactive");
	puts("sessions, while the lowercase versions are for that session only.");
	if (fmt)
		printf(fmt, data);
	exit(1);
}

/* Return 1 if a given name refers to an existing directory.  If it doesn't
 * exist, or is something other than a directory, then return 0.
 */
int isdirectory(const char *filename)
{
	struct stat st;

	if (stat(filename, &st) < 0)
		return 0;
	if ((st.st_mode & S_IFMT) != S_IFDIR)
		return 0;
	return 1;
}

/* This function is called if you use an unrecognized name in a statement.
 * If the name is "files" then it returns an array of JSON files in the
 * sampledata directory.  If it is the name of one of those files, then it
 * loads the file into memory and returns that.
 */
edj_t *autoload(char *key)
{
	char    filename[1000];
	edj_t  *dir, *parser, *ext;

	/* Scan for a key.json (or other known extensions) file in any of
	 * the autoload directories.
	 */
	dir = edj_by_key(edj_config, "autoload");
	if (!dir || dir->type != EDJ_ARRAY)
		return NULL;
	for (dir = edj_first(dir); dir; dir = edj_next(dir)) {
		if (dir->type != EDJ_STRING)
			continue;
		parser = edj_by_key(edj_system, "parsers");
		if (!parser)
			continue;
		for (parser = edj_first(parser); parser; parser = edj_next(parser)) {
			if (parser->type != EDJ_OBJECT)
				continue;
			ext = edj_by_key(parser, "suffix");
			if (!ext)
				continue;
			for (ext = edj_first(ext); ext; ext = edj_next(ext)) {
				if (ext->type != EDJ_STRING)
					continue;
				sprintf(filename, "%s/%s%s", dir->text, key, ext->text);
				if (access(filename, R_OK) == 0) {
					edj_break(ext);
					edj_break(parser);
					edj_break(dir);
					return edj_parse_file(filename);
				}
			}
		}
	}

	/* No joy */
	return NULL;

}


/* This is a context hook.  It adds a layer a the standard context for
 * autoloading files from the -ddirectory.
 */
static edjcontext_t *autodir(edjcontext_t *context)
{
	/* Add the autoloader */
	context = edj_context(context, edj_object(), EDJ_CONTEXT_GLOBAL);
	context->autoload = autoload;
	return context;
}


/******************************************************************************/

/* Load the contents of a file into a dynamically-allocated string buffer */
char *jcreadscript(const char *filename)
{
	FILE	*fp;
	long	size;
	char	*buf;

	/* Open the file, or give an error message */
	fp = fopen(filename, "r");
	if (!fp) {
		perror(filename);
		return NULL;
	}

	/* Find the length of the file */
	fseek(fp, 0, SEEK_END);
	size = ftell(fp);
	rewind(fp);

	/* Allocate space for the script, plus a terminating NULL */
	buf = (char *)malloc((size_t)(size + 1));

	/* Read it */
	fread(buf, 1, (size_t)size, fp);

	/* Add a terminating '\0' */
	buf[size] = '\0';

	/* Close the file */
	fclose(fp);

	/* Return it */
	return buf;
}

void run(edjcmd_t *jc, edjcontext_t **refcontext)
{
	edjcmdout_t *result;

	result = edj_cmd_run(jc, &context);
	if (result) {
		if (result->ret == &edj_cmd_break)
			edj_user_printf(NULL, "debug", "RETURNED A \"BREAK\"\n");
		else if (result->ret == &edj_cmd_continue)
			edj_user_printf(NULL, "debug", "RETURNED A \"CONTINUE\"\n");
		else if (result->ret) {
			edj_user_printf(NULL, "debug", "RETURNED A VALUE: ");
			edj_print(result->ret, NULL);
			edj_free(result->ret);
		} else {
			if (result->where) {
				int lineno;
				edjfile_t *jf = edj_file_containing(result->where, &lineno);
				if (jf)
					edj_user_printf(NULL, "error", "%s:%d: ", jf->filename, lineno);
			}
			edj_user_printf(NULL, "error", "%s", result->text);
			edj_user_printf(NULL, "normal", "\n");
		}
		free(result);
	}
}

/* Try to guess whether a given file is a data file based on its name and
 * maybe its contents.
 */
int isscript(char *filename)
{
	char	*ext = strrchr(filename, '.');
	char	buf[100], *s;
	FILE	*fp;
	size_t	nbytes;

	/* If it doesn't exist, then it isn't a script */
	if (access(filename, F_OK) < 0)
		return 0;

	/* If it has a well known filename extension, trust it */
	if (ext) {
		if (!strcasecmp(ext, ".json")
		 || !strcasecmp(ext, ".xml")
		 || !strcasecmp(ext, ".wsdl")
		 || !strcasecmp(ext, ".csv"))
			return 0;
		if (!strcasecmp(ext, ".edj")
		 || !strcasecmp(ext, ".js")) /* I know, but it isn't data */
			return 1;
	}

	/* Okay, we'll do it the hard way: Check the first few bytes */
	fp = fopen(filename, "r");
	if (fp) {
		nbytes = fread(buf, 1, sizeof buf - 1, fp);
		fclose(fp);
		if (nbytes < 2)
			return 0; /* too short to be a meaningful script */
		buf[nbytes] = '\0';
		if (!strncmp(buf, "#!", 2) || !strncmp(buf, "//", 2))
			return 1; /* Looks script-ish! */

		/* One last chance for a script: If it starts with a word
		 * followed by a character other than comma, it's a script.
		 */
		if (isalpha(buf[0])) {
			for (s = buf; isalnum(*s); s++) {
			}
			if (*s != ',' && *s)
				return 1;
		}
	}

	/* Okay, we can't say for sure.  Assume data */
	return 0;
}

/* Delete a string from an array of strings */
static void delete_from_array(edj_t *array, const char *str)
{
	edj_t *scan, *lag;

	/* If array is empty, do nothing */
	if (!array->first)
		return;

	/* Scan for the string */
	for (lag = NULL, scan = array->first; scan; lag = scan, scan = scan->next)
		if (!strcmp(scan->text, str))
			break;
	if (scan) {
		if (lag)
			lag->next = scan->next;
		else
			array->first = scan->next;
		scan->next = NULL;
		edj_free(scan);
	}
}


/******************************************************************************/
int main(int argc, char **argv)
{
	int	i;
	edj_t	*args, *section, *err, *omitplugins, *omitscripts;
	char	*val, *plugin;
	int	anypersistent = 0;
	int	anyfiles = 0;
	int	firstscript = 0;
	int	pretty = 0;
	int	opt;
	int	exitcode = 0;

	/* Set the locale.  JSON data and edj scripts always uses numbers
	 * formatted for the "C" locale, but we can use the real locale
	 * for everything else.
	 */
	(void)setlocale(LC_ALL, "");
	(void)setlocale(LC_NUMERIC, "C");


	/* Detect "--version" */
	if (argc >= 2 && !strcmp(argv[1], "--version")) {
		printf("edj %s\n", EDJ_VERSION);
		printf("Copyright %s\n", EDJ_COPYRIGHT);
		puts("Freely redistributable under the terms of the");
		puts("GNU General Public License v3 or later.");
		return 0;
	}

	/* Detect "--help" */
	if (argc >= 2 && !strcmp(argv[1], "--help")) {
		usage(NULL, NULL);
		return 0;
	}

	/* Try to load the config.  If not found, use built-in defaults.
	 * This also sets up edj_system and edj_config.  It also fetches
	 * the list of persistent plugins, if there is one.
	 */
	edj_config_load("edj");

	/* The library's default settings don't include an "autoload" directory
	 * list, "autoscript" script list, or "autoplugin" plugin list because
	 * the library doesn't support those itself; the edj program does.
	 * If we reloaded those values from the config file then keep them;
	 * if they're missing from config then add them now.
	 */
	section = edj_by_key(edj_config, "autoload");
	if (!section || section->type != EDJ_ARRAY)
		edj_append(edj_config, edj_key("autoload", edj_array()));
	section = edj_by_key(edj_config, "autoscript");
	if (!section || section->type != EDJ_ARRAY)
		edj_append(edj_config, edj_key("autoscript", edj_array()));
	section = edj_by_key(edj_config, "autoplugin");
	if (!section || section->type != EDJ_ARRAY)
		edj_append(edj_config, edj_key("autoplugin", edj_array()));

	/* Scan the options for things we need to know to decide whether this
	 * will be batch or interactive.  Check whether the first arg after
	 * the options is a script file to help with that decision.  Also,
	 * this adjusts the list of persistent plugins, including a lowercase
	 * -l-plugin to temporarily remove a persistent plugin.
	 */
	interactive = -1;
	omitplugins = edj_array();
	omitscripts = edj_array();
	while ((opt = getopt(argc, argv, OPTFLAGS)) >= 0) {
		switch (opt) {
		case 'c':
		case 'p':
		case 's':
			if (interactive == -1)
				interactive = 0;
			break;
		case 'i':
			interactive = 1;
			break;
		case 'u':
			allow_update = 1;
			edj_append(edj_system, edj_key("update", edj_boolean(1)));
			break;
		case 'L':
			/* Adjust the list of persistent plugins but DON'T LOAD
			 * YET!  We'll load the whole persistent list after
			 * we're done processing all -L flags, if interactive.
			 */
			section = edj_by_key(edj_config, "autoplugin");
			plugin = strdup(optarg);
			val = strchr(plugin, ',');
			if (val)
				*val = '\0';
			if (*plugin == '-') {
				/* Delete from the list of persistent plugins */
				delete_from_array(section, plugin + 1);
			} else {
				/* Delete if already there, and then append
				 * to the end of the list.
				 */
				delete_from_array(section, plugin);
				edj_append(section, edj_string(plugin, -1));
			}
			free(plugin);
			break;

		case 'f':
			/* Here, all we're doing is watching for -f-script to
			 * temporarily remove a persistent script.  We do this
			 * by adding its name to a separate "omitscripts" list.
			 */
			if (optarg[0] == '-') {
				/* Add it to the omitscripts list */
				val = strchr(optarg, ',');
				if (val)
					edj_append(omitscripts, edj_string(optarg + 1, val - optarg - 1));
				else
					edj_append(omitscripts, edj_string(optarg + 1, -1));
			} else {
				/* Using -f means it isn't interactive unless
				 * -i is also given.
				 */
				if (interactive == -1)
					interactive = 0;
			}
			break;

		case 'l':
			/* Here, all we're doing is watching for -l-plugin to
			 * temporarily remove a persistent plugin.  We do this
			 * by adding its name to a separate "omitplugins" list.
			 */
			if (optarg[0] == '-') {
				/* Find the end of the plugin name */
				val = strchr(optarg + 1, ',');
				if (!val)
					val = optarg + strlen(optarg);

				/* Add it to the omitplugins list */
				edj_append(omitplugins, edj_string(optarg + 1, val - optarg));
			}
			break;

		case '?':
			edj_free(omitplugins);
			edj_free(omitscripts);
			usage(NULL, NULL);
			break;
		}
	}
	if (interactive != 1 && optind < argc)
		firstscript = isscript(argv[optind]);
	if (interactive == -1 && firstscript)
		interactive = 0;
	else if (interactive == -1)
		interactive = 1;
	optind = 1;

	/* Set the runmode in edj_system to "interactive" or "batch" */
	val = interactive ? "interactive" : "batch";
	edj_append(edj_system, edj_key("runmode", edj_string(val, -1)));

	/* When running in batch mode, we use a separate set of formatting
	 * options which are NOT persistent, because we want batch scripts to
	 * have a consistent runtime environment.  HOWEVER, we can tweak those
	 * settings to work better for terminal output vs pipe/file output.
	 * USERS CAN STILL OVERRIDE THIS VIA THE -s FLAG.
	 */
	if (!interactive && isatty(1)) {
		/* Copy "graphic", "color", and "table" from interactive */
		section = edj_by_key(edj_config, "interactive");
		edj_config_set("batch", "graphic", edj_copy(edj_by_key(section, "graphic")));
		edj_config_set("batch", "color", edj_copy(edj_by_key(section, "color")));
		edj_config_set("batch", "table", edj_copy(edj_by_key(section, "table")));
	}

	/* Load the persistent plugins.  We have to do this before we process
	 * any settings included in -Dplugin,settings flags.  Also load any
	 * -Fscript scripts, because they might load plugins too.
	 */
	if (interactive) {
		/* Plugins first */
		section = edj_by_key(edj_config, "autoplugin");
		for (args = section->first; args; args = args->next) {
			/* Skip if in "omitplugins" list (abusing the "err" variable)*/
			for (err = omitplugins->first; err; err = err->next)
				if (!strcmp(err->text, args->text))
					break;
			if (err)
				continue;

			/* load it */
			err = edj_plugin_load(args->text);
			if (err) {
				fprintf(stderr, "%s\n", err->text);
				edj_free(err);
				exitcode = 1;
				goto CleanExit;
			}
		}

		/* Then scripts */
		section = edj_by_key(edj_config, "autoscript");
		for (args = section->first; args; args = args->next) {
			/* Skip if in "omitscripts" list (abusing the "err" variable)*/
			for (err = omitscripts->first; err; err = err->next)
				if (!strcmp(err->text, args->text))
					break;
			if (err)
				continue;

			/* Load it.  This will load plugins now (which is what
			 * we really care about) but we need to wait to execute
			 * other parts of it until we've created the context.
			 */
			autocmd = edj_cmd_append(autocmd, edj_cmd_parse_file(optarg), NULL);
		}
	}

	/* Free the "omitplugins" and "omitscripts" lists */
	edj_free(omitplugins);
	edj_free(omitscripts);

	/* Parse persistent flags */
	while ((opt = getopt(argc, argv, OPTFLAGS)) >= 0) {
		if (!strchr("FDLS", opt))
			continue;
		if (!interactive) {
			fprintf(stderr, "Persistent options only work for interactive invocations\n");
			exitcode = 1;
			goto CleanExit;
		}
		anypersistent = 1;
		switch (opt) {
		case 'F':
			section = edj_by_key(edj_config, "autoscript");
			if (*optarg == '-') {
				/* Delete from the list */
				delete_from_array(section, optarg + 1);
			} else {
				/* If it was already in the list, delete it */
				delete_from_array(section, optarg);

				/* Insert at front of the list */
				args = edj_string(optarg, -1);
				args->next = section->first;
				section->first = args;
			}
			break;
		case 'D':
			section = edj_by_key(edj_config, "autoload");
			if (*optarg == '-') {
				/* Delete from the list */
				delete_from_array(section, optarg + 1);
			} else {
				/* If it was already in the list, delete it */
				delete_from_array(section, optarg);

				/* Insert at front of the list */
				args = edj_string(optarg, -1);
				args->next = section->first;
				section->first = args;
			}
			break;
		case 'L':
			/* We've already adjusted the autoplugin list and
			 * loaded all persistent plugins.  All we're doing
			 * here is adjusting each plugin's settings.
			 */

			/* Separate the settings from the name */
			plugin = strdup(*optarg == '-' ? optarg + 1 : optarg);
			val = strchr(plugin, ',');
			if (val) {
				*val++ = '\0';

				/* Find this plugin's settings */
				section = edj_by_key(edj_config, "plugin");
				if (section)
					section = edj_by_key(section, plugin);
				if (!section) {
					fprintf(stderr, "The \"%s\" plugin doesn't use settings\n", plugin);
					exitcode = 1;
					goto CleanExit;
				}

				/* Adjust the settings */
				err = edj_config_parse(section, val, NULL);
				if (err) {
					puts(err->text);
					edj_free(err);
					exitcode = 1;
					goto CleanExit;
				}
			}
			free(plugin);
			break;

		case 'S':
			/* Parse the options. */
			section = edj_by_key(edj_config, "interactive");
			err = edj_config_parse(section, optarg, NULL);
			if (err) {
				puts(err->text);
				edj_free(err);
				exitcode = 1;
				goto CleanExit;
			}

		}
	}
	optind = 1;

	/* If any persistent settings were changed, save the config */
	if (anypersistent)
		edj_config_save("edj");

	/* Register the autoload handler if we're interactive.  If we end up
	 * not using autoload, having this registered will be only a minor
	 * inefficiency.
	 */
	if (interactive)
		edj_context_hook(autodir);

	/* Create an object that will hold any name=value parameters, and
	 * start a context using it.  We have to do this before our final
	 * scan of command-line options because we need to know the context
	 * while parsing -ccommand or -fscript options.
	 */
	args = edj_object();
	context = edj_context_std(args);

	/* Parse most remaining command-line flags.  In particular, we load
	 * any scripts so that we can process the "plugin" commands they
	 * contain, so ALL plugins will be loaded.
	 */
	while ((opt = getopt(argc, argv, OPTFLAGS)) >= 0) {
		switch (opt) {
		case 'r':
			restricted = 1;
			break;
		case 'o':
			edj_file_new_type = 'o';
			break;
		case 'a':
			edj_file_new_type = 'a';
			break;
		case 'd':
			section = edj_by_key(edj_config, "autoload");
			if (*optarg == '-') {
				/* Delete from the list */
				delete_from_array(section, optarg + 1);
			} else {
				/* If it was already in the list, delete it */
				delete_from_array(section, optarg);

				/* Insert at front of the list */
				args = edj_string(optarg, -1);
				args->next = section->first;
				section->first = args;
			}
			break;
		case 'l':
			/* If -l-plugin then skip it.  That's just for
			 * temporarily removing persistent plugins, and we
			 * already did that.
			 */
			if (*optarg == '-')
				break;

			/* Make a copy of the -lplugin,options string */
			plugin = strdup(optarg);

			/* Separate the name from the options */
			val = strchr(plugin, ',');
			if (val)
				*val++ = '\0';

			/* Load the plugin */
			err = edj_plugin_load(plugin);
			if (err) {
				fprintf(stderr, "%s\n", err->text);
				edj_free(err);
				free(plugin);
				exitcode = 1;
				goto CleanExit;
			}

			/* If there are options, process them now */
			if (val && *val)
			{ 
				/* Find the edj_config.plugin.{plugin} section.
				 * If it doesn't exist, then fail.
				 */
				section = edj_by_key(edj_config, "plugin");
				if (section)
					section = edj_by_key(section, plugin);
				if (!section) {
					fprintf(stderr, "The \"%s\" plugin doesn't use settings\n", plugin);
					free(plugin);
					exitcode = 1;
					goto CleanExit;
				}

				/* Parse the options.  Watch for errors */
				err = edj_config_parse(section, val, NULL);
				if (err) {
					fprintf(stderr, "%s\n", err->text);
					edj_free(err);
					free(plugin);
					exitcode = 1;
					goto CleanExit;
				}
			}
			free(plugin);
			break;
		case 's':
			section = edj_by_key(edj_config, interactive ? "interactive" : "batch");
			err = edj_config_parse(section, optarg, NULL);
			if (err) {
				fprintf(stderr, "%s\n", err->text);
				exitcode = 1;
				goto CleanExit;
			}
			break;
		case 'p':
			pretty++;
			break;
		case 'j':
			if (*optarg == '?' || edj_debug(optarg)) {
				debug_usage();
				goto CleanExit;
			}
			break;
		case 'c':
			/* Parse a command and add it to the initcmd list. */
			initcmd = edj_cmd_append(initcmd, edj_cmd_parse_string(optarg), context);
			break;
		case 'f':
			/* Add commands from the script to initcmd.  It is also
			 * likely to have defined some functions, which don't
			 * add anything to initcmd.
			 */
			initcmd = edj_cmd_append(initcmd, edj_cmd_parse_file(optarg), context);
			break;
		case 'F':
		case 'L':
		case 'D':
		case 'S':
		case 'i':
		case 'u':
			/* already handled */
			break;
		case '?':
			usage(NULL, NULL);
			goto CleanExit;
		default:
			{
				char optstr[2];
				optstr[0] = (char)opt;
				optstr[1] = '\0';
				usage("Invalid flag -%s\n", optstr);
				exitcode = 1;
				goto CleanExit;
			}
		}
	}

	/* If one or more -p flags were given, adjust the format accordingly. */
	if (pretty > 0) {
		if (interactive) {
			fprintf(stderr, "The -p flag only works for batch invocations\n");
			exitcode = 1;
			goto CleanExit;
		}
		switch (pretty) {
		case 1:	val = "pretty,json,oneline=0,noelem";	break;
		case 2: val = "pretty,json,oneline=0,elem";	break;
		default:val = "nopretty,json,oneline=70";
		}

		section = edj_by_key(edj_config, "batch");
		err = edj_config_parse(section, val, NULL);
		if (err) {
			fprintf(stderr, "%s\n", err->text);
			exitcode = 1;
			goto CleanExit;
		}
	}

	/* We determined earlier whether the first non-option argument is a
	 * script (as opposed to data or a name=value).  If it's a script
	 * then parse it like a -fscript flag.
	 */
	if (firstscript) {
		initcmd = edj_cmd_append(initcmd, edj_cmd_parse_file(argv[optind]), context);
		optind++;
	}

	/* If -ccmd or -fscript resulted in an error (already reported) then
	 * quit now.
	 */
	if (initcmd == EDJ_CMD_ERROR || autocmd == EDJ_CMD_ERROR) {
		exitcode = 1;
		goto CleanExit;
	}

	/* Set the formatting from the config */
	edj_format_set(NULL, NULL);

	/* Add any name=value parameters to the "args" object that we created
	 * when we started the context.  Also, add any data files named on
	 * the command line, via edj_context_file().
	 */
	for (i = optind; i < argc; i++) {
		edj_t *tmp;

		/* If it looks like a file, then add it to the list of files */
		if (((strchr(argv[i], '.') && edj_file_new_type != '\0')
		  || !strcmp(argv[i], "-")
		  || 0 == access(argv[i], F_OK))
		 && !isdirectory(argv[i])) {
			/* If it doesn't exist and no -a/-o was given, fail */
			if (strcmp(argv[i], "-") && 0 != access(argv[i], F_OK) && edj_file_new_type == '\0'){
				perror(argv[i]);
				exitcode = 1;
				goto CleanExit;
			}
			edj_context_file(context, argv[i], allow_update, NULL);
			anyfiles = 1;
		} else {
			/* In a name=value string, separate the name from the value */
			val = strchr(argv[i], '=');
			if (val)
				*val++ = '\0';
			else
				val = "true";

			/* Does the value look like JSON? */
			if (strchr("{[\"-.0123456789", *val) /*}*/
			 || !strcmp("true", val)
			 || !strcmp("false", val)
			 || !strcmp("null", val))
				tmp = edj_parse_string(val);
			else
				tmp = edj_string(val, -1);
			edj_append(args, edj_key(argv[i], tmp));
		}
	}

	/* Start on the first file named on the command line, if any */
	edj_context_file(context, NULL, 0, NULL);

	/* If this was determined to be a batch invocation (not interactive)
	 * but no -ccommand or -fscript flags were given, then assume "-cdata"
	 * or "-cselect" so any data will simply be output.
	 */
	if (!interactive && !initcmd)
	{
		args = edj_config_get("batch", "table");
		if (args && (!strcmp(args->text, "grid") || !strcmp(args->text, "sh") || !strcmp(args->text, "csv")))
			initcmd = edj_cmd_parse_string("select");
		else
			initcmd = edj_cmd_parse_string("data");
	}

	/* If batch mode and no filenames were named on the command line,
	 * then assume "-", unless stdin is a tty.
	 */
	if (!interactive && !anyfiles && !isatty(0))
		edj_context_file(context, "-", 1, NULL);

	/* Run any -Fscript scripts now, if interactive.  (For batch mode,
	 * we'll run them later, separately for each data file.)
	 */
	if (interactive) {
		run(autocmd, &context);
	}
	edj_cmd_free(autocmd);

	/* Start on the first file */
	edj_context_file(context, NULL, 0, NULL);

	/* Do either the batch or interactive thing */
	if (interactive)
		interact(&context, initcmd);
	else
		batch(&context, initcmd);

	/* Clean up & exit */
CleanExit:
	/* Update the last file, if appropriate */
	if (allow_update) {
		/* Switch to previous file to trigger update, if the current
		 * file was modified.  Switching to the previous file is enough
		 * to trigger this even if there was no previous file.
		 */
		i = EDJ_CONTEXT_FILE_PREVIOUS;
		edj_context_file(context, NULL, 0, &i);
	}

	/* Free the context stack */
	while (context)
		context = edj_context_free(context);

	/* Free the initialization commands (from -ccmd and -fffile) */
	edj_cmd_free(initcmd);

	/* Revert to normal text colors */
	edj_user_printf(NULL, "normal", "");

	/* Return the success/fail status */
	return exitcode;
}

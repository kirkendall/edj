#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <edj.h>
#include "edjprog.h"

typedef struct opt_s {
	struct opt_s *next;
	char *shortname;/* short name of the option without "-" */
	char *longname; /* long name of the option without "--" or "=" */
	char *arg;	/* name of argument, or NULL if none.  "#" number */
	char *desc;	/* one-line description of the option */
} opt_t;

static char *name;	/* name of the script (for the usage message) */
opt_t *options, *optend;/* List of options */
static char *desc;	/* multi-line description */
static int namevalue;	/* is name=value allowed? */
static int files;	/* are filenames allowed? */
static char *version;	/* version number (of the script, not edj) */
static char *license;	/* multi-line copyright/license text */

/* Output a usage message, possibly with an error, and then quit */
static void script_usage(char *error)
{
	int	width, widest;
	opt_t	*opt;

	/* One-line summary */
	printf("usage: %s", name);
	if (options)
		fputs(" [options]", stdout);
	if (namevalue)
		fputs(" [name=value...]", stdout);
	if (files)
		fputs(" [files...]", stdout);
	putchar('\n');

	/* List of options.  First, find the column widths, then output */
	widest = 0;
	for (opt = options; opt; opt = opt->next) {
		if (opt->shortname) {
			width = 2;
			if (opt->arg)
				width += strlen(opt->arg);
		} else {
			width = 2 + strlen(opt->longname);
			if (opt->arg)
				width += 1 + strlen(opt->arg);
		}
		if (width > widest)
			widest = width;
	}
	for (opt = options; opt; opt = opt->next) {
		if (opt->shortname) {
			if (opt->arg)
				printf("  -%s%-*s %s\n", opt->shortname, widest - 2, opt->arg, opt->desc);
			else
				printf("  -%-*s %s\n", widest - 1, opt->shortname, opt->desc);
			if (opt->longname)
				printf("  --%s\n", opt->longname);
		} else {
			if (opt->arg)
				printf("  --%s=%-*s %s\n", opt->longname, (int)(widest - strlen(opt->longname) - 3), opt->arg, opt->desc);
			else
				printf("  --%-*s %s\n", width - 2, opt->longname, opt->desc);
		}
	}

	/* Long descriptive text */
	if (desc)
		fputs(desc, stdout);

	/* Error message */
	if (error)
		fputs(error, stdout);
}

/* Output the version number, and quit */
static void script_version(void)
{
	printf("%s %s", name, version ? version : "(no version)");
	if (license) {
		putchar(' ');
		fputs(license, stdout);
	} else {
		putchar('\n');
	}
}

/* Count non-whitespace chars */
static size_t wordlen(const char *str, char other)
{
	size_t len;
	for (len = 0; str[len] && !isspace(str[len]) && str[len] != other; len++) {
	}
	return len;
}


/* Process a script's options.  "first" is the index of the script name.
 * Returns index of first data file, or argc if there are none, or 0 if
 * edj should exit immediately.
 */
int edjprog_getopt(int argc, char **argv, int first, edjcontext_t *context)
{
	int arg;
	opt_t *opt;
	char	errbuf[200];

	/* Save the script name */
	name = strrchr(argv[first], '/');
	if (name)
		name++;
	else
		name = argv[first];

	/* Parse the script options */
	for (arg = first + 1; arg < argc && argv[arg][0] == '-'; arg++)
	{
		/* Some special options */
		if (!strcmp(argv[arg], "--help") || !strcmp(argv[arg], "-?")) {
			script_usage(NULL);
			return 0;
		}
		if (!strcmp(argv[arg], "--version")) {
			script_version();
			return 0; 
		}

		/* If double-hyphen, then it's a long option */
		if (argv[arg][1] == '-') {
			size_t len = wordlen(argv[arg] + 2, '\0');
			for (opt = options; opt && (strncmp(opt->longname, argv[arg] + 2, len) || opt->longname[len]); opt = opt->next) {
			}

			/* Is it recognized */
			if (!opt) {
				snprintf(errbuf, sizeof errbuf, "Unrecognized long option --%.*s", (int)len, argv[arg]);
				script_usage(errbuf);
				return -1;
			}

			/* parameters okay? */

		}

		/* It must be a short option, or a series of short options */
	}

	/* Maybe parse name=value parameters */

	/* Return the index of files or other args */
	return arg;
}

/* Appends a line to existing text.  Existing text may be NULL initially.
 * The added text should not include a newline; that gets added here.
 */
static char *append(char *existing, const char *added)
{
	size_t	len = 0;
	if (existing)
		len = strlen(existing);
	len += strlen(added) + 2; /* for newline and '\0' */
	if (existing)
		existing = realloc(existing, len);
	else
		existing = calloc(len, sizeof(char));
	strcat(existing, added);
	strcat(existing, "\n");
	return existing;
}

/* Make a dynamic copy of a string of a given length */
static char *copylen(const char *str, size_t len)
{
	char	*copy = malloc(len + 1);
	strncpy(copy, str, len);
	copy[len] = '\0';
	return copy;
}


/* This is called to process the arguments of a single getopt command line */
void edjprog_getopt_def(char *argstr)
{
	size_t	len;
	opt_t	*opt;

	/* Classify the arguments */
	if (*argstr == '-') {

		/* Defining an option */

		/* Allocate it */
		opt = malloc(sizeof *opt);
		memset(opt, 0, sizeof *opt);

		/* If there's a short flag, use it */
		if (*++argstr != '-') {
			opt->shortname = copylen(argstr, 1);
			argstr++;
			len = wordlen(argstr, '\0');
			if (len > 0) {
				opt->arg = copylen(argstr, len);
				argstr += len;
			}
			while (*argstr && isspace(*argstr))
				argstr++;
			if (*argstr == '-')
				argstr++;
		}

		/* If there's a long flag, use it */
		if (*argstr == '-') {
			argstr++;
			len = wordlen(argstr, '=');
			opt->longname = copylen(argstr, len);
			argstr += len;
			if (*argstr == '=') {
				argstr++;
				len = wordlen(argstr, '\0');
				if (len > 0 && !opt->arg)
					opt->arg = copylen(argstr, len);
				argstr += len;
			}
			while (*argstr && isspace(*argstr))
				argstr++;
		}

		/* Everything else is the description */
		opt->desc = strdup(argstr);

		/* Append it to the end of the list */
		if (options)
			optend->next = opt;
		else
			options = optend = opt;

	} else if (*argstr == '[') {
		/* Defining a version and license */

		/* Parse the version number, if any */
		if (argstr[1] == ']') {
			argstr += 2;
		} else {
			for (len = 0; argstr[len + 1] && argstr[len + 1] != ']'; len++){
			}
			if (version)
				free(version);
			version = malloc(len + 1);
			strncpy(version, argstr + 1, len);
			version[len] = '\0';
			argstr += len;
			if (*argstr == ']')
				argstr++;
		}
		while (isspace(*argstr))
			argstr++;

		/* Append the license text, if any */
		if (*argstr)
			license = append(license, argstr);
	} else if (!strcmp(argstr, "name=value")) {
		/* This script allows name=value arguments */
		namevalue = 1;
	} else if (!strcmp(argstr, "files")) {
		/* This script allows filenames */
		files = 1;
	} else if (!strcmp(argstr, "usage")) {
		/* Output the usage message */
		script_usage(NULL);
	} else if (!strcmp(argstr, "version")) {
		/* Output the usage message */
		script_version();
	} else {
		/* Anything else is part of the long description */
		desc = append(desc, argstr);
	}
}

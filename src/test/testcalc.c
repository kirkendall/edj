#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <locale.h>
#include <edj.h>

#define GREEN	"\033[32;1m"
#define RED	"\033[31;1m"
#define PLAIN	"\033[m"

typedef struct {
	int	tests;
	int	failed;
} count_t;

int test_memleaks = 0;
int show_expression = 0;

/* Output a usage message and then exit */
void usage()
{
        puts("testcalc [flags] [file...]");
        puts("Flags: -m      Check for evidence of memory leaks during each test.");
        puts("       -e      Write each test to stderr before running. Helps for core dumps.");
        puts("       -Jflags Debug: +/-/= a:abort c:edj_calc_parse e:edj_by_expr t:trace");
        puts("This reads a series of tests from a file, and writes any inconsistencies to");
        puts("stdout. Input lines starting with # are section headers. Lines starting with");
        puts("name= define data to be used in tests later in the file. Lines that don't");
        puts("start with #, name=, or = are tests. Lines starting with = are the expected");
        puts("result of the preceding test.");
        exit(0);
}

/* Evaluate a single test */
char *test(char *str, edj_t *names)
{
        edjcontext_t *context;
        edjcalc_t *calc;
        edj_t  *result;
        char    *resultstr;
        const char *tail, *err;
        int	before, compiled;  
        int	calcleaks, parseleaks;

        /* Compile it */
        before = edj_debug_count;
        calc = edj_calc_parse(str, &tail, &err, 0);
        if (err)
                printf("Error: %s\n", err);
        if (!calc)
                return NULL;
	compiled = edj_debug_count;

        /* Evaluate it */
        context = edj_context_std(edj_copy(names));
        result = edj_calc(calc, context, NULL);
        resultstr = edj_serialize(result, NULL);
        edj_free(result);
        while (context)
		context = edj_context_free(context);

        /* Memory leak in edj_calc? */
        calcleaks = edj_debug_count - compiled;
        if (test_memleaks && calcleaks != 0)
		printf("leak: %s (calc leaked %d edj_t's)\n", str, calcleaks);

        /* Clean up.  Memory leak in edj_calc_parse()? */
        edj_calc_free(calc);
        parseleaks = edj_debug_count - before - calcleaks;
        if (test_memleaks && parseleaks != 0)
		printf("leak: %s (parser allocated %d leaked %d)\n", str, compiled - before, parseleaks);

        /* Return the result */
        return resultstr;
}

/* Read a series of tests, and run them */
void testfile(FILE *in, count_t *counts)
{
        char    buf[1000];
        char	section[1000];
	char	expression[1000];
	char	*resultstr = NULL;
        char    *tmp;
        edj_t  *names;

        /* Initialize section.  It'll be updated with real section names as
         * tests are read.
         */
	strcpy(section, "Before first section");

        /* Create an object for storing defined names */
        names = edj_object();

        /* For each line ... */
        while (fgets(buf, sizeof buf, in)) {
                /* Strip off the newline */
                tmp = strchr(buf, '\n');
                if (tmp)
                        *tmp = '\0';

		/* Skip blank lines */
		if (!buf[0])
			continue;

                /* Remember # lines as section labels */
                if (buf[0] == '#') {
			strcpy(section, buf);
			continue;
		}

		/* If result line, and we have a previous result, check it */
		if (buf[0] == '=' && resultstr) {
			if (strcmp(resultstr, buf + 1)) {
				fputs(RED, stdout);
				if (*section) 
					puts(section);
				printf("expr: %s\n", expression);
				printf("want: %s\n", buf + 1);
				printf("got:  %s\n", resultstr);
				fputs(PLAIN, stdout);
				fflush(stdout);
				counts->failed++;
			}
			continue;
		}

                /* If name=value then parse it and store it */
                if (isalpha(buf[0])) {
                        for (tmp = buf; isalnum(*tmp); tmp++) {
                        }
                        if (*tmp == '=' && tmp[1] != '=') {
                                *tmp++ = '\0';
                                edj_t *value = edj_parse_string(tmp);
                                if (value)
                                        edj_append(names, edj_key(buf, value));
                                continue;
                        }
                }

                /* Anything else is a test.  Evaluate it */
                if (show_expression) {
			fputs(buf, stderr);
			putc('\n', stderr);
		}
		strcpy(expression, buf);
		if (resultstr)
			free(resultstr);
                resultstr = test(buf, names);
                counts->tests++;
        }

        /* Clean up the names */
        edj_free(names);
}


int main(int argc, char **argv)
{
	int ch;
	count_t counts = {0,0};
        setlocale(LC_ALL, "");

        /* Use ASCII output, because it's trickier than UTF-8 */
        edj_config_load("testcalc");
        edj_config_set(NULL, "ascii", edj_boolean(1));
        edj_format_set(NULL, NULL);
        edj_config_set(NULL, "defersize", edj_from_int(0));

        /* Parse command-line flags */
        while ((ch = getopt(argc, argv, "meJ:")) >= 0)
        {
		switch (ch) {
		  case 'm': test_memleaks = 1;	break;
		  case 'e': show_expression = 1;break;
		  case 'J': edj_debug(optarg);	break;
		  default:
			usage();
		}
        }

	if (optind == argc) {
		testfile(stdin, &counts);
	} else {
		FILE *fp;

		for (; optind < argc; optind++) {
			fp = fopen(argv[optind], "r");
			if (fp) {
				testfile(fp, &counts);
				fclose(fp);
			} else {
				perror(argv[optind]);
			}
		}
	}

	printf("%sPassed %d/%d%s", GREEN, counts.tests - counts.failed, counts.tests, PLAIN);
	if (counts.failed > 0)
		printf(", %sFailed %d/%d%s", RED, counts.failed, counts.tests, PLAIN);
	putchar('\n');
        return counts.failed > 0;
}


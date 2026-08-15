#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <locale.h>
#include <edj.h>

/* Run a single test */
static int singletest(char *settings)
{
	edj_t	*err;

	err = edj_config_parse(NULL, settings, 0);
	if (!err) {
		edj_print(edj_config, NULL);
		return 1;
	}
	if (isatty(0))
		printf("\e[31m%s\e[m\n", err->text);
	else {
		puts(err->text);
	}
	edj_free(err);
	return 0;
}

int main(int argc, char **argv)
{
	int	i;
	char	buf[100], *eol;
	edj_t	*dummy;

	setlocale(LC_ALL,"");
	edj_config_load("textconfig");

	/* Add a dummy plugin */
	dummy = edj_object();
	edj_append(dummy, edj_key("host", edj_string("localhost", -1)));
	edj_append(dummy, edj_key("db", edj_string("", -1)));
	edj_append(dummy, edj_key("user", edj_string("", -1)));
	edj_append(edj_by_key(edj_config, "plugin"), edj_key("dummy",dummy));

	edj_print(edj_config, NULL);
	if (argc <= 1) {
		for (;;) {
			if (isatty(0))
				printf("testconfig> ");
			if (!fgets(buf, sizeof buf, stdin))
				break;
			eol = strchr(buf, '\n');
			if (eol)
				*eol = '\0';
			singletest(buf);
		}
	}
	for (i = 1; i < argc; i++)
		singletest(argv[i]);
	return 0;
}

#include <stdio.h>
#include <edj.h>

int main(int argc, char **argv)
{
	edjcmd_t *jc;
	edjcmdout_t *result;
	edjcontext_t *context;

	/* Create a context */
	context = edj_context_std(NULL);

	/* Parse the first command-line argument as an edj command */
	jc = edj_cmd_parse_string(argv[1]);
	if (jc != EDJ_CMD_ERROR) {
		/* Run the command */
		result = edj_cmd_run(jc, &context);

		/* If it returned anything, say what it returned */
		if (result) {
			if (result->ret) {
				/* Returned value */
				printf("returning ");
				edj_print(result->ret, NULL);
				putchar('\n');
				edj_free(result->ret);
			} else {
				/* Returned error */
				printf("%s\n", result->text);
			}
		}

		/* Clean up */
		edj_cmd_free(jc);
	}

	/* Free the context */
	edj_context_free(context);
	return 0;
}

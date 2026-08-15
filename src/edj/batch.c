#include <stdio.h>
#include <stdlib.h>
#include <edj.h>
#include "edjprog.h"


void batch(edjcontext_t **refcontext, edjcmd_t *initcmds)
{
	edj_t	*files;
	int	i;

	/* Fetch the list of filenames */
	files = edj_context_file(*refcontext, NULL, 0, NULL);

	/* If no files then just run any -c commands once and exit */
	if (edj_length(files) == 0) {
		run(initcmds, refcontext);
		return;
	}

	/* For each file... */
	for (i = 0; i < edj_length(files); i++) {

		/* Load the next file */
		edj_context_file(*refcontext, NULL, 0, &i);

		/* Run the initcmds on it */
		run(initcmds, refcontext);
	}
}

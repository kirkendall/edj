#include <stdlib.h>
#include <string.h>
#include <fineline.h>

/* Allocate and initialize a fineline_t */
fineline_t *fineline_alloc(void *context)
{
	/* Allocate it */
	fineline_t *fine = malloc(sizeof(fineline_t));

	/* Initialize it */
	memset(fine, 0, sizeof *fine);
	fine->dynamic = 0; /* just return history[0] every time */
	fine->matchparen = 1;
	fine->tabstop = 4;
	fine->context = context;
	fine->columns = 80; /* These are likely to be overridden */
	fine->rows = 24;

	/* History size must be at least 1 */
	fine->historysize = 1;
	fine->historyused = 0;
	fine->historyshown = 0;
	fine->history = calloc(fine->historysize, sizeof(char *));

	/* fine->history[0] is actually the current input buffer.  Give it a
	 * reasonable size; it will grow as necessary.
	 */
	fine->linesize = 256;
	fine->line = fine->history[0] = malloc(fine->linesize);
	fine->line[0] = '\0';

	/* Generic prompt */
	fine->prompt = "->";
	return fine;
}

/* Free a fineline_t */
void fineline_free(fineline_t *fine)
{
	int i;

	/* Free history, including the input buffer in history[0] */
	for (i = 0; i < fine->historysize && fine->history[i]; i++)
		free(fine->history[i]);
	free(fine->history);

	/* Free the image */
	if (fine->image)
		fineline_image_free(fine->image);

	/* Free the style array too.  The individual strings that it points to
	 * are static so they don't need to be freed.
	 */
	if (fine->style)
		free(fine->style);

	/* If there are completions, free them */
	if (fine->complete) {
		for (i = 0; i < fine->completesize && fine->complete[i]; i++)
			free(fine->complete[i]);
		free(fine->complete);
	}

	/* FINALLY we can free the fineline_t itself */
	free(fine);
}

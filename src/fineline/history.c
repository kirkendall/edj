#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <fineline.h>

/* Set the size of the history, in lines.  This must be at least 1; passing
 * 0 or less just returns the current size.
 */
int fineline_history_lines(fineline_t *fine, int size)
{
	int	i;

	/* If too small, just return the current value */
	if (size < 1)
		return fine->historysize;

	/* Adjust the size.  This may involve freeing used history slots
	 * beyond the new size, or initializing unused slots if the size
	 * is extended.
	 */
	for (i = size; i < fine->historyused; i++)
		free(fine->history[i]);
	fine->history = realloc(fine->history, size * sizeof(char *));
	for (i = fine->historysize; i < size; i++)
		fine->history[i] = NULL;
	fine->historysize = size;
	if (fine->historyused > size)
		fine->historyused = size;
	return size;
}

/* Shift a copy of a given line into history at slot 1. (slot 0 is used for the
 * current line). Exception: If the line is empty or identical to the previous
 * line, do nothing.
 */
void fineline_history_add(fineline_t *fine, const char *line)
{
	/* If empty, do nothing */
	if (!line || !*line)
		return;

	/* Configured to not store history, then do nothing */
	if (fine->historysize == 1)
		return;

	/* If identical to history[1], do nothing */
	if (fine->historyused > 1 && !strcmp(fine->history[1], line))
		return;

	/* Shift the history.  If the history is full, this will involve
	 * freeing the last history slot before shifting.
	 */
	if (fine->historyused == fine->historysize)
		free(fine->history[--fine->historyused]);
	if (fine->historyused > 2)
		memmove(&fine->history[2], &fine->history[1], (fine->historyused - 1) * sizeof(char *));

	/* Store a copy of the new line */
	fine->history[1] = strdup(line);
	fine->historyused++;
}

/* Load history from a file. */
void fineline_history_load(fineline_t *fine, const char *historyname)
{
	char	*buf;
	int	ch;
	size_t	bufsize;
	size_t	bufused;
	FILE	*fp;

	/* Open the file.  If we can't do that, silently load nothing */
	fp = fopen(historyname, "r");
	if (!fp)
		return;

	/* Start with a modest-sized buffer */
	bufsize = 200;
	buf = (char *)malloc(bufsize);
	bufused = 0;

	/* Read bytes from the history, and add them to buf EXCEPT \r becomes
	 * \n, and \n causes buf to be added to history and the used is reset.
	 */
	while ((ch = getc(fp)) != EOF) {
		/* If buf is too small, enlarge it */
		if (bufused == bufsize) {
			bufsize *= 2;
			buf = (char *)realloc(buf, bufsize);
		}

		/* If \n then add it to history */
		if (ch == '\n') {
			buf[bufused] = '\0';
			fineline_history_add(fine, buf);
			bufused = 0;
		} else if (ch == '\r')
			buf[bufused++] = '\n';
		else
			buf[bufused++] = ch;
	}

	/* Clean up */
	fclose(fp);
	free(buf);
}

/* Write history to a file */
void fineline_history_save(fineline_t *fine, const char *historyname)
{
	FILE 	*fp;
	int	i;
	char	*scan;

	/* Open the file for writing.  If we can't write, do nothing */
	fp = fopen(historyname, "w");
	if (!fp)
		return;

	/* Write the oldest history first.  If history contains \n characters,
	 * convert them to \r so we can use \n as a delimiter.
	 */
	for (i = fine->historyused - 1; i >= 1; i++) {
		for (scan = fine->history[i]; *scan; scan++) {
			if (*scan == '\n')
				putc('\r', fp);
			else
				putc(*scan, fp);
		}
		putc('\n', fp);
	}

	/* Clean up */
	fclose(fp);
}

/* Set fine->line from a history line.  Positive delta moves back in history,
 * negative delta moves closer to the current line, zero jumps to current line.
 */
void fineline_history_show(fineline_t *fine, int delta)
{
	int	absolute = fine->historyshown + delta;

	/* Defend against overflow */
	if (delta == 0)
		absolute = 0;
	if (absolute < 0 || absolute >= fine->historyused)
		return;

	/* Make this be the shown item */
	fine->line = fine->history[absolute];
	fine->historyshown = absolute;
}

/* Copy the currently shown line into fine->history[0] so we can edit it.
 * Usually this function does nothing, but if the user has scrolled back in
 * history and starts editing a historic line, then this will copy it to the
 * current line editor buffer in fine->history[0]
 */
void fineline_history_edit(fineline_t *fine)
{
	size_t len;

	/* If already history[0] then do nothing */
	if (fine->historyshown == 0)
		return;

	/* Make history[0] be a copy of the indicated history line.  If the
	 * current history[0] buffer isn't big enough, enlarge it.
	 */
	len = strlen(fine->history[fine->historyshown]);
	if (len + 1 > fine->linesize) {
		fine->linesize = (len | 0xff) + 1;
		fine->history[0] = (char *)malloc(fine->linesize);
	}
	strcpy(fine->history[0], fine->history[fine->historyshown]);

	/* Move the cursor to the corresponding point within the line buffer */
	fine->historyshown = 0;
	fine->line = fine->history[0];
}

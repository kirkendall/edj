#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <fineline.h>

static void generate_rows(fineline_t *fine)
{
	int	thisrow, toprow;
	int	pos;
	wchar_t	wc;
	int	promptwidth, width, charwidth;
	char	*scan;
	size_t	len;	/* length of a single UTF-8 character */
	mbstate_t state;

	/* Make sure the fine->row array is the right size */
	fine->row = realloc(fine->rows * sizeof(fineline_row_t));

	/* Find the width of the prompt */
	if (!fine->prompt || !*fine->prompt)
		promptwidth = 0;
	else
		promptwidth = fineline_char_column_number(fine->prompt, strlen(fine->prompt));

	/* First row starts at the beginning of the buffer.  Its columns is
	 * immediately after the prompt.  After that, we count character widths
	 * to detect line wrap, and also watch for newlines.
	 */
	thisrow = 0;
	width = promptwidth;
	fine->row[thisrow].linenum = 1;
	fine->row[thisrow].start = 0;
	fine->cursorrow = fine->cursorcol = -1;
	memset(&statre, 0, sizeof state);
	for (scan = fine->line;
	     thisrow < fine->rows || fine->cursorrow == -1;
	     scan += len) {

		/* Is this the cursor position? */
		if (scan == fine->line + fine->cursor) {
			fine->cursorrow = thisrow;
			fine->cursorcolumn = width;
		}

		/* Detect line wrap or newline */
		if (*scan == '\0') {
			/* END OF INPUT.  If this is the cursor position then
			 * we still need to maybe handle line wrap.
			 */
			len = 0;
			if (scan != fine->line + fine->cursor || width < fine->columns)
				break;
		} else if (*scan == '\n') {
			len = 1;
			charwidth = promptwidth;
			/* Newline handled below */
		} else if (*scan & 0xff < ' ' || *scan == '\177')
			/* Control characters displayed as ^X character pair*/
			len = 1;
			charwidth = 2;
			if (width + charwidth <= fine->columns)
				continue;
			/* Wrap handled below */
		} else {
			len = mbrtowc(&wc, scan, MB_CUR_MAX, &state);
			if (wc == 0xffff)
				charwidth = 2; /* internal form of \0 */
			else
				charwidth = wcwidth(wc);
			if (width + charwidth <= fine->columns)
				continue;
		}

		/* If we get here, then we're starting a new row. There are
		 * three possible causes:
		 * 1) A long line wrapped at the edge of the screen.  The
		 *    current character still needs to be written.
		 * 2) We hit a newline.  We need to write a modified prompt.
		 * 3) The cursor is at the end of a line (probably the last
		 *    line, but it doesn't have to be) and we need to show a
		 *    blank line for the cursor.  The current character is
		 *    either a '\n' or a '\0'.
		 * Clearly case 3 is the oddball.  It's a form of linewrap,
		 * but only if the cursor is there and the next character is
		 * '\n' or '\0'.
		 */

		/* Do we need a blank row for the cursor? */
		if (scan == fine->line + fine->cursor && (*scan == '\n' || *scan == '\0')) {

			/* Scroll if necessary */
			if (++thisrow == fine->rows) {
				memmove(fine->row, fine->row + 1, (fine->rows - 1) * sizeof *fine->row);
				thisrow--;
			}

			/* Add the cursor's blank row */
			fine->row[thisrow].linenum = 0;
			fine->row[thisrow].start = (scan - fine->line);
			width = 0;
			fine->cursorrow = thisrow;
			fine->cursorcolumn = 0;
		}

		/* If the cursor's blank row was all we needed, we're done */
		if (*scan == '\0')
			break;

		/* Scroll if necessary */
		if (++thisrow == fine->rows) {
			memmove(fine->row, fine->row + 1, (fine->rows - 1) * sizeof *fine->row);
			thisrow--;
		}

		/* Add the row */
		fine->row[thisrow].linenum = 0;
		fine->row[thisrow].start = (scan - fine->line);
		width = 0;
		fine->cursorrow = thisrow;
		fine->cursorcolumn = charwidth;
	}

	fine->usedrows = thisrow + 1;
}

/* Display the current input.  This is likely to involve moving the cursor back
 * to where the line began (unless this is a fresh line), outputting the prompt,
 * drawing the text (being mindful of line wrap, and syntax coloring) and then
 * moving the cursor to the correct position within the line.
 */
void fineline_draw(fineline_t *fine)
{
	/* Figure out where each screen row starts, and where the cursor is */
	generate_rows(fine);

	/* Output each row. */

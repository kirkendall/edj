#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#define _XOPEN_SOURCE
#define __USE_XOPEN
#include <wchar.h>
#include <fineline.h>

static void generate_rows(fineline_t *fine)
{
	int	thisrow;
	wchar_t	wc;
	int	width, charwidth;
	char	*scan;
	size_t	len;	/* length of a single UTF-8 character */
	mbstate_t state;

	/* Make sure the fine->row array is the right size */
	fine->row = realloc(fine->row, fine->rows * sizeof(fineline_row_t));

	/* Find the width of the prompt */
	if (!fine->prompt || !*fine->prompt)
		fine->promptwidth = 0;
	else
		fine->promptwidth = fineline_char_column_number(fine->prompt, strlen(fine->prompt));

	/* First row starts at the beginning of the buffer.  Its columns is
	 * immediately after the prompt.  After that, we count character widths
	 * to detect line wrap, and also watch for newlines.
	 */
	thisrow = 0;
	width = fine->promptwidth;
	fine->row[thisrow].linenum = 1;
	fine->row[thisrow].start = 0;
	fine->cursorrow = fine->cursorcolumn = -1;
	memset(&state, 0, sizeof state);
	for (scan = fine->line;
	     thisrow < fine->rows || fine->cursorrow == -1;
	     width += charwidth, scan += len) {

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
			charwidth = fine->promptwidth;
			/* Newline handled below */
		} else if ((*scan & 0xff) < ' ' || *scan == '\177') {
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
#if 0
		fine->cursorrow = thisrow;
		fine->cursorcolumn = width;
#endif
		width = 0;
	}

	fine->usedrows = thisrow + 1;
}

/* Draw a single row */
static int draw_row(fineline_t *fine, int row)
{
	int	i;
	size_t	len;
	const char	*style;

	/* If we need to output a prompt, do that */
	if (fine->promptwidth > 0) {
		if (fine->row[row].linenum == 1) {
			/* First line, use the unmodified prompt */
			fine->text("prompt", fine->prompt, strlen(fine->prompt));
		} else if (fine->row[row].linenum > 1) {
			/* Show line number as the prompt.  If the line number
			 * is too wide for the prompt, then show #s.
			 */
			int numwidth, linenum;
			char *buf = malloc(fine->promptwidth + 1);
			for (numwidth = 1, linenum = fine->row[row].linenum;
			     linenum > 9;
			     numwidth++, linenum /= 10) {
			}
			if (numwidth > fine->promptwidth) {
				memset(buf, '#', fine->promptwidth);
				buf[fine->promptwidth] = '\0';
			} else
				sprintf(buf, "%-*d", fine->promptwidth, fine->row[row].linenum);
			fine->text("prompt", buf, fine->promptwidth);
			free(buf);
		}
	}

	/* If the first character has no style, then look back for the most
	 * recent style in the input and use that.  If there is no style at all
	 * then default to "normal".
	 */
	style = "normal";
	if (fine->style) {
		i = fine->row[row].start;
		style = fine->style[i];
		if (!style) {
			while (i >= 0 && (style = fine->style[i]) == NULL)
				i--;
			if (!style)
				style = "normal";
		}
	}

	/* Output the row's text.  Watch for changes of style */
	i = fine->row[row].start;
	while (fine->line[i] && (row  + 1 == fine->usedrows || i < fine->row[row + 1].start)) {
		/* Find the length of this style chunk */
		for (len = 1;
		     fine->line[i + len]
			&& i + len < fine->row[row + 1].start
			&& (!fine->style
			 || !fine->style[i + len]
			 || !strcmp(fine->style[i + len], style));
		     len++) {
		}

		/* Output it */
		fine->text(style, &fine->line[i], len);

		/* Prepare for the next chunk */
		i += len;
		if (fine->line[i] && fine->style)
			style = fine->style[i + len];
	}

	/* If this is the last row, then we need to return the column number */
	if (row == fine->usedrows - 1) {
		char *text = &fine->line[fine->row[row].start];
		return fine->promptwidth + fineline_char_column_number(text, strlen(text));
	}
	return 0;
}

/* Display the current input.  This is likely to involve moving the cursor back
 * to where the line began (unless this is a fresh line), outputting the prompt,
 * drawing the text (being mindful of line wrap, and syntax coloring) and then
 * moving the cursor to the correct position within the line.
 */
void fineline_draw(fineline_t *fine)
{
	int	row, col;

	/* Figure out where each screen row starts, and where the cursor is */
	generate_rows(fine);

	/* Output each row.  If the row needs a prompt, that will be added. */
	fine->home();
	for (row = 0; row < fine->usedrows; row++) {
		if (row < fine->usedrows - 1) {
			fine->down(1);
			fine->home();
		}
		col = draw_row(fine, row);
	}

	/* Move the visible cursor back where it belongs */
	if (fine->cursorrow < fine->usedrows - 1)
		fine->up(fine->usedrows - 1 - fine->cursorrow);
	if (fine->cursorcolumn < col)
		fine->left(col - fine->cursorcolumn);
	else if (fine->cursorcolumn > col)
		fine->right(fine->cursorcolumn - col);
}

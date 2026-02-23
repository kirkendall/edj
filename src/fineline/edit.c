#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <fineline.h>

/* edit.c -- The basic line editor. */

/* Perform an edit operation at the cursor.  Return 0 if successful, 1 if
 * a complete line is ready to process, or -1 if error.  An error typically
 * means you've bumped into the edge of the line being edited.
 */
int fineline_edit(fineline_t *fine, fineline_edit_t edit)
{
	size_t	len;
	int	col, lnum;
	char	*moved;

	switch (edit) {
	case FINELINE_INSERT:
		/* Toggle insert/overwrite mode */
		fine->replace = !fine->replace;
		break;

	case FINELINE_DELETE:
		/* Delete character at cursor, unless at end of input */
		if (fine->line[fine->cursor]) {
			/* If on a history line, copy it to current line */
			fineline_history_edit(fine);

			/* Find the byte-length of the character */
			len = fineline_char_size(fine->line + fine->cursor, 1);

			/* Move the characters after the cursor, and the '\0'
			 * at the end of the input line.
			 */
			memmove(&fine->line[fine->cursor], &fine->line[fine->cursor + len], strlen(&fine->line[fine->cursor + len]) + 1);
		}
		break;

	case FINELINE_BACK_SPACE:
		/* Delete character before cursor */
		fineline_edit(fine, FINELINE_LEFT);
		fineline_edit(fine, FINELINE_DELETE);
		break;

	case FINELINE_BACK_WORD:
		/* Delete to start of word */
		break;

	case FINELINE_BACK_LINE:
		/* Delete to start of line */
		moved = &fine->line[fine->cursor];
		fineline_edit(fine, FINELINE_HOME);
		if (moved != &fine->line[fine->cursor])
			memmove(fine->line + fine->cursor, moved, strlen(moved) + 1);
		break;

	case FINELINE_BACK_TAB:
		/* Delete spaces to previous tabstop */

	case FINELINE_TAB:	
		/* Insert spaces to next tabstop */
		col = fineline_char_column_number(fine->line, fine->cursor);
		do {
			fineline_edit_char(fine, L' ');
			col++;
		} while (col % fine->tabstop != 0);
		break;

	case FINELINE_HOME:	
		/* Move cursor to start of line.  If it's already at the start
		 * of the line, then move to the start of the edit buffer.
		 */
		len = fineline_char_line_offset(fine->line, fineline_char_line_number(fine->line, fine->cursor));
		if (len == fine->cursor)
			len = 0;
		fine->cursor = len;
		break;

	case FINELINE_END:	
		/* Move cursor to end of line.  If already at the end of the
		 * line, then move to the end of the edit buffer.
		 */
		if (fine->line[fine->cursor] == '\n')
			fine->cursor = strlen(fine->line);
		else {
			while (fine->line[fine->cursor] && fine->line[fine->cursor] != '\n')
				fine->cursor++;
		}
		break;

	case FINELINE_LEFT:
		/* Move cursor left */
		fine->cursor = fineline_char_delta(fine->line, fine->cursor, -1);
		break;

	case FINELINE_RIGHT:	
		/* Move cursor right */
		fine->cursor = fineline_char_delta(fine->line, fine->cursor, 1);
		break;

	case FINELINE_UP:
		/* Move back in history, or up within current input */

		/* First try moving up in the edit buffer */
		lnum = fineline_char_line_number(fine->line, fine->cursor);
		len = fineline_char_line_offset(fine->line, lnum - 1);
		if (len || lnum == 1) {
			/* Yes, just move the cursor */
			col = fineline_char_column_number(fine->line, fine->cursor);
			fine->cursor = fineline_char_at_column(fine->line + len, col, NULL) - fine->line;
		} else
			/* Otherwise move back in history */
			fineline_history_show(fine, 1);
		break;

	case FINELINE_DOWN:	
		/* Move forward in history, or down within current input */

		/* First try moving down in the edit buffer */
		lnum = fineline_char_line_number(fine->line, fine->cursor);
		len = fineline_char_line_offset(fine->line, lnum + 1);
		if (len) {
			/* Yes, just move the cursor */
			col = fineline_char_column_number(fine->line, fine->cursor);
			fine->cursor = fineline_char_at_column(fine->line + len, col, NULL) - fine->line;
		} else
			/* Otherwise move forward in history */
			fineline_history_show(fine, -1);
		break;

	case FINELINE_ENTER:
		/* If the line is known to be incomplete, then just add a
		 * newline to the input.
		 */
		/*!!!*/

		/* else fall through to save */
	case FINELINE_SAVE:
		/* If we were looking at a history line (without editing it
		 * yet) then make it the current line before we do anything
		 * else.
		 */
		fineline_history_edit(fine);

		/* Save the current line to history */
		fineline_history_add(fine, fine->line);

		/* return the current line */
		if (!fine->runner || (*fine->runner)(fine->line) != 0)
			return 1;
		*fine->line = '\0';
		fine->cursor = 0;
		break;

	case FINELINE_EXIT:
		/* Fail if line is not empty */
		if (*fine->line)
			return -1;

		/* else fall though to quit... */
	case FINELINE_QUIT:
		/* return 2, indicating quit-no-processing */
		return 2;

	/* The case following indicate special conditions */
	case FINELINE_PAGE_DOWN:/* scroll forward */
	case FINELINE_PAGE_UP:	/* scroll back */
	case FINELINE_RESIZE:	/* the window was resized */
		return 0;

	/* These aren't meant to be used */
	case FINELINE_MIN:
	case FINELINE_MAX:
		abort();
	}

	return 0;
}


/* Insert text at the cursor.  "len" is the bytecount of UTF-8 data, not
 * the number of characters.
 */
void fineline_edit_text(fineline_t *fine, const char *text, size_t len)
{
	size_t	buflen;

	/* Start editing the shown line */
	fineline_history_edit(fine);

	/* If necessary, enlarge the edit buffer */
	buflen = strlen(fine->line);
	if (buflen + len + 1 > fine->linesize) {
		fine->linesize = ((buflen + len) | 0xff) + 1;
		fine->line = fine->history[0] = realloc(fine->line, fine->linesize);
	}

	/* Shift the end of the line to make room.  Even if the cursor is at
	 * the end of the line, we still want to shift the '\0' there.
	 */
	memmove(&fine->line[fine->cursor + len], &fine->line[fine->cursor], strlen(&fine->line[fine->cursor]) + 1);

	/* Copy the new text */
	strncpy(&fine->line[fine->cursor], text, len);

	/* Move the cursor to the end of the new text */
	fine->cursor += len;
}

/* Insert a single character at the cursor */
void fineline_edit_char(fineline_t *fine, wchar_t ch)
{
	char buf[MB_CUR_MAX + 1];
	size_t len;

	/* Convert it to a string */
	len = wctomb(buf, ch);

	/* insert it */
	fineline_edit_text(fine, buf, len);
}

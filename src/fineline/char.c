/* char.c */
#include <stdlib.h>
#include <string.h>
#define _XOPEN_SOURCE
#define __USE_XOPEN
#include <wchar.h>
#include <limits.h>
#include <fineline.h>

#define ASSUME_UTF8

/* This file defines functions for dealing with UTF-8 character data.  In
 * particular, it deals with varying byte-length of characters, and varying
 * display width of characters.
 */

/* Return the number of bytes for a given number of characters. */
size_t fineline_char_size(const char *text, int charcount)
{
	size_t	total, single;
	const char *curs;
	mbstate_t state;

	memset(&state, 0, sizeof state);
	for (total = 0, curs = text; --charcount >= 0; total += single) {
		single = mbrtowc(NULL, curs, MB_LEN_MAX, &state);
		if (single <= 0)
			return single;
		curs += single;
	}
	return total;
}

/* Return the line number of the cursor within buf.  The first line is 0.
 * If the cursor isn't in buf then it returns -1.
 */
int fineline_char_line_number(const char *buf, size_t cursor)
{
	int	line;
	size_t offset;
	for (line = 0, offset = 0; buf[offset] && offset != cursor; offset++)
		if (buf[offset] == '\n')
			line++;
	if (offset == cursor)
		return line;
	return -1;
}

/* Return the offset to the start of a given line within buf.  The first line
 * is 0.  If the requested line exists, then it returns 0.
 */
size_t fineline_char_line_offset(const char *buf, int line)
{
	const char *scan = buf;
	if (line < 0)
		return 0;
	while (line > 0 && *scan)
		if (*scan++ == '\n')
			line--;
	if (line > 0)
		return 0;
	return scan - buf;
}

/* Return the column number of a given offset into a line buffer.  This knows
 * about newlines, but not about the width of the prompt, or terminal wrapping.
 */
int fineline_char_column_number(const char *buf, int cursor)
{
	int	col;
	wchar_t	wc;
	int	len;
	const char	*pos;
	mbstate_t state;

	/* Count widths out to the cursor */
	memset(&state, 0, sizeof state);
	for (pos = buf, col = 0; *pos && pos < buf + cursor; pos += len) {
		len = mbrtowc(&wc, pos, MB_CUR_MAX, &state);
		if (len <= 0)
			return -1;
		if (wc == '\n' || wc == '\r')
			col = 0;
		else
			col += wcwidth(wc);
	}
	return col;
}

/* Return a pointer to a given column, assuming "line" points to the start
 * of a line.  If '\0' or '\n' is encountered before that, then it'll end
 * prematurely, and store the actual column number at *refcol (unless refcol
 * is NULL).
 *
 * This function doesn't know about the prompt width, or terminal columns.
 * It only deals with "logical" lines.
 */
const char *fineline_char_at_column(const char *line, int wantcol, int *refcol)
{
	wchar_t	wc;
	size_t	wclen;
	int	col;
	const char *prev;
	mbstate_t state;

	prev = line;
	memset(&state, 0, sizeof state);
	for (col = 0; col < wantcol && *line && *line != '\n'; col += wcwidth(wc))  {
		/* Fetch the next character */
		wclen = mbrtowc(&wc, line, MB_LEN_MAX, &state);
		if (wclen <= 0)
			return NULL;
		prev = line;
		line += wclen;
	}

	/* If we went too far, then return the previous character.  This can
	 * happen if the requested column is in the middle of a double-width
	 * character.
	 */
	if (col > wantcol) {
		line = prev;
		col--;
	}

	/* Return it */
	if (refcol)
		*refcol = col;
	return line;
}

/* Move within a buffer.  Delta can be -1/1 to move back/forward 1 character */
int fineline_char_delta(const char *buf, int cursor, int delta)
{
	/* Handle the 0 case */
	if (delta == 0)
		return cursor;

	/* Forward is easy */
	if (delta > 0) {
		/* Fail if at end */
		if (!buf[cursor])
			return cursor;
		return cursor + fineline_char_size(buf + cursor, delta);
	}

#ifdef ASSUME_UTF8
	/* In UTF-8, all multibyte sequences have "10" in their top two bits
	 * of their extra bytes (i.e., not the first byte) and we need to skip
	 * over those.
	 */
	do {
		if (cursor == 0)
			break;
		cursor--;
	} while ((buf[cursor] & 0xc0) == 0x80);

#else
	/* Backward, we need to start scanning from the start of the buffer */
#endif
	return cursor;
}

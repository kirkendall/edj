#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#define _XOPEN_SOURCE
#define __USE_XOPEN
#include <wchar.h>
#include <fineline.h>

/* Allocate an image */
static fineline_image_t *alloc_image(fineline_t *fine)
{
	fineline_img_t *img;

	/* Allocate the image_t.  Make the row array be the same height as
	 * the screen, since that's the maximum we'll ever display
	 */
	img = malloc(sizeof *img);
	img->rowsize = fine->rows;
	img->row = calloc(img->rowsize, sizeof *img->row);
	img->style = calloc(img->rowsize, sizeof *img->style);
	img->toprow = 0;
	img->height = 0;
	img->width = fine->columns;
	img->thisrow = 0;
	img->thissize = 0;
	img->thiscol = 0;
	img->cursorrow = img->cursorcol = -1;

	return image;
}

/* Free an image */
static void free_image(fineline_image_t *img)
{
	int i;

	/* Free any row and style buffers */
	for (i = 0; i < img->height; i++) {
		free(img->row[i]);
		free(img->style[i]);
	}

	/* Free the row and style tables themselves */
	free(img->row);
	free(img->style);

	/* Free the image */
	free(img);
}


/* Start a new row.  If the row table is full, then scroll it. */
static void new_row(fineline_image_t *img)
{
	/* If there's still room in the row, this is easy.  Otherwise we
	 * need to scroll.
	 */
	img->thisrow++;
	if (img->thisrow >= img->rowsize) {
		free(img->row[0]);
		free(img->style[0]);
		if (img->rowsize > 1) {
			memmove(&img->row[0], &img->row[1], (img->rowsize - 1) * sizeof *img->row);
			memmove(&img->style[0], &img->style[1], (img->rowsize - 1) * sizeof *img->row);
		}
		img->toprow++;
		img->thisrow--;
		img->thissize = 0;
		img->thiscol = 0;
	}
}

/* Append multibyte characters to an image's current row, and return its byte
 * length.  This stops adding when it hits maxlen, or when it finds a character
 * that is too wide to fit on the current row.  This expands the row buffer
 * if necessary.
 *
 * It also watches for the editing cursor position within the text, and stores
 * the found position in fine->cursorrow and fine->cursorcol.
 */
static size_t append(fineline_t *fine, fineline_image_t *img, char *utf8, size_t maxlen, char *style)
{
	wchar_t	ch;
	int	chlen, totallen, i;
	int	chwidth;
	mbstate_t state;

	memset(&state, 0, sizeof state);
	for (totallen = 0; totallen < maxlen; totallen += chlen, utf8 += chlen) {
		/* Get the next character */
		chlen = mbtowc(&ch, utf8, MB_CUR_MAX);

		/* Check its width -- stop if too wide */
		chwidth = wcwidth(ch);
		if (img->col + chwidth > img->width)
			break;

		/* If the cursor is here, remember that.  This doesn't handle
		 * the case where the cursor is located at the end of a row;
		 * that's handled elsewhere.  Also, this function is used for
		 * adding text that doesn't come directly from the line buffer
		 * (e.g., the prompt, or "^X" for control characters), so those
		 * need to be handled elsewhere too.
		 */
		if (fine->line[fine->cursor] == utf8) {
			img->cursorrow = img->toprow + img->height;
			img->cursorcol = img->col;
		}

		/* Add to the row.  If the row buffer is too small, expand it */
		if (img->pos + chlen + 1 > img->rowsize) {
			img->rowsize += 256;
			img->row[img->height] = realloc(img->row[img->height], img->rowsize);
			img->style[img->height] = realloc(img->style[img->height], img->rowsize);
		}
		memcpy(img->row[img->height] + img->pos, utf8, chlen);
		for (i = 0; i < chlen; i++)
			img->style[img->height][img->pos] = style;
	}

	return (size_t)totallen;
}


/* This generates an image, basically by splitting the input into rows.
 * Most importantly, it converts fine->line and fine->style into a
 * fineline_image_t's ->row  and->style arrays.  Those arrays are indexed
 * by row; their elements are indexed by bytes of UTF-8 text.  So
 * ->row[rownum][bytenum] is a byte of a UTF-8 character, and
 * ->style[rownum][bytenum] is a string identifying the color and other
 * attributes of the character.  
 */
static fineline_image_t *generate_image(fineline_t *fine)
{
	int	thisrow;
	wchar_t	wc;
	int	width, charwidth;
	char	*scan;
	size_t	len;	/* byte length of a single UTF-8 character */
	mbstate_t state;
	fineline_image_t *image;

	/* Allocate the image_t. */
	image = alloc_image(sizeof *image);

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
	fineline_image_t *image;

	/* Generate a new image */
	image = generate_image(fine);

	/* Are we updating an old image? */
	if (fine->image) {
		/* Yes -- move the cursor back to the start of the first row */
		fine->up(fine->image->cursorrow - fine->image->toprow);
		fine->home();

		/* If the top row is moved (due to scrolling) then insert or
		 * delete rows.  Also adjust the old image to match.
		 */
		if (image->toprow != fine->image->toprow) {
			fine->scroll(fine->image->toprow - image->toprow);
			/*!!! Adjust the old image */
		}

		/* For each row of the new image... */
		for (row = 0; row < image->height; row++) {
			/* If the image has changed, redraw it */
			if (!fine->image->row || fine->image->row) {
		}

		/* If there were rows in the old image that aren't needed now,
		 * then erase them.
		 */
		/*!!!*/
	}

	/* Move the visible cursor back where it belongs */
	if (fine->cursorrow < fine->usedrows - 1)
		fine->up(fine->usedrows - 1 - fine->cursorrow);
	if (fine->cursorcolumn < col)
		fine->left(col - fine->cursorcolumn);
	else if (fine->cursorcolumn > col)
		fine->right(fine->cursorcolumn - col);

	/* Free the old image, store the new image */
	if (fine->image) {
		for (row = 0; row < fine->image->rowsize; i++)
			if (fine->image->row[row])
				free(fine->image->row[row]);
		free(fine->image);
	}
	fine->image = image;
}

/* This contains code to generate an image of the edit buffer.  An image
 * consists of...
 *  - rows of UTF-8 text.
 *  - rows of character attributes.
 *  - the number of rows that scrolled off the top of the screen.
 *  - the row and column of the cursor 
 *
 * The text filling those rows comes from...
 *  - The prompt.
 *  - The input.
 *  - Generated text such as carat-letter for control characters
 *  - Partial completions (shown inline)
 *  - Hints (shown inline)
 *  - Name completion list (shown on extra rows below input)
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#define _XOPEN_SOURCE
#define __USE_XOPEN
#include <wchar.h>
#include <fineline.h>

#if 1
#define DUMP
#else
#define DUMP dump(__LINE__, img);
static void dump(int line, fineline_image_t *img)
{
	int	r;
	fprintf(stderr, "%d:img@0x%lx:{hxw=%dx%d, thisrow/col=%d/%d, rowpos=%d, rowsize=%d, cursor=%dx%d}\r\n", line, (long)img, img->height, img->width, img->thisrow, img->thiscol, img->rowpos, img->rowsize, img->cursorrow, img->cursorcol);
	for (r = 0; r < img->usedrows - 1; r++)
		fprintf(stderr, "img->row[%d] = \"%s\"\r\n", r, img->row[r]);
	fprintf(stderr, "img->row[%d] = \"%.*s\"\r\n\n", r, img->rowpos, img->row[r]);
}
#endif

/* Allocate an image */
static fineline_image_t *alloc_image(fineline_t *fine)
{
	fineline_image_t *img;

	/* Allocate the image_t.  Make the row array be the same height as
	 * the screen, since that's the maximum we'll ever display
	 */
	img = malloc(sizeof *img);
	img->height = fine->rows;
	img->width = fine->columns;
	img->row = calloc(img->height, sizeof *img->row);
	img->style = calloc(img->height, sizeof *img->style);
	img->rowwidth = calloc(img->height, sizeof *img->rowwidth);
	img->toprow = 0;
	img->thisrow = 0;
	img->rowsize = 0;
	img->thiscol = 0;
	img->rowpos = 0;
	img->cursorrow = img->cursorcol = -1;

	return img;
}

/* Free an image */
void fineline_image_free(fineline_image_t *img)
{
	int i;

	/* Free any row and style buffers */
	for (i = 0; i < img->height; i++) {
		if (img->row[i])
			free(img->row[i]);
		if (img->style[i])
			free(img->style[i]);
	}

	/* Free the row and style tables themselves */
	free(img->row);
	free(img->style);
	free(img->rowwidth);

	/* Free the image */
	free(img);
}


/* Start a new row.  If the row table is full, then scroll it. */
static void start_new_row(fineline_image_t *img)
{
	/* Mark the end of the row's text with a NUL byte */
	img->row[img->thisrow][img->rowpos] = '\0';

	/* Remember the width of this row */
	img->rowwidth[img->thisrow] = img->thiscol;

	/* If there's still room in the row table, this is easy.  Otherwise we
	 * need to scroll.
	 */
	img->thisrow++;
	if (img->thisrow >= img->height) {
		free(img->row[0]);
		free(img->style[0]);
		if (img->height > 1) {
			memmove(&img->row[0], &img->row[1], (img->rowsize - 1) * sizeof *img->row);
			memmove(&img->style[0], &img->style[1], (img->rowsize - 1) * sizeof *img->row);
			memmove(&img->rowwidth[0], &img->rowwidth[1], (img->rowsize - 1) * sizeof *img->rowwidth);
		}
		img->row[img->height - 1] = NULL;
		img->style[img->height - 1] = NULL;
		img->rowwidth[img->height - 1] = 0;
		img->toprow++;
		img->thisrow--;
		if (img->cursorrow >= 0)
			img->cursorrow--;
	}
	img->rowsize = 0;
	img->thiscol = 0;
	img->rowpos = 0;
}

/* Add a wide character to a row.  If there isn't room in the row, then
 * add a new row and add it there.  Note that the text is passed as both a
 * wchar_t and a char*.
 *
 * This function assumes only printable characters will be submitted.
 * Unprintable characters such as newline or tab must be handled elsewhere.
 * It also doesn't try to figure out where the cursor should be displayed.
 */
static void add_char(fineline_image_t *img, wchar_t wc, const char *text, int charSize, const char *style)
{
	int	charWidth;	/* width of the character, in columns */

	/* Get its width. If not known, skip it */
	charWidth = wcwidth(wc);
	if (charWidth < 0)
		return;

	/* If it doesn't fit, or it's zero width at the exact end of the row,
	 * then start a new row.
	 */
	if (img->thiscol + (charWidth ? charWidth : 1) > img->width)
		start_new_row(img);

	/* If the row buffer is too small, enlarge it */
	if (img->rowpos + charSize >= img->rowsize) {
		img->rowsize += 32;
		img->row[img->thisrow] = realloc(img->row[img->thisrow], img->rowsize * sizeof(*img->row));
		img->style[img->thisrow] = realloc(img->style[img->thisrow], img->rowsize * sizeof(*img->style));
	}

	/* Add the character to the row */
	while (charSize-- > 0) {
		img->style[img->thisrow][img->rowpos] = style;
		img->row[img->thisrow][img->rowpos++] = *text++;
	}

	/* Adjust the column trackers */
	img->thiscol += charWidth;
	img->virtualcol += charWidth;
}


/* Add spaces to the line, mostly for tabstops */
static void add_spaces(fineline_image_t *img, int nspaces, const char *style)
{
	wchar_t	space = ' ';

	while (nspaces-- > 0)
		add_char(img, space, " ", 1, style);

}

/* Append multibyte characters to an image.  This is used for text other than
 * the input buffer (e.g., use this for prompt or hint); it does not watch for
 * the cursor position or anything * fancy like that.  All characters are
 * assumed to be printable.
 */
static void add_string(fineline_image_t *img, const char *text, const char *style)
{
	wchar_t	wc;
	int	chsize;
	mbstate_t state;

	memset(&state, 0, sizeof state);
	for (; *text; text += chsize) {
		/* Get the next character */
		chsize = mbrtowc(&wc, text, MB_CUR_MAX, &state);
		if (chsize <= 0)
			return;

		/* Add it to the image */
		add_char(img, wc, text, chsize, style);
	}
}

/* Add a numbered version of the prompt. */
static void add_line_prompt(fineline_image_t *img, int width, int space, int lineno)
{
	char	prompt[20];

	/* If no main prompt, then no prompts on extra lines either */
	if (width == 0)
		return;

	/* Generate the prompt */
	if (space && width > 2)
		snprintf(prompt, sizeof prompt, "%17d> ", lineno);
	else if (width > 1)
		snprintf(prompt, sizeof prompt, "%18d>", lineno);
	else /* width == 1 */
		strcpy(prompt, ">");

	/* All characters in the generated prompt have a width of 1.  Use the
	 * rightmost portion of the prompt string.
	 */
	add_string(img, prompt + strlen(prompt) - width, "prompt");
}

/* This gets called when the cursor position is detected.  It records the
 * row/column of the cursor, and also displays hints/completions.
 */
static void found_cursor(fineline_t *fine, fineline_image_t *img, int plain)
{
	/* If the row is full then the cursor would be off the
	 * right edge of the screen.  We can't have that!
	 * Force a linewrap first.
	 */
	if (img->thiscol == img->width)
		start_new_row(img);

	/* Remember position */
	img->cursorrow = img->thisrow;
	img->cursorcol = img->thiscol;

	/* If "plain" mode, that's all */
	if (plain)
		return;

	/* Show partial completion text, if any */
	if (fine->completesame && *fine->completesame)
		add_string(img, fine->completesame, "complete");

	/* Show hint text, if any */
	if (fine->hint && *fine->hint)
		add_string(img, fine->hint, "hint");

#if 0
	/* For debugging, show cursor column */
	{
		char buf[100];
		sprintf(buf, "[%d@%d]", img->cursorcol, img->cursorrow);
		add_string(img, buf, "cursor");
	}
#endif
}

/* This generates an image, basically by splitting the input into rows.
 * Most importantly, it converts fine->line and fine->style into a
 * fineline_image_t's ->row  and->style arrays.  Those arrays are indexed
 * by row; their elements are indexed by bytes of UTF-8 text.  So
 * ->row[rownum][bytenum] is a byte of a UTF-8 character, and
 * ->style[rownum][bytenum] is a string identifying the color and other
 * attributes of the character.  
 */
fineline_image_t *fineline_image(fineline_t *fine, int plain)
{
	wchar_t	wc;
	char	*scan;
	const char	**style;
	size_t	len;	/* byte length of a single UTF-8 character */
	mbstate_t state;
	fineline_image_t *img;
	int	promptwidth;
	int	promptspace;	/* whether prompt ends with a space */
	char	tmpstr[10];
	const wchar_t	carat = '^';
	int	lineno = 1;

	/* Allocate the image_t. */
	img = alloc_image(fine);
DUMP

	/* Add the main prompt.  Remember its width, so we can make subsequent
	 * lines' prompts use the same width.
	 */
	if (!fine->prompt || !*fine->prompt) {
		promptwidth = 0;
		promptspace = 0;
	} else {
		add_string(img, fine->prompt, "prompt");
		promptwidth = img->thiscol;
		promptspace = (fine->prompt[strlen(fine->prompt) - 1] == ' ');
	}
DUMP

	/* Add characters from the input buffer, watching for special characters
	 * such as newlines, tabs, and control characters.  When we hit the
	 * cursor position, remember its row and column and also maybe display
	 * hints or partial completions.
	 */
	memset(&state, 0, sizeof state);
	for (scan = fine->line, style = fine->style;
	     img->thisrow - img->toprow < img->height || img->cursorrow == -1;
	     scan += len) {

		/* Get the character */
		len = mbrtowc(&wc, scan, MB_CUR_MAX, &state);
		if (len <= 0)
			break;

		/* Is this the cursor position? */
		if (scan == fine->line + fine->cursor)
			found_cursor(fine, img, plain);
DUMP

		/* Add this character.  Some characters are special */
		if (wc == '\n') {
			/* Newlines force a new row, and also output a prompt */
			start_new_row(img);
			add_line_prompt(img, promptwidth, promptspace, ++lineno);
		} else if (wc == '\t') {
			/* Tabs are converted into a variable number of spaces */
			add_spaces(img, fine->tabstop - img->virtualcol % fine->tabstop, NULL);
		} else if (wc < ' ' || wc == 0x7f) {
			/* ASCII control characters show as uppercase ^X */
			strcpy(tmpstr, "^");
			add_char(img, carat, tmpstr, 1, "ctrl");
			wc ^= '@';
			*tmpstr = (char)wc;
			add_char(img, wc, tmpstr, 1, "ctrl");
		} else if (wc >= 0x80 && wc < 0xa0) {
			/* Extended control characters show as lowercase ^x */
			strcpy(tmpstr, "^");
			add_char(img, carat, tmpstr, 1, "ctrl");
			wc -= 32;
			*tmpstr = (char)wc;
			add_char(img, wc, tmpstr, 1, "ctrl");
		} else if (wc == 0xffff) {
			/* U+FFFF is used internally for NUL, shown as ^@ */
			strcpy(tmpstr, "^");
			add_char(img, carat, tmpstr, 1, "ctrl");
			wc = '@';
			*tmpstr = (char)wc;
			add_char(img, wc, tmpstr, 1, "ctrl");
		} else {
			/* normal character */
			add_char(img, wc, scan, len, style ? *style : NULL);
		}

		/* Increment the style scanner, unless it's NULL */
		if (style)
			style += len;
	}

	/* If we never found the cursor then it must be at the very end of
	 * the input.
	 */
	if (img->cursorrow == -1)
		found_cursor(fine, img, plain);
DUMP

	/* Mark the end of the last row with a NUL byte */
	if (img->row[img->thisrow]) {
		img->row[img->thisrow][img->rowpos] = '\0';
		img->rowwidth[img->thisrow] = img->thiscol;
	}

	/* The number of rows is 1 more than the current row */
	img->usedrows = img->thisrow + 1;
DUMP

	return img;
}

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#define _XOPEN_SOURCE
#define __USE_XOPEN
#include <wchar.h>
#include <fineline.h>

// Draw a row.  Leave the cursor on the row.
static void draw_row(fineline_t *fine, fineline_image_t *image, int row)
{
	size_t len, pos;
	char	*text = image->row[row];
	const char **style = image->style[row];

	/* Defend against NULL */
	if (!text)
		return;

	/* For each chunk of same-style text... */
	for (pos = 0; text[pos]; pos += len) {
		/* Count the length of this chunk */
		for (len = 1; text[pos + len] && style[pos + len] == style[pos]; len++) {
		}

		/* Write it */
		fine->text(style[pos], &text[pos], len);
	}
}


/* Display the current input.  This is likely to involve moving the cursor back
 * to where the line began (unless this is a fresh line), outputting the prompt,
 * drawing the text (being mindful of line wrap, and syntax coloring) and then
 * moving the cursor to the correct position within the line.
 */
void fineline_draw(fineline_t *fine)
{
	int	row;
	fineline_image_t *img;

	/* Generate a new image */
	img = fineline_image(fine);

	/* Are we updating an old image? */
	if (fine->image) {
		/* Yes -- move the cursor back to the start of the first row */
#if 0
		fine->up(fine->image->cursorrow - fine->image->toprow);
#endif
		fine->home();

		/* If the top row is moved (due to scrolling) then insert or
		 * delete rows.  Also adjust the old image to match.
		 */
		if (img->toprow != fine->image->toprow) {
			fine->scroll(fine->image->toprow - img->toprow);
			/*!!! Adjust the old image */
		}

		/* For each row of the new image... */
		for (row = 0; row < img->usedrows; row++) {
			/* If the row has changed, redraw it */
			draw_row(fine, img, row);
			if (row + 1 < img->usedrows) {
				fine->home();
				fine->down(1);
			}
		}

		/* If there were rows in the old image that aren't needed now,
		 * then erase them.
		 */
		for (; row < fine->image->usedrows; row++) {
			fine->clear();
			fine->down(1);
		}
	} else {
		/* Draw all rows */
		for (row = 0; row < img->usedrows; row++) {
			/* If the row has changed, redraw it */
			draw_row(fine, img, row);
			if (row + 1 < img->usedrows) {
				fine->home();
				fine->down(1);
			}
		}
	}

	/* Move the visible cursor back where it belongs */
	if (img->cursorrow < fine->usedrows - 1)
		fine->up(fine->usedrows - 1 - img->cursorrow);
	if (img->cursorcol < img->thiscol)
		fine->left(img->thiscol - img->cursorcol);
	else if (img->cursorcol > img->thiscol)
		fine->right(img->cursorcol - img->thiscol);

	/* Free the old image, store the new image */
	if (fine->image)
		fineline_image_free(fine->image);
	fine->image = img;
}

/* Move the cursor to the line after the input.  This function should be called
 * immediately after a line has been entered.
 */
void fineline_draw_after(fineline_t *fine)
{
	/* We want to move the edit cursor to the end of the input, but not
	 * past it.  If the line is empty then the cursor is already on the
	 * '\0' marking the end of input, and we should leave it there, but
	 * in all other cases we want to move the cursor to the last character
	 * before the '\0'.  Since characters may be multi-byte, this is
	 * non-trivial.
	 */
	if (*fine->line) {
		fine->cursor = strlen(fine->line);
		fine->cursor = fineline_char_delta(fine->line, fine->cursor, -1);
	}

	/* Draw it like that. */
	fineline_draw(fine);

	/* Move to the start of the next line */
	fine->down(1);
	fine->home();
}


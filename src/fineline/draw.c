#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <fineline.h>

/* Reasons why a chunk might end */
typedef enum {
	BECAUSE_END,	/* We reached the end of the input buffer */
	BECAUSE_NEWLINE,/* We encountered a newline as part of the input */
	BECAUSE_STYLE,	/* The character style/colors has changed */
	BECAUSE_HINT,	/* We reached the cursor, and have a hint to show */
	BECAUSE_WRAP	/* The line wrapped because it is wider than the screen */
} because_t;


/* Do a line wrap.  This involves writing a secondary prompt which is derived
 * from the main prompt.  linenum should be the line number, or 0 if the same
 * logical input line just wrapped at the edge of the screen.
 */
static void wrap(fineline_t *fine, int linenum)
{
	int	width, spaces, punct;
	size_t	len;
	char	buf[40];


	/* Wrap */
	(*fine->clear)();
	(*fine->down)(1);
	(*fine->home)();

	/* If continuation of a line due to screen wrap, then we're done */
	if (linenum == 0)
		return;

	/* Examine the prompt */
	len = strlen(fine->prompt);
	width = fineline_char_column_number(fine->prompt, len);
	for (spaces = 0; spaces < len && fine->prompt[len - spaces - 1] == ' '; spaces++) {
	}
	if (spaces < len && ispunct(fine->prompt[len - spaces - 1]))
		punct = fine->prompt[len - spaces - 1];
	else
		punct = ':';

	/* Generate number+punct prompt */
	if (linenum == 0)
		snprintf(buf, sizeof buf, "%*c%-*c", (int)len, ' ', spaces + 1, punct);
	else
		snprintf(buf, sizeof buf, "%*d%-*c", (int)len, linenum, spaces + 1, punct);

	/* Output it, limiting the width to match prompt and aligning punct */
	(*fine->text)("prompt", buf + len + spaces + 1, width);
}

/* Display the current line.  This is likely to involve moving the cursor back
 * to where the line began (unless this is a fresh line), outputting the prompt,
 * drawing the text (being mindful of line wrap, and syntax coloring) and then
 * moving the cursor to the correct position within the line.
 */
void fineline_draw(fineline_t *fine)
{
	int row, linenum, col, width;
	int cursorrow, cursorcol;
	int offset, span;
	const char *style;
	int promptwidth = fineline_char_column_number(fine->prompt, strlen(fine->prompt));
	because_t why;


	/* If there's a draw() function, give it a try */
	if (fine->draw && !(*fine->draw)(fine))
		return;

	/* Output the prompt */
	(*fine->home)();
	(*fine->text)("prompt", fine->prompt, strlen(fine->prompt));
	row = 0;
	linenum = 1;
	col = promptwidth;
	cursorrow = cursorcol = -1;

	/* If line coloring starts with NULL, assume that means "normal" */
	style = "normal";
	if (fine->colors && fine->colors[0] == NULL)
		fine->colors[0] = "normal";

	/* Output each chunk of the line, being mindful of coloring and
	 * line wrap.  Detect when we reach the cursor.  If there's a hint,
	 * show it immediately after the cursor.
	 */
	for (offset = 0; fine->line[offset]; offset += span) {
		/* Get the style of this chunk.  If a chunk gets split due to
		 * a line wrap, then fine->colors[offset] might be NULL.
		 */
		if (fine->colors && fine->colors[offset])
			style = fine->colors[offset];

		/* Find the end of this chunk.  The chunk ends at the end
		 * of the line, or a change of style/colors, or the cursor if
		 * if there's a hint or completesame, or line wrap.  So it's
		 * a bit complicated.
		 */
		for (span = 0; ; span += fineline_char_size(&fine->line[offset + span], 1)) {
			/* end of the line? */
			if (fine->line[offset + span] == '\0') {
				why = BECAUSE_END;
				break;
			}

			/* newline */
			if (fine->line[offset + span] == '\n') {
				why = BECAUSE_NEWLINE;
				break;
			}

			/* change of style/colors? */
			if (fine->colors && fine->colors[offset + span] && fine->colors[offset + span] != style) {
				why = BECAUSE_STYLE;
				break;
			}

			/* cursor?  Only marks end if there's hint or completesame */
			if (offset + span == fine->cursor) {
				cursorrow = row;
				cursorcol = col;
				if (fine->hint || fine->completesame) {
					why = BECAUSE_HINT;
					break;
				}
			}

			/* wrapped line? */
			width = fineline_char_column_number(&fine->line[offset + span], 0);
			if (col + width > fine->columns) {
				why = BECAUSE_WRAP;
				break;
			}
		}

		/* Output the chunk */
		(*fine->text)(style, &fine->line[offset], span);

		/* Handle special situations */
		switch (why) {
		case BECAUSE_STYLE:
		case BECAUSE_END:
			/* No special processing needed */
			break;

		case BECAUSE_NEWLINE: 
			/* Start the next line */
			linenum++;
			wrap(fine, linenum);
			row++;
			col = 0;
			break;

		case BECAUSE_HINT:
			/* Output the completion or hint.  Note that this might
			 * cause wrap.
			 */
			break;

		case BECAUSE_WRAP:
			/* The line wrapped. */
			wrap(fine, 0);
			row++;
			col = 0;
			break;
		}
	}

	/* Move the terminal's cursor back to fineline's cursor */
	if (cursorrow > row)
		(*fine->up)(cursorrow - row);
	if (cursorcol > col)
		(*fine->right)(cursorcol - col);
	else if (cursorcol < col)
		(*fine->left)(col - cursorcol);
}

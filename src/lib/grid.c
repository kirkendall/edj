#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <locale.h>
#include <assert.h>
#include <edj.h>

/* NOTE: The edj_is_table() function is defined in is.c */

/* If json appears to be a table, then output it as a table/grid.  */
void edj_grid(edj_t *json, edjformat_t *format)
{
	edj_t	*explain, *row, *col, *cell;
	char	*text;
	int	wdata, width, c, i, line;
	int	rowheight, cellheight;
	size_t	size;
	char	hdrpad;
	char	number[40];
	char	*bar, *barface, *cellface;
	int	*widths, *pad;
	edjformat_t tweaked;

	/* If not a table, return 0 */
	if (!edj_is_table(json))
		return;

	/* If format is NULL then use the default format */
	if (!format)
		format = &edj_format_default;
	tweaked = *format;
	format = &tweaked;
	if (!format->fp) /* Default output is stdout */
		format->fp = stdout;
	if (!isatty(fileno(format->fp))) /* Disable color if not a tty */
		format->color = 0;

	/* Collect statistics about the columns.  For deferred arrays,
	 * we may want to limit the number of rows that we check.
	 */
	explain = NULL;
	if (edj_is_deferred_array(json)) {
		/* Get the limit on explain rows to check for deferred arrays.
		 * If >=1 then only scan those rows.
		 */
		int deferexplain = 0;
		edj_t *jdef = edj_by_key(edj_config, "deferexplain");
		if (jdef && jdef->type == EDJ_NUMBER)
			deferexplain = edj_int(jdef);
		if (deferexplain > 0) {
			/* Collect statistics about columns in the first few rows */
			for (row = edj_first(json);
			     deferexplain > 0 && row;
			     deferexplain--, row = edj_next(row)) {
				explain = edj_explain(explain, row, 0);
			}
			edj_break(row);
		}
	}
	if (!explain) {
		/* Collect column statistics across all rows */
		for (row = edj_first(json); row; row = edj_next(row))
			explain = edj_explain(explain, row, 0);
	}

	/* Allocate arrays to hold padding tips. */
	c = edj_length(explain);
	widths = calloc(c, sizeof(int));
	pad = calloc(c, sizeof(int));

	/* If any column's key is wider than their data, expand the column. */
	rowheight = 1;
	for (c = 0, col = edj_first(explain); col; c++, col = edj_next(col)) {
		/* For columns that can contain arrays or objects, make sure
		 * it's wide enough to show "[array]" or "{object}".
		 */
		text = edj_text_by_key(col, "type");
		width = widths[c] = edj_int(edj_by_key(col, "width"));
		if ((!strcmp(text, "array") || !strcmp(text, "table")) && width < 7)
			width = widths[c] = 7;
		else if ((!strcmp(text, "object") || !strcmp(text, "any")) && width < 8)
			width = widths[c] = 8;

		/* For nullable columns, if null isn't displayed as "" then
		 * make sure the column is wide enough for it.
		 */
		if (*format->null && edj_is_true(edj_by_key(col, "nullable"))) {
			int w = edj_mbs_width(format->null);
			if (width < w)
				width = widths[c] = w;
		}

		/* Expand the column if key is wide */
		text = edj_text_by_key(col, "key");
		wdata = edj_mbs_width(text);
		if (wdata > width) {
			pad[c] = wdata - width;
		}

		/* If this is the highest key, then increase rowheight */
		cellheight = edj_mbs_height(text);
		if (cellheight > rowheight)
			rowheight = cellheight;
	}

	/* Decide whether to color the output */
	hdrpad = format->color ? ' ' : '_';
	bar = format->graphic ? "\xe2\x94\x82" : "|";

	/* Output the column headings.  If rowheight > 1 we need to do this
	 * separately for each line of the headings.
	 */
	for (line = 0; line < rowheight; line++) {
		/* Colorize? */
		cellface = (line == rowheight - 1 ? "gridhead" : "_gridhead");

		/* Output this line of this column's heading */
		for (c = 0, col = edj_first(explain); col; c++, col = edj_next(col)) {
			/* Get this line of the heading, and its width */
			width = widths[c] + pad[c];
			text = edj_text_by_key(col, "key");
			size = edj_mbs_line(text, line, NULL, &text, &wdata);
			if (size > 0)
				size--; /* remove newline */

			/* Output the key as a column heading */
			edj_user_printf(format, cellface, "");
			for (i = 0; i < (width - wdata + 1) / 2; i++)
				edj_user_ch(hdrpad);
			if (size > 0)
				edj_user_printf(format, cellface, "%.*s", (int)size, text);
			for (; i < (width - wdata); i++)
				edj_user_ch(hdrpad);

			/* Bar between columns */
			if (!edj_is_last(col))
				edj_user_printf(format, cellface, "%s", bar);
		}

		/* End the line */
		edj_user_printf(format, "normal", "\n");
	}

	/* For each row... */
	for (row = edj_first(json); row && !edj_interrupt; row = edj_next(row)) {
		/* Find the height of the tallest cell.  All cells are 1
		 * except for strings that contain newlines.
		 */
		rowheight = 1;
		for (c = 0, col = edj_first(explain); col; c++, col = edj_next(col)) {
			cell = edj_by_key(row, edj_text_by_key(col, "key"));
			if (cell && cell->type == EDJ_STRING) {
				cellheight = edj_mbs_height(cell->text);
				if (cellheight > rowheight)
					rowheight = cellheight;
			}
		}

		/* For each line of the row... */
		for (line = 0; line < rowheight; line++) {

			/* Choose the color */
			barface = (line == rowheight - 1 ? "gridline" : "_gridline");
			cellface = (line == rowheight - 1 ? "gridcell" : "_gridcell");

			/* Output this line of the row */
			for (c = 0, col = edj_first(explain); col; c++, col = edj_next(col)) {
				/* Fetch the cell */
				cell = edj_by_key(row, edj_text_by_key(col, "key"));
				/* Get its text and width.  Since strings can
				 * be multi-line, they're handled differently.
				 */
				if (cell && cell->type == EDJ_STRING) {
					size = edj_mbs_line(cell->text, line, NULL, &text, &wdata);
					if (size > 0)
						size--; /* remove newline */
				} else if (line > 0) {
					/* All non-strings are 1 row high */
					size = 0;
					text = "";
					wdata = 0;
				} else {
					if (!cell || cell->type == EDJ_NULL)
						text = format->null;
					else if (cell->type == EDJ_ARRAY)
						text = edj_is_table(cell) ? "[table]" : "[array]";
					else if (cell->type == EDJ_OBJECT)
						text = "{object}";
					else if (cell->type == EDJ_NUMBER && !cell->text[0] && cell->text[1] == 'i')
						snprintf(text = number, sizeof number, "%d", EDJ_INT(cell));
					else if (cell->type == EDJ_NUMBER && !cell->text[0] && cell->text[1] == 'd')
						snprintf(text = number, sizeof number, "%.*g", format->digits, EDJ_DOUBLE(cell));
					else /* boolean or non-binary number */
						text = cell->text;

					/* Get widths */
					size = strlen(text);
					wdata = edj_mbs_width(text);
				}

				/* width of this column */
				width = widths[c];

				/* If a wide column heading dictates that we
				 * need extra padding (more than data width),
				 * then output half of that extra padding now.
				 */
				if (pad[c] >= 2)
					edj_user_printf(format, cellface, "%*c", pad[c] >> 1, ' ');

				/* Output the cell. Alignment depends on type */
				if (cell && cell->type == EDJ_STRING) {
					/* left-justify strings */
					if (size > 0)
						edj_user_printf(format, cellface, "%.*s", (int)size, text);
					if (width - wdata > 0)
						edj_user_printf(format, cellface, "%*c", width - wdata, ' ');
				} else if (cell && cell->type == EDJ_NUMBER) {
					/* right-justify numbers */
					if (width - wdata > 0)
						edj_user_printf(format, cellface, "%*c", width - wdata, ' ');
					edj_user_printf(format, cellface, "%s", text);
				} else {
					/* center everything else */
					if (width - wdata > 0)
						edj_user_printf(format, cellface, "%*c", (width - wdata + 1) >> 1, ' ');
					edj_user_printf(format, cellface, "%s", text);
					if (width - wdata > 1)
						edj_user_printf(format, cellface, "%*c", (width - wdata) >> 1, ' ');
				}

				/* If a wide column heading dictates that we
				 * need extra padding, then output the second
				 * half of that extra padding now.
				 */
				if (pad[c] >= 1)
					edj_user_printf(format, cellface, "%*c", (pad[c] + 1) >> 1, ' ');

				/* Delimiter between columns */
				if (!edj_is_last(col))
					edj_user_printf(format, barface, "%s", bar);

			}
			edj_user_printf(format, "normal", "\n");
		}
	}

	/* Discard the "explain" data */
	edj_free(explain);
	free(widths);
	free(pad);

	/* Done! */
	return;
}


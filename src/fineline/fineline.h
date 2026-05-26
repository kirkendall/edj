/* These are the cursor keys used by fineline.  They start well beyond the
 * range of Unicode characters.
 */
typedef enum {
	/* The lowest key code is very deliberately outside Unicode range */
	FINELINE_MIN = 0x200000,/* Lowest key code, beyond Unicode range */

	FINELINE_INSERT,	/* toggle insert/overwrite mode */
	FINELINE_DELETE,	/* delete character at cursor */
	FINELINE_BACK_SPACE,	/* delete character before cursor */
	FINELINE_BACK_WORD,	/* delete to start of word */
	FINELINE_BACK_LINE,	/* delete to start of line */
	FINELINE_BACK_TAB,	/* delete to start of tab */
	FINELINE_TAB,		/* move to next tabstop */

	/* Cursor keypad keys */
	FINELINE_HOME,		/* move cursor to start of line */
	FINELINE_END,		/* move cursor to end of line */
	FINELINE_LEFT_WORD,	/* move cursor to previous word */
	FINELINE_RIGHT_WORD,	/* move cursor to next word */
	FINELINE_LEFT,		/* move cursor left */
	FINELINE_RIGHT,		/* move cursor right */
	FINELINE_UP,		/* move cursor up, or back in history */
	FINELINE_DOWN,		/* move cursor down, or forward in history */

	/* Shifted keypad keys, used to select text.  These must must exactly
	 * match the list of keypad keys above, with "HOME" first.
	 */
	FINELINE_S_HOME,	/* select to start of line */
	FINELINE_S_END,		/* select to end of line */
	FINELINE_S_LEFT_WORD,	/* select to previous word */
	FINELINE_S_RIGHT_WORD,	/* select to next word */
	FINELINE_S_LEFT,	/* select left */
	FINELINE_S_RIGHT,	/* select right */
	FINELINE_S_UP,		/* select up */
	FINELINE_S_DOWN,	/* select down */

	/* The following indicate special conditions */
	FINELINE_PAGE_DOWN,	/* scroll forward */
	FINELINE_PAGE_UP,	/* scroll back */
	FINELINE_ENTER,		/* Process the line or add newline */
	FINELINE_EXIT,		/* Process the line or add newline */
	FINELINE_SAVE,		/* Save the line to history without processing */
	FINELINE_QUIT,		/* Expect no more lines (exit program) */
	FINELINE_RESIZE,	/* the window was resized */

	/* If application-specific codes are needed, add them after this */
	FINELINE_MAX		/* Highest key code */

} fineline_edit_t;


typedef struct {
	char	**row;		/* dynamic char *row[] array */
	const char ***style;	/* dynamic char **style[] array */
	int	*rowwidth;	/* dynamic rowwidth[] array */
	int	height;		/* dimension of the row[] and style[] arrays */
	int	width;		/* number of columns per row */
	int	usedrows;	/* number of used rows */
	int	toprow;		/* number of rows scrolled off the top */
	int	thisrow;	/* current row number */
	int	thiscol;	/* current column number */
	int	virtualcol;	/* column ignoring line wraps */
	int	rowpos;		/* where to add next character */
	int	rowsize;	/* allocated size of ->row[->thisrow] */
	int	cursorrow;	/* row containing cursor (after subtracting toprow) */
	int	cursorcol;	/* column containing the cursor */
} fineline_image_t;

/* This contains all of the info needed to draw the current line. */
typedef struct fineline_s {
	void	*context;	/* Info to help with name completion */
	void	*window;	/* Info to help draw text */

	/* Options controlling the behavior or appearance */
	int	dynamic;	/* Boolean: Return a strdup() of the line? */
	int	matchparen;	/* Boolean: Highlight unmatched parenthesis? */
	int	tabstop;	/* Integer: width of tabstops (normally 4) */

	/* Values describing the terminal size, in single-width characters */
	int	columns, rows, usedrows;

	/* Status of keystroke input */
	int	quote;	/* treat next char as literal, even if <Esc> */

	/* data describing the line as currently shown */
	fineline_image_t *image;

	/* Callbacks related to drawing a line. "before" is called at the start
	 * of inputting a line, and may be used to adjust "columns" and "rows",
	 * or switch input to "raw" mode so we get individual keystrokes.
	 * "after" is called after inputting a line, and may be used to switch
	 * back to "noraw" mode. "draw" is used to draw the line in a very
	 * interface-specific way.  The "up", "down", "left", "right", and
	 * "home" functions move the cursor for displaying text, or actually
	 * displaying a cursor on the screen.  "text" draws text.  "scroll"
	 * inserts blank rows at the cursor position, or (for negative "n")
	 * deletes rows, scrolling the bottom part of the screen down.
	 *
	 * Most interfaces won't define a "draw" function, and instead depend
	 * on the cursor motion functions and "text".  If an interface does
	 * define a "draw" function, that function can return a non-zero value
	 * to cause the cursor motion and "text" functions to be called anyway.
	 */
	void	(*before)(struct fineline_s *fine);
	void	(*after)(struct fineline_s *fine);
	int	(*draw)(struct fineline_s *fine);
	void	(*up)(struct fineline_s *fine, int n);
	void	(*left)(struct fineline_s *fine, int n);
	void	(*scroll)(struct fineline_s *fine, int n);
	void	(*home)(struct fineline_s *fine);
	void	(*clear)(struct fineline_s *fine);
	void	(*text)(struct fineline_s *fine, const char *style, const char *text, size_t size);

	/* This is a callback, invoked when a complete line has been entered.
	 * This "line" may contain newline characters, for multiline commands.
	 * If this function is NULL, or if it exists but returns a non-zero
	 * value, then the fineline_
	 */
	int	(*runner)(const char *line);

	/* These store the history */
	char	**history;	/* List of line buffers, [0] is current line */
	int	historysize;	/* Number of lines allocated for history */
	int	historyused;	/* Number of lines used for history */
	int	historyshown;	/* Which line we're looking at now. */

	/* The remaining sections are listed in the order that they'd typically
	 * be displayed while a line is being edited.
	 */

	const char *prompt;	/* Main prompt string */

	char	*line;		/* Line buffer -- usually history[0] */
	size_t	linesize;	/* size of the history[0] buffer */
	int	replace;	/* Boolean: replacing? else inserting */
	int 	cursor;		/* byte-index into "line" of the cursor */

	const char *(*colorer)(struct fineline_s *);
	const char **style;
	const size_t stylesize;	/* dimension of style */

	void (*completer)(struct fineline_s *);
	char	*completesame;	/* Common part of name completion */
	char	**complete;	/* All name completions */
	int	completesize;	/* number of allocated completion slots */

	char *(*hinter)(struct fineline_s *);
	const char *hint;	/* Other text such as function parameters */

} fineline_t;


/* fineline.c -- low level allocate and free */
fineline_t *fineline_alloc(void *context);
void fineline_free(fineline_t *);

/* tty.c -- functions that specifically let it work on a plain tty.  The
 * fineline_tty() function reads a line and returns it as a dynamic string,
 * similar to readline().
 */
char *fineline_tty(fineline_t *fine, const char *prompt);
char *fineline(const char *prompt);

/* char.c -- Functions for handling multibyte characters */
size_t fineline_char_size(const char *text, int charcount);
int fineline_char_line_number(const char *buf, size_t cursor);
size_t fineline_char_line_offset(const char *buf, int line);
int fineline_char_column_number(const char *buf, int cursor);
const char *fineline_char_at_column(const char *line, int wantcol, int *refcol);
int fineline_char_delta(const char *buf, int cursor, int delta);

/* history.c -- Functions for manipulating or accessing history.  Since the
 * current line is always in fineline->history[0], the size of history must
 * be at least 1.
 */
int fineline_history_lines(fineline_t *fine, int size);
void fineline_history_add(fineline_t *fine, const char *line);
void fineline_history_load(fineline_t *fine, const char *historyname);
void fineline_history_save(fineline_t *fine, const char *historyname);
void fineline_history_show(fineline_t *fine, int delta);
void fineline_history_edit(fineline_t *fine);

/* edit.c -- The basic line editor. */
int fineline_edit(fineline_t *fine, fineline_edit_t edit);
void fineline_edit_text(fineline_t *fine, const char *text, size_t len);
void fineline_edit_char(fineline_t *fine, wchar_t ch);

/* hint.c -- Registers a function for doing hinting.  The function will be
 * called when the cursor is at the end of a non-empty line, and it should
 * return NULL for no hint, or a new hint as a string.
 */
void fineline_hint_hook(char *(*fn)(fineline_t *fine));

/* complete.c */
void fineline_complete_hook(char *(*fn)(fineline_t *fine));
void fineline_complete_item(fineline_t *fine, const char *item, const char *group);

/* draw.c */
void fineline_draw(fineline_t *fine, int plain);
void fineline_draw_after(fineline_t *fine);

/* image.c */
fineline_image_t *fineline_image(fineline_t *fine, int plain);
void fineline_image_free(fineline_image_t *img);

/******************************************************************************/
/* ncursesw support.  This is implemented in the header to avoid making the   */
/* library be dependent on ncursesw in programs that don't use ncursesw.      */

#ifdef NCURSES_WIDE
# define fineline_curses(fine, prompt) fineline_wcurses((w), fine, prompt)
# define fineline_curses_alloc() fineline_wcurses_alloc(stdscr)
# ifdef FINELINE_CURSES


/* Pointer to application-specific function for setting window drawing
 * attributes for a given name such as "prompt".  If unset (NULL) then
 * simple defaults will be used.
 */
void (*fineline_wattrbyname)(WINDOW *w, const char *name);


static void fineline_wcurses_before(fineline_t *fine)
{
	int	y, x;
	WINDOW *w = (WINDOW *)fine->window;

	/* Detect window resizing */
	getmaxyx(w, &y, &x)
	if (y != fine->rows || x != fine->columns) {
		fine->rows = y;
		fine->columns = x;
		fineline_edit(fine, FINELINE_RESIZE);
	}
}

static void fineline_wcurses_after(fineline_t *fine)
{
	WINDOW *w = (WINDOW *)fine->window;

	wrefresh(w);
}

static void fineline_wcurses_up(fineline_t *fine, int n)
{
	int	y, x;
	WINDOW *w = (WINDOW *)fine->window;
	getyx(w, &y, &x)
	wmove(w, y - n, x);
}

static void fineline_wcurses_left(fineline_t *fine, int n)
{
	int	y, x;
	WINDOW *w = (WINDOW *)fine->window;
	getyx(w, &y, &x)
	wmove(w, y, x - n);
}

static void fineline_wcurses_scroll(fineline_t *fine, int n)
{
	WINDOW *w = (WINDOW *)fine->window;
	winsdelln(w, n);
}

static void fineline_wcurses_home(fineline_t *fine)
{
	int	y, x;
	WINDOW *w = (WINDOW *)fine->window;
	getyx(w, &y, &x)
	wmove(w, y, 0);
}

static void fineline_wcurses_clear(fineline_t *fine)
{
	WINDOW *w = (WINDOW *)fine->window;
	wclrtoeol(w);
}

static void fineline_wcurses_text(fineline_t *fine, const char *style, const char *text, size_t size)
{
	int	y, x, y2, x2, len;
	char	*scan;
	WINDOW *w = (WINDOW *)fine->window;

	/* Convert byte count to UTF-8 character count */
	for (len = 0, scan = text; scan < &text[size]; )
		if ((*scan++ & 0xc0) != 0x80)
			len++;

	/* If fineline_wattrbyname is non-NULL, then call it to set the
	 * text style; otherwise, use A_NORMAL for everything except a few
	 * styles that are built into the fineline library.
	 */
	if (fineline_wattrbyname)
		(*fineline_wattrbyname)(w, style);
	else if (!strcmp(style, "prompt"))
		wattrset(w, A_BOLD);
	else if (!strcmp(style, "hint") || !strcmp(style, "complete"))
		wattrset(w, A_DIM);
	else
		wattrset(w, A_NORMAL);

	/* Add the characters */
	waddnstr(w, text, len);
}

/* Receive keystrokes, and process them.  This is somewhat tricky since we
 * could have multiple windows that that are inputting lines at the same time,
 * and ncursesw isn't good about keeping them separate.
 */
char *fineline_wcurses(WINDOW *w, fineline_t *fine, const char *prompt)
{
}

/* Allocate a fineline_t, and initialize it for ncursesw */
fineline_t *fineline_wcurses_alloc(WINDOW *w)
{
	fineline_t *fine = fineline_alloc();
	fineline->window = (void *)w;
	fineline->before = fineline_wcurses_before;
	fineline->up = fineline_wcurses_up;
	fineline->left = fineline_wcurses_left;
	fineline->scroll = fineline_wcurses_scroll;
	fineline->home = fineline_wcurses_home;
	fineline->clear = fineline_wcurses_clear;
	fineline->text = fineline_wcurses_text;
	return fine;
}

# else
extern char *fineline_wcurses(WINDOW *w, fineline_t *fine, const char *prompt);
extern fineline_t *fineline_wcurses_alloc(WINDOW *w);
extern void (*wattrbyname)(WINDOW *w, const char *name);
# endif
#endif

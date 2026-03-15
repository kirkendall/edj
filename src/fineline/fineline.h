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
	FINELINE_HOME,		/* move cursor to start of line */
	FINELINE_END,		/* move cursor to end of line */
	FINELINE_LEFT,		/* move cursor left */
	FINELINE_RIGHT,		/* move cursor right */
	FINELINE_UP,		/* move back in history (up arrow key) */
	FINELINE_DOWN,		/* move forward in history (down arrow key) */

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

typedef enum {
	FINELINE_BOLD = 1,
	FINELINE_DIM = 2,
	FINELINE_UNDERLINED = 4,
	FINELINE_ITALIC = 8,
	FINELINE_LINETHRU = 16,
	FINELINE_STANDOUT = 32,
	FINELINE_NO_ATTRIBUTES = 16384
} fineline_attributes_t;

typedef struct {
	char	*color;		/* name of the role ("prompt", "string", etc.)*/
	char	*fg;		/* name of the foreground color */
	char	*bg;		/* name of the background color */
	fineline_attributes_t at;/* bitmap of other attributes such as bold */
} fineline_color_t;

/* draw2.c */
typedef struct {
	int	linenum;	/* line number, or 0 if continuation of a long line */
	int	start;		/* Where the line starts */
} fineline_row_t;

/* draw3.c */
typedef struct {
	char	**row;		/* dynamic char *row[] array */
	char	***style;	/* dynamic char **style[] array */
	int	rowsize;	/* dimension of the row[] and style[] arrays */
	int	toprow;		/* number of rows scrolled off the top */
	int	height;		/* row that we're writing into now */
	int	width;		/* number of columns per row */
	int	col;		/* current column with the current row */
	int	cursorrow, cursorcol;	/* where to display the cursor */
} fineline_image_t;

/* This contains all of the info needed to draw the current line. */
typedef struct fineline_s {
	void	*context;	/* Info to help with name completion */

	/* Options controlling the behavior or appearance */
	int	dynamic;	/* Boolean: Return a strdup() of the line? */
	int	matchparen;	/* Boolean: Highlight unmatched parenthesis? */
	int	tabstop;	/* Integer: width of tabstops (normally 4) */

	/* Values describing the terminal size, in single-width characters */
	int	columns, rows, usedrows;
	fineline_row_t *row; /* array of row info */
	int	cursorrow, cursorcolumn;
	fineline_image_t *image; /* data describing the line as currently shown */

	/* Callbacks related to drawing a line. "before" is called at the start
	 * of inputting a line, and may be used to adjust "columns" and "rows",
	 * or switch input to "raw" mode so we get individual keystrokes.
	 * "after" is called after inputting a line, and may be used to switch
	 * back to "noraw" mode. "draw" is used to draw the line in a very
	 * interface-specific way.  The "up", "down", "left", "right", and
	 * "home" functions move the cursor for displaying text, or actually
	 * displaying a cursor on the screen.  "text" draws text.  "addrow"
	 * inserts a blank row at the cursor position, scrolling the bottom
	 * part of the screen down.
	 *
	 * Most interfaces won't define a "draw" function, and instead depend
	 * on the cursor motion functions and "text".  If an interface does
	 * define a "draw" function, that function can return a non-zero value
	 * to cause the cursor motion and "text" functions to be called anyway.
	 */
	void	(*before)(struct fineline_s *);
	void	(*after)(struct fineline_s *);
	int	(*draw)(struct fineline_s *fine);
	void	(*up)(int n);
	void	(*down)(int n);
	void	(*left)(int n);
	void	(*right)(int n);
	void	(*home)(void);
	void	(*clear)(void);
	void	(*text)(const char *style, const char *text, size_t size);
	int	(*scroll)(int n);

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
	int	promptwidth;

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

/* color.c */
int fineline_color(const char *color);
void fineline_color_set(int color, fineline_attributes_t on, fineline_attributes_t off, const char *fg, const char *bg);
void fineline_color_hook(int *(*fn)(fineline_t *fine));

/* draw.c */
void fineline_draw(fineline_t *fine);

#include <sys/ioctl.h>
#include <wchar.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <locale.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <assert.h>
#include "fineline.h"

/* This implements generic tty input.  In the interest of avoiding dependencies
 * it has hardcoded escape sequences appropriate for an ANSI terminal.
 */

/* Initially this contains the name of the terminfo attribute for each key.
 * Once the terminal has been loaded, the attribute names are replaced with
 * the attribute values.
 *
 * In addition to the keys listed here, '\n' or '\r' is FINELINE_ENTER,
 * <Esc> is FINELINE_EXIT, '\t' is FINELINE_TAB, '\b' is FINELINE_BACK_SPACE,
 * and '\177' is either * FINELINE_BACK_SPACE or FINELINE_DELETE depending on
 * stty settings.
 */
static struct {
	fineline_edit_t	key;	/* fineline key code */
	char esc[5];		/* escape sequence for the key */
	size_t	length;		/* length of esc, computed in fineline_active() */
} keys[] = {
	{FINELINE_BACK_SPACE,	"\177"},	/* delete or backspace */
	{FINELINE_DELETE,	"\033[3~"},	/* delete character at cursor */
	{FINELINE_INSERT,	"\033[2~"},	/* toggle insert/overwrite mode */
	{FINELINE_BACK_SPACE,	"\b"},
	{FINELINE_TAB,		"\t"},		/* insert spaces to next tab */
	{FINELINE_BACK_TAB,	"\033[Z"},	/* delete to start of tab */
	{FINELINE_HOME,		"\033[H"},	/* move to start of line */
	{FINELINE_HOME,		"\033OH"},	/* move to start of line */
	{FINELINE_END,		"\033[F"},	/* move to end of line */
	{FINELINE_END,		"\033OF"},	/* move to end of line */
	{FINELINE_LEFT,		"\033[D"},	/* move left */
	{FINELINE_LEFT,		"\033OD"},	/* move left */
	{FINELINE_RIGHT,	"\033[C"},	/* move right */
	{FINELINE_RIGHT,	"\033OC"},	/* move right */
	{FINELINE_UP,		"\033[A"},	/* move up */
	{FINELINE_UP,		"\033OA"},	/* move up */
	{FINELINE_DOWN,		"\033[B"},	/* move down */
	{FINELINE_DOWN,		"\033OB"},	/* move down */
	{FINELINE_PAGE_UP,	"\033[5~"},	/* scroll page back */
	{FINELINE_PAGE_DOWN,	"\033[6~"},	/* scroll page forward */
	{FINELINE_ENTER,	"\r"},		/* process line or add newline */
	{FINELINE_ENTER,	"\n"},		/* process line or add newline */
	{FINELINE_SAVE,		"\033"},	/* Save to history but don't process */
	{FINELINE_QUIT,		"\004"}		/* exit without processing the line */
};

static int doingresize;
static void resized(int signum)
{
	struct winsize w;

	ioctl(0, TIOCGWINSZ, &w);
	printf("rows=%d, columns=%d\r\n", w.ws_row, w.ws_col);
	doingresize = 1;
}


/* Switches terminal between raw and normal mode.  Timeout is tenths of a 
 * second, and is normally 0 (no timeout) while waiting for a keystroke,
 * 2 or 3 for 0.2 seconds timeout while receiving multibyte characters or
 * escape sequences.  Passing -1 switches to "normal" mode, anything else is
 * "raw" mode. "Raw" is used while reading a line, and normal is used while
 *  processing that line, and after exiting.
 */
static void fineline_active(int timeout)
{
	static struct termios origstty;
	static int haveorig;
	struct termios stty;

	/* Have we haven't done so yet, fetch the original settings now */
	if (!haveorig) {
		/* Get the original settings */
		tcgetattr(0, &origstty);
		haveorig = 1;
		if (timeout < 0)
			return;

		/* Adjust the backspace key, in keys[0] */
		keys[0].esc[0] = origstty.c_cc[VERASE];

		/* Also, the first time we want to configure window resize
		 * detection.  We also simulate a resize event, so we can
		 * get the initial screen size but we DON'T want to return
		 * FINELINE_RESIZE.
		 */
		signal(SIGWINCH, resized);
		resized(0);
		doingresize = 0;
	}

	/* If supposed to revert to normal, do that */
	if (timeout < 0) {
		tcsetattr(0, TCSANOW, &origstty);
		return;
	}

	/* Otherwise, get the current settings and switch to raw/noecho */
	tcgetattr(0, &stty);
	stty.c_iflag &= ~(INLCR | IGNCR | IXON | ISIG);
#ifdef IUTF8
	stty.c_iflag |= IUTF8;
#endif
	stty.c_oflag &= ~OPOST;
	stty.c_lflag &= ~(ICANON|ECHO);
	stty.c_cc[VMIN] = 1;
	stty.c_cc[VTIME] = timeout;
	tcsetattr(0, TCSANOW, &stty);
}

/* Count the keys that could match.  If there's exactly 1 match then return
 * its index into keys[], otherwise return -1 if can't be a cursor key, or
 * -2 if more characters are needed to determine.
 */
static int count_key_matches(char *buf, size_t bufused, int timedout)
{
	int	k, match, partial;

	/* First time, count the lengths of esc */
	if (keys[0].length == 0) {
		for (k = 0; k < sizeof keys / sizeof *keys; k++)
			keys[k].length = strlen(keys[k].esc);
	}

	/* Count complete and partial matches */
	for (k = partial = 0, match = -1; k < sizeof keys / sizeof *keys; k++) {
		if (keys[k].length > bufused) {
			if (!strncmp(keys[k].esc, buf, bufused))
				partial++;
		} else {
			if (!strncmp(keys[k].esc, buf, keys[k].length)) {
				if (match < 0 || keys[match].length < keys[k].length)
					match = k;
			}
		}
	}

	/* If there's only one possible match, or if there's at least one
	 * complete match and we timed out, then use it
	 */
	if (match >= 0 && (timedout || partial == 0))
		return match;
	return partial > 0 ? -2 : -1;
}

/* Check whether the buffer starts with a complete character.  For ASCII that's
 * easy, but UTF-8 is a bit trickier.  Returns the character's byte length, or
 * 0 if incomplete.
 */
static size_t count_char_matches(char *buf, size_t bufused)
{
	static size_t lentable[] = {
		1,1,1,1,1,1,1,1, /* 0x00-0x7f ASCII */
		1,1,1,1, 	 /* 0x80-0xbf extra byte of UTF-8 */
		2,2, 		 /* 0xc0-0xdf first byte of 2-byte UTF-8 */
		3, 		 /* 0xe0-0xef first byte of 3-byte UTF-8 */
		4		 /* 0xf0-0xff first byte of 4-byte UTF-8 */
	};
	size_t len = lentable[(buf[0] >> 4) & 0xf];
	if (len <= bufused)
		return len;
	return 0;
}

/* Read a key and return it.  The key is either a Unicode codepoint, or a
 * fineline_edit_t key code.  The returned value is a "long" instead of
 * "wchar_t" because some OSes use UTF-16 for wchar_t instead of full Unicode.
 */
static long fineline_get_key(void)
{
	static char	buf[20];
	static ssize_t	bufused, charused;
	ssize_t	nread;
	mbstate_t state;
	wchar_t	wc;
	int	timedout, k;

	/* If a resize occurred between lines, return FINELINE_RESIZE now */
	if (doingresize) {
		doingresize = 0;
		return FINELINE_RESIZE;
	}

	/* Make sure we have at least one byte in buf */
	if (bufused == 0) {
		fineline_active(0);
		nread = read(0, buf + bufused, sizeof buf - bufused);
		if (nread < 1) {
			if (doingresize) {
				doingresize = 0;
				return FINELINE_RESIZE;
			}
			return -1;
		}
		bufused = nread;
	}

	/* Check for a cursor key or a complete character */
	timedout = 0;
	for (;;) {
		/* Do we have a cursor key? */
		k = count_key_matches(buf, bufused, timedout);
		if (k >= 0) {
			/* Shift the buffer, return the key code */
			if (keys[k].length < bufused)
				memmove(buf, buf + keys[k].length, bufused - keys[k].length);
			bufused -= keys[k].length;
			return keys[k].key;
		}

		/* If no partial keys, do we have a complete character? */
		if (k != -2) {
			charused = count_char_matches(buf, bufused);
			if (charused > 0) {
				/* ASCII is easy.  For UTF-8, parse it */
				if (charused == 1)
					wc = (wchar_t)buf[0];
				else {
					memset(&state, 0, sizeof state);
					(void)mbrtowc(&wc, buf, MB_CUR_MAX, &state);
				}

				/* Shift the buffer */
				if (charused < bufused)
					memmove(buf, buf + charused, bufused - charused);
				bufused -= charused;

				/* Return the wchar_t */
				return wc;
			}
		}

		/* Read more data, with timeout this time */
		fineline_active(2);
		nread = read(0, buf + bufused, sizeof buf - bufused);
		if (nread < 0) {
			if (doingresize) {
				doingresize = 0;
				return FINELINE_RESIZE;
			}
			return -1;
		}
		bufused += nread;
		timedout = (nread == 0);
	}
}

/* Called to move the cursor up */
static void ttyup(int n)
{
	fprintf(stderr, "\033[%dA", n);
}

/* Called to move the cursor down */
static void ttydown(int n)
{
	fprintf(stderr, "\033[%dB", n);
}

static void ttyleft(int n)
{
	fprintf(stderr, "\033[%dD", n);
}

static void ttyright(int n)
{
	fprintf(stderr, "\033[%dC", n);
}

static void ttyhome(void)
{
	fputc('\r', stderr);
}

static void ttyclear(void)
{
	fputs("\033[K", stderr);
}

static void ttytext(const char *style, const char *text, size_t size)
{
	fwrite(text, 1, size, stderr);
}


/* Allocate a fineline_t for reading from a tty in a generic way */
fineline_t *fineline_tty_alloc()
{
	/* Allocate it */
	fineline_t *fine = fineline_alloc(NULL);

	/* Initialize it */
	fine->up = ttyup;
	fine->down = ttydown;
	fine->left = ttyleft;
	fine->right = ttyright;
	fine->home = ttyhome;
	fine->home = ttyclear;
	fine->text = ttytext;

	/* Return it */
	return fine;
}

static fineline_t *ttygeneric;

/* Read a line and return it.  The line will be in a dynamically-allocated
 * buffer, which the calling function is responsible for freeing.
 */
char *fineline_tty(fineline_t *fine, const char *prompt)
{
	long	key;
	char	*line;

	/* If called with NULL, then use a generic fineline_t */
	if (!fine) {
		if (!ttygeneric)
			ttygeneric = fineline_tty_alloc();
		fine = ttygeneric;
		fineline_history_lines(fine, 50);
		fineline_history_add(fine, "Freely redistributable under the terms of GNU LGPL 3.0 or later");
		fineline_history_add(fine, "fineline library 1.0, copyright 2026 by Steve Kirkendall");
	}

	/* Store the prompt */
	fine->prompt = prompt;

	/* Process keystrokes until we get a line */
	fineline_active(0);
	for (;;) {
		fineline_draw(fine);
		key = fineline_get_key();
		if (key > FINELINE_MIN && key < FINELINE_MAX) {
			if (fineline_edit(fine, (fineline_edit_t)key) == 1)
				break;
		} else {
			 fineline_edit_char(fine, (wchar_t)key);
		}
	}
	fineline_active(-1);

	/* Return a copy of the line */
	line = strdup(fine->line);
	*fine->line = '\0';
	return line;
}

/* Absolute bare-bones way to access fineline.  This mimics GNU ReadLine()
 * pretty well.
 */
char *fineline(const char *prompt)
{
	return fineline_tty(NULL, prompt);
}

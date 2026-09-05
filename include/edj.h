/* edj.h */

/* Help C and C++ peacefully coexist */
#ifndef EXTERN_C
# ifdef _cplusplus
#  define BEGIN_C extern "C" {
#  define END_C }
#  define VOID_C
# else
#  define BEGIN_C
#  define END_C
#  define VOID_C void
# endif
#endif

/* These are token types.  Some are used only during parsing, some are for both
 * parsing and the internal edj_t tree, and some are internally for edj_t only.
 */
typedef enum {
	EDJ_BADTOKEN, EDJ_NEWLINE,
	EDJ_OBJECT, EDJ_ENDOBJECT,
	EDJ_ARRAY, EDJ_ENDARRAY, EDJ_DEFER,
	EDJ_KEY, EDJ_STRING, EDJ_NUMBER, EDJ_NULL, EDJ_BOOLEAN
} edjtype_t;

/* These represent a parsed token */
typedef struct {
	const char *start;
	size_t len;
	edjtype_t type;
} edj_token_t;


/* This represents a JSON value.  The way it is used depends on the type:
 * EDJ_OBJECT	first points to first member
 * EDJ_ARRAY	first points to first element
 * EDJ_KEY	first points to value, text contains name
 * EDJ_STRING	text contains value
 * EDJ_NUMBER	text contains value, as a string
 * EDJ_BOOLEAN	text contains "true" or "false"
 * EDJ_NULL	text is "" or an error message
 */
typedef struct edj_s {
	struct edj_s *next;	/* next element of an array or object */
	struct edj_s *first;	/* contents of this object, array, or key */
	edjtype_t type : 4;	/* type of this edj_t node */
	unsigned    memslot:12; /* used for EDJ_DEBUG_MEMORY */
	char        text[14];	/* value of string, number, boolean; name of key */
} edj_t;

/* For edj_t's that are EDJ_ARRAY or EDJ_OBJECT, this is a pointer to the
 * last element/member, while parsing a file only.  This saves us from having
 * to scan potentially long arrays every time we want to append one element.
 *
 * The way this works is: It adds 1 to the container, so it points to the
 * memory immediately after the edj_t.  It then casts that pointer to be
 * a pointer to a (edj_t*), and uses [-1] to find the space for a (edjt_t *)
 * at then end of j's memory.
 */
#define EDJ_END_POINTER(j)	(((edj_t **)((j) + 1))[-1])

/* In arrays, we use the 4 bytes before EDJ_END_POINTER to store the length */
#define EDJ_ARRAY_LENGTH(j)	(((__uint32_t *)&EDJ_END_POINTER(j))[-1])

/* These are used to stuff a binary double or int into a EDJ_NUMBER*/
#define EDJ_DOUBLE(j)	(((double *)((j) + 1))[-1])
#define EDJ_INT(j)	(((int *)((j) + 1))[-1])
/* This stores info about formatting -- mostly output formatting, since for
 * input we take whatever we're given.
 */
typedef struct {
	short	tab;	/* indentation to add for pretty-printing nested data */
	short	oneline;/* Force compact output if shorter than this */
	short	digits;	/* precision for floating point output */
	char	table[20];/* Table output: csv/shell/grid/json */
	char	string;	/* unquoted string output */
	char	pretty;	/* Pretty-print JSON */
	char	elem;	/* one element per line */
	char	sh;	/* Quote output for shell */
	char	errors;	/* Error output.  Writes text in "null" to stderr */
	char	ascii;	/* Convert non-ASCII characters to \u sequences */
	char	color;	/* Allow the use of ANSI escape sequences */
	char	quick;	/* Output tables piecemeal.  Use first row for names */
	char	graphic;/* Use Unicode graphic chars where appropriate */
	char	prefix[20]; /* Prefix to add to keys for shell output */
	char	null[20];/* how to display null in tables */
	FILE	*fp;	/* where to write to */
} edjformat_t;

/* This is a collection of debugging flags.  These are generally set via
 * the edj_debug() function, but various library functions need access to
 * them.
 */
typedef struct {
        int abort;      /* Controls whether some errors cause an abort */
        int expr;       /* Output information about simple expressions */
        int calc;       /* Output information about complex expressions */
        int trace;      /* Trace commands as they're run */
} edj_debug_t;

extern edj_debug_t edj_debug_flags;
extern edjformat_t edj_format_default;

/* This represents a file that is open for reading JSON data or scripts.
 * The file is mapped into memory starting at "base", and can be accessed
 * like a giant string.
 *
 * For data files, the reference count is 0 unless it contains a deferred
 * array, in which case the reference count is 1.  For script files, it the
 * number of functions or vars/consts defined in the script.
 */
typedef struct edjfile_s {
	struct edjfile_s *other;/* Used to for a linked list */
	int		fd;	/* File descriptor of the open file */
	int		isfile;	/* Boolean: is this a regular file (not pipe, etc) */
	int		refs;	/* Reference count */
	size_t		size;	/* Size of the file, in bytes */
	const char	*base;	/* Contents of the file, as a giant string */
	char		*filename; /* Name of the file */
} edjfile_t;

#define EDJ_PATH_DELIM		':'

/* This stores info about the implementation of different types of deferred
 * arrays. It's basically a collection of function pointers, plus a size_t
 * indicating how much storage space it needs.
 */
typedef struct {
	size_t	size;	/* Size of edjdef_t plus any other needed storage */
	char	*desc;	/* basically the "class" of deferred items */
	edj_t	*(*first)(edj_t *array);	/* REQUIRED */
	edj_t	*(*next)(edj_t *elem);		/* REQUIRED */
	int	(*islast)(const edj_t *elem);	/* REQUIRED */
	void	(*free)(edj_t *array_or_elem);	/* Only if special needs */
	edj_t	*(*byindex)(edj_t *array, int index);
	edj_t	*(*bykeyvalue)(edj_t *array, const char *key, edj_t *value);
} edjdeffns_t;

/* This is the generic part of a EDJ_DEFER node.  It starts with plain edj_t,
 * and adds some extra fields.  An actual EDJ_DEFER note will generally have
 * other information, as its own data type.  The functions that edjdeffns_t
 * points to know the actual data type.
 */
typedef struct {
	edj_t	json; /* Standard stuff, with ->type=EDJ_DEFER */
	edjdeffns_t *fns; /* pointer to a group of function pointers */
	edjfile_t *file; /* if non-NULL, it indicates which file to read */
} edjdef_t;

/* This is a list of token types.  Nearly all of them are operators.
 * IF YOU MAKE ANY CHANGES HERE, THEN YOU MUST ALSO UPDATE THE operators[]
 * ARRAY IN calcparse.c
 */
typedef enum {
        EDJOP_ADD,
	EDJOP_AG,
	EDJOP_AND,
	EDJOP_APPEND,
	EDJOP_ARRAY,
	EDJOP_AS,
	EDJOP_ASSIGN,
	EDJOP_BETWEEN,
	EDJOP_BITAND,
	EDJOP_BITLEFT,
	EDJOP_BITNOT,
	EDJOP_BITOR,
	EDJOP_BITRIGHT,
	EDJOP_BITXOR,
	EDJOP_BOOLEAN,
	EDJOP_COALESCE,
	EDJOP_COLON,
	EDJOP_COMMA,
	EDJOP_DESCENDING,
	EDJOP_DISTINCT,
	EDJOP_DIVIDE,
	EDJOP_DEEPDOT,
	EDJOP_DOT,
	EDJOP_DOUBLEDOT,
	EDJOP_EACH,
	EDJOP_ELLIPSIS,
	EDJOP_ENDARRAY,
	EDJOP_ENDOBJECT,
	EDJOP_ENDPAREN,
	EDJOP_ENVIRON,
	EDJOP_EQ,
	EDJOP_EQSTRICT,
	EDJOP_FIND,
	EDJOP_FIRST,
	EDJOP_FNCALL,
	EDJOP_FROM,
	EDJOP_GE,
	EDJOP_GROUP,
	EDJOP_GROUPBY,
	EDJOP_GT,
	EDJOP_HAVING,
	EDJOP_ICEQ,
	EDJOP_ICNE,
	EDJOP_IN,
	EDJOP_ISNOTNULL,
	EDJOP_ISNULL,
	EDJOP_LE,
	EDJOP_LIKE,
	EDJOP_LIMIT,
	EDJOP_LITERAL,
	EDJOP_LJOIN,
	EDJOP_LT,
	EDJOP_MAYBEASSIGN,
	EDJOP_MAYBEMEMBER,
	EDJOP_MODULO,
	EDJOP_MULTIPLY,
	EDJOP_NAME,
	EDJOP_NE,
	EDJOP_NEGATE,
	EDJOP_NESTRICT,
	EDJOP_NJOIN,
	EDJOP_NOT,
	EDJOP_NOTIN,
	EDJOP_NOTLIKE,
	EDJOP_NULL,
	EDJOP_NUMBER,
	EDJOP_OBJECT,
	EDJOP_OR,
	EDJOP_ORDERBY,
	EDJOP_QUESTION,
	EDJOP_REGEX,
	EDJOP_RJOIN,
	EDJOP_SELECT,
	EDJOP_STARTARRAY,
	EDJOP_STARTOBJECT,
	EDJOP_STARTPAREN,
	EDJOP_STRING,
	EDJOP_SUBEXPR,
	EDJOP_SUBSCRIPT,
	EDJOP_SUBTRACT,
	EDJOP_VALUES,
	EDJOP_WHERE,
	EDJOP_INVALID /* <-- This must be the last */
} edjop_t;

/* This is used to represent an expression, or part of an expression */
typedef struct edjcalc_s{
        edjop_t op;
        union {
                struct {
                        struct edjcalc_s *left;        /* left operand */
                        struct edjcalc_s *right;       /* right operand */
                } param;
                struct {
                        struct edjfunc_s *jf;          /* function info */
                        struct edjcalc_s *args;        /* args as array generator */
                        size_t agoffset;                /* If aggregate, this is the offset of its agdata */
                } func;
                struct {
			void	*preg;
			int	global;
                } regex;
                struct edjag_s *ag;
                struct edjselect_s *select;
                edj_t *literal;
                char text[1]; /* extra chars get allocated later */
        } u;
} edjcalc_t;
/* This enum represents details about how a single context layer is used */
typedef enum {
	EDJ_CONTEXT_NOFREE = 1,/* Don't free the data when context is freed */
	EDJ_CONTEXT_VAR = 2,	/* contains vars -- use with GLOBAL for non-local */
        EDJ_CONTEXT_CONST = 4,	/* contains consts -- like variable but can't assign */
        EDJ_CONTEXT_GLOBAL = 8,/* Context is accessible everywhere */
	EDJ_CONTEXT_THIS = 16, /* Context can be "this" or "that" */
	EDJ_CONTEXT_DATA = 32,	/* Context contains "data" variable */
        EDJ_CONTEXT_ARGS = 64, /* Function arguments and local vars/consts */
        EDJ_CONTEXT_NOCACHE = 128, /* try autoload() before *data */
        EDJ_CONTEXT_MODIFIED = 256 /* Data has been modified (set via context->modified() function */
} edjcontextflags_t;

/* This is used to track context (the stack of variable definitions).  */
typedef struct edjcontext_s {
    struct edjcontext_s *older;/* link list of edjcontext_t contexts */
    edj_t *data;     /* a used item */
    edj_t *(*autoload)(char *key); /* called from edj_context_by_key() */
    void   (*modified)(struct edjcontext_s *layer, edjcalc_t *lvalue);
    edjcontextflags_t flags;
} edjcontext_t;

BEGIN_C

/* Files */
char edj_file_new_type;
void edj_file_defer(edjfile_t *jf, edj_t *array);
void edj_file_defer_free(edj_t *array);
edjfile_t *edj_file_load(const char *filename);
void edj_file_unload(edjfile_t *jf);
edjfile_t *edj_file_containing(const char *where, int *refline);
FILE *edj_file_update(const char *filename);
char *edj_file_path(const char *prefix, const char *name, const char *suffix);

/* Error handling */
extern char *edj_debug(char *flags);

/* This flag indicates whether computations have been interrupted */
extern int edj_interrupt;

/* Manipulation */
extern void edj_free(edj_t *json);
extern edj_t *edj_simple(const char *str, size_t len, edjtype_t type);
extern edj_t *edj_simple_from_token(edj_token_t *token);
extern edj_t *edj_string(const char *str, size_t len);
extern edj_t *edj_number(const char *str, size_t len);
extern edj_t *edj_boolean(int boolean);
extern edj_t *edj_null(void);
extern edj_t *edj_error_null(const char *where, const char *fmt, ...);
extern edj_t *edj_from_int(int i);
extern edj_t *edj_from_double(double f);
extern edj_t *edj_key(const char *key, edj_t *value);
extern edj_t *edj_object();
extern edj_t *edj_array();
extern edj_t *edj_defer(edjdeffns_t *fns);
extern edj_t *edj_defer_ellipsis(int from, int to);
extern char *edj_append(edj_t *container, edj_t *more);
extern size_t edj_sizeof(edj_t *json);
extern char *edj_typeof(edj_t *json, int extended);
extern char *edj_mix_types(char *oldtype, char *newtype);
extern void edj_sort(edj_t *array, edj_t *orderby, int grouping);
extern edj_t *edj_copy_filter(edj_t *json, int (*filter)(edj_t *));
extern edj_t *edj_copy(edj_t *json);
extern edj_t *edj_array_flat(edj_t *array, int depth);
extern edj_t *edj_unroll(edj_t *table, edj_t *nestlist);
extern edj_t *edj_array_group_by(edj_t *array, edj_t *orderby);
extern int edj_walk(edj_t *json, int (*callback)(edj_t *, void *), void *data);

/* Binary files */
typedef enum {
	EDJ_BLOB_ANY = -1,    /* Automatically choose best interpretation */
	EDJ_BLOB_STRING = -2, /* Automatically choose best text interpretation */
	EDJ_BLOB_UTF8 = -3,   /* Treat like UTF-8 text.  Fail if malformed */
	EDJ_BLOB_LATIN1 = -4, /* Treat like Latin1 text, convert to UTF-8 */
	EDJ_BLOB_BYTES = -5   /* Treat like a deferred array of bytes */
} edjblobconv_t;
extern edjblobconv_t edj_blob_best(const char *data, size_t len, size_t *reflatin1len);
extern edj_t *edj_blob_convert(const char *data, size_t len, edjblobconv_t conversion);
extern size_t edj_blob_unconvert(edj_t *json, char *data, edjblobconv_t conversion);
extern edj_t *edj_blob(edj_t *json, edjblobconv_t convin, edjblobconv_t convout);
extern const char *edj_blob_data(edj_t *json, size_t *reflen);
extern int edj_blob_test(const char *data, size_t len);
extern edj_t *edj_blob_parse(const char *data, size_t len, const char **refend, const char **referr);

/* Parsing */
extern void edj_parse_hook(
	const char *plugin,
	const char *name,
	const char *suffix,
	const char *mimetype,
	int (*tester)(const char *str, size_t len),
	edj_t *(*parser)(const char *str, size_t len, const char **refend, const char **referr),
	int (*updater)(edj_t *data, const char *filename));
extern edj_t *edj_parse_string(const char *str);
extern edj_t *edj_parse_file(const char *filename);

/* Serialization / Output */
extern edj_t *edj_explain(edj_t *stats, edj_t *row, int depth);
extern char *edj_serialize(edj_t *json, edjformat_t *format);
extern void edj_print_table_hook(char *name, void (*fn)(edj_t *json, edjformat_t *format));
extern int edj_print_incomplete_line;
extern void edj_print(edj_t *json, edjformat_t *format);
extern void edj_grid(edj_t *json, edjformat_t *format);
extern void edj_format_set(edjformat_t *format, edj_t *config);
extern void edj_undefer(edj_t *arr);

/* Accessing */
extern edj_t *edj_by_key(const edj_t *object, const char *key);
extern edj_t *edj_by_deep_key(edj_t *container, char *key);
extern edj_t *edj_by_index(edj_t *array, int idx);
extern edj_t *edj_by_key_value(edj_t *array, const char *key, edj_t *value);
extern edj_t *edj_by_expr(edj_t *container, const char *expr, const char **after, edj_t **parent, char **key);
extern edj_t *edj_find(edj_t *haystack, edj_t *needle, int ignorecase, char *needkey);
extern edj_t *edj_find_calc(edj_t *haystack, edjcalc_t *calc, edjcontext_t *context);
extern edj_t *edj_grep(edj_t *haystack, edj_t *needle, int ignorecase, char *needkey);
#ifdef REG_ICASE /* skip this if <regex.h> not included */
extern edj_t *edj_find_regex(edj_t *haystack, regex_t *regex, char *needkey);
extern edj_t *edj_grep_regex(edj_t *haystack, regex_t *regex, char *needkey);
#endif
extern char *edj_default_text(char *newdefault);
extern char *edj_text(edj_t *json);
extern double edj_double(edj_t *json);
extern int edj_int(edj_t *json);
extern edj_t *edj_first(edj_t *arr);
extern edj_t *edj_next(edj_t *elem);
extern void edj_break(edj_t *elem);
extern int edj_is_last(const edj_t *elem);

/* Testing */
extern int edj_length(edj_t *container);
extern int edj_is_true(edj_t *json);
extern int edj_is_null(edj_t *json);
extern int edj_is_error(edj_t *json);
extern int edj_is_table(edj_t *json);
extern int edj_is_short(edj_t *json, size_t oneline);
extern int edj_is_date(edj_t *json);
extern int edj_is_time(edj_t *json);
extern int edj_is_datetime(edj_t *json);
extern int edj_is_period(edj_t *json);
extern int edj_is_deferred_array(const edj_t *arr);
extern int edj_is_deferred_element(const edj_t *elem);
extern int edj_equal(edj_t *j1, edj_t *j2);
extern int edj_compare(edj_t *obj1, edj_t *obj2, edj_t *compare);
#define edj_text_by_key(container, key) edj_text(edj_by_key((container), (key)))
#define edj_text_by_deep_key(container, key) edj_text(edj_by_deep_key((container), (key)))
#define edj_text_by_index(container, index) edj_text(edj_by_index((container), (index)))
/* The next parameter may be NULL.  See edj_by_expr() for more details. */
#define edj_text_by_expr(container, expr, after) edj_text(edj_by_expr((container), (expr), (after)))


/* Multibyte character strings */
size_t edj_mbs_len(const char *s);
int edj_mbs_width(const char *s);
int edj_mbs_height(const char *s);
size_t edj_mbs_line(const char *s, int line, char *buf, char **refstart, int *refwidth);
size_t edj_mbs_wrap_char(char *buf, const char *s, int width);
size_t edj_mbs_wrap_word(char *buf, const char *s, int width);
size_t edj_mbs_simple_key(char *dest, const char *src);
int edj_mbs_cmp(const char *s1, const char *s2);
int edj_mbs_ncmp(const char *s1, const char *s2, size_t len);
const char *edj_mbs_substr(const char *s, size_t start, size_t *reflimit);
const char *edj_mbs_str(const char *haystack, const char *needle, size_t *refccount, size_t *reflen, int last, int ignorecase);
void edj_mbs_tolower(char *s);
void edj_mbs_toupper(char *s);
void edj_mbs_tomixed(char *s, edj_t *exceptions);
int edj_mbs_casecmp(const char *s1, const char *s2);
int edj_mbs_ncasecmp(const char *s1, const char *s2, size_t len);
int edj_mbs_abbrcmp(const char *abbr, const char *full);
const char *edj_mbs_ascii(const char *str, char *buf);
size_t edj_mbs_escape(char *dst, const char *src, size_t len, int quote, edjformat_t *format);
size_t edj_mbs_unescape(char *dst, const char *src, size_t len);
int edj_mbs_like(const char *text, const char *pattern);
int edj_mbs_levenshtein(const char *s1, const char *s2, int ignorecase);

/* Dates and times */
int edj_str_date(const char *str);
int edj_str_time(const char *str);
int edj_str_datetime(const char *str);
int edj_str_period(const char *str);
int edj_date(char *result, const char *str);
int edj_time(char *result, const char *str, const char *tz);
int edj_datetime(char *result, const char *str, const char *tz);
int edj_datetime_add(char *result, const char *str, const char *period);
int edj_datetime_subtract(char *result, const char *str, const char *period);
int edj_datetime_diff(char *result, const char *str1, const char *str2);
int edj_datetime_seconds(const char *str);
int edj_time_seconds(const char *str);
int edj_period_abs(char *result, const char *text);
edj_t *edj_datetime_fn(edj_t *args, char *type);

/* Bigger analysis functions */
typedef enum {
	EDJ_DIFF_VALUE = 1,
	EDJ_DIFF_SPAN = 2,
	EDJ_DIFF_BESIDE = 4,
	EDJ_DIFF_EDIT = 8,
	EDJ_DIFF_CONTEXT = 16
} edjdiffstyle_t;
typedef enum {
	EDJ_COMMON_INDEX = 1,
	EDJ_COMMON_COUNT = 2,
	EDJ_COMMON_CHECK = 3,
	EDJ_COMMON_IN = 0,
	EDJ_COMMON_ALL = 4,
	EDJ_COMMON_IN_ONLY = 8,
	EDJ_COMMON_OUT_ONLY = 16,
	EDJ_COMMON_NONE = 24,
	EDJ_COMMON_MIX = 0,
	EDJ_COMMON_STATS = 32,
	EDJ_COMMON_NOSORT = 64,
	EDJ_COMMON_FORCE = 128,
	EDJ_COMMON_RECHECK = 256
} edjcommonstyle_t;
extern int edj_hash(edj_t *json, int seed);
extern edj_t *edj_diff(edj_t *edjold, edj_t *edjnew, edjdiffstyle_t diff);
extern edj_t *edj_common(const char **keys, edj_t **values, int style);


/* Configuration data */
edj_t *edj_config, *edj_system;
void edj_config_load(const char *name);
void edj_config_save(const char *name);
edj_t *edj_config_style(const char *name, edj_t **refstyles);
edj_t *edj_config_get(const char *section, const char *key);
void edj_config_set(const char *section, const char *key, edj_t *value);
edj_t *edj_config_parse(edj_t *config, const char *settings, const char **refend);
#define edj_config_get_int(section, key) edj_int(edj_config_get(section, key))
#define edj_config_get_double(section, key) edj_double(edj_config_get(section, key))
/* edj_config_get_text() is not threadsafe because it returns a pointer into
 * the edj_config tree.  If the option is changed while an expression is
 * being evaluated, the returned value could become a dangling pointer.
 */
#define edj_config_get_text(section, key) edj_text(edj_config_get(section, key))
#define edj_config_get_boolean(section, key) edj_is_true(edj_config_get(section, key))

/* Plugins */
edj_t *edj_plugins;
void edj_plugin_repl_hook(const char *pluginname, int priority, void (*repl)(edjcontext_t *));
int edj_plugin_repl(edjcontext_t *);
edj_t *edj_plugin_load(const char *name);


#ifndef FALSE
# define FALSE 0
# define TRUE 1
#endif



/* Functions are stored in a linked list of these.  If a function is *not* an
 * aggregate function then agfn is NULL and storeagesize is 0.  Also, the
 * name, args, and returntype will generally be (const char *) literals for
 * compiled C functions, but dynamically-allocated strings for user-defined
 * (edjcalc script) functions; the latter prevents us from declaring those
 * fields as "const" here.
 */
typedef struct edjfunc_s {
        struct edjfunc_s *other;
        char    *name;
        char	*args;		/* Argument list, as text */
        char	*returntype;	/* Return value type, as text */
        edj_t *(*fn)(edj_t *args, void *agdata);
        void   (*agfn)(edj_t *args, void *agdata);
        size_t  agsize;
        int	jfoptions;
        struct edjcmd_s *user;
        edj_t	*userparams;
} edjfunc_t;
#define EDJFUNC_EDJFREE 1	/* Call edj_free() on the agdata afterward */
#define EDJFUNC_FREE 2		/* Call free() on the agdata afterward */
#define EDJFUNC_FCLOSE 4	/* Call fclose() on the agdata afterward */

/* For non-aggregate functions, this is used to pass other information that
 * they might need.
 */
typedef struct {
	edjcontext_t *context;
	edjcalc_t    *regex; /* The regex_t is at regex->u.regex.preg */
} edjfuncextra_t;


/* This stores a list of aggregate functions used in a given context.  Each
 * edjcalc_t node with ->op==EDJOP_AG contains an "ag" pointer that points
 * to one of these, so  ->ag[i]->u.func->jf->agfn(args, context, storage)
 * is the way to call the aggregating function. Yikes.
 *
 * Note that the combined size of the agdata is stored here, but the memory
 * for it is allocated in edj_calc() as needed.  This is done for thread
 * safety, in case two threads call edj_calc() on the same edjcalc_t at
 * the same time.
 */
typedef struct edjag_s {
        edjcalc_t *expr;   /* equation containing aggregate functions */
        int        nags;    /* number of aggregates */
        size_t     agsize;  /* combined storage requirements */
        edjcalc_t *ag[1];  /* function calls with params, expanded as needed */
} edjag_t;


/* This tracks source code for commands.  For strings, "buf" points to the
 * string.  For files, additional memory is allocated for "buf" and must also
 * be freed, but "filename" is a copy of a pointer to a filename string which
 * must not be freed before the application terminates.
 */
typedef struct {
	const char	*buf;	/* buffer, contains entire source text */
	const char	*str;	/* current parse position within "base" */
	size_t	size;		/* size of "buf" */
} edjsrc_t;

/* This is used for returning the result of a command.  A NULL pointer means
 * the command completed without incident, and execution should continue to
 * the next command.  Otherwise, the meaning is determined by the "ret" field
 * as follows:
 *   NULL		An error, indicated by code and text
 *   &edj_cmd_break	A "break" command
 *   &edj_cmd_continue A "continue" command
 *   (anything else)	A "return" command with this value
 */
typedef struct {
	edj_t	*ret;		/* if really a "return" then this is value */
	const char *where;	/* where error detected */
	char	text[1];	/* extended as necessary */
} edjcmdout_t;
extern edj_t edj_cmd_break, edj_cmd_continue;

/* This data type is used for storing command names.  Some command names are
 * built in, but plugins can add new command names too.
 */
typedef struct edjcmdname_s {
	struct edjcmdname_s *other;
	char	*name;
	struct edjcmd_s *(*argparser)(edjsrc_t *src, edjcmdout_t **referr);
	edjcmdout_t *(*run)(struct edjcmd_s *cmd, edjcontext_t **refcontext);
	char	*pluginname;
} edjcmdname_t;

/* This stores a parsed statement. */
typedef struct edjcmd_s {
	const char	   *where;/* pointer into source text, for reporting errors */
	edjcmdname_t	   *name;/* command name and other details */
	char		   var;
	char		   *key; /* Name of a variable, if the cmd uses one */
	edjcalc_t 	   *calc;/* calc expression, if the cmd uses one */
	edjcontextflags_t flags;/* Context flags for "key" */
	struct edjcmd_s   *sub; /* For "then" in "if-then-else" for example */
	struct edjcmd_s   *more;/* For "else" in "if-then-else" for example */
	struct edjcmd_s   *nextcmd;/* in a series of statements, "nextcmd" is next */
} edjcmd_t;

/* These are magic values for edj_context_file() "current" argument.  They
 * aren't enums because we could also pass an int index to select a file.
 */
#define EDJ_CONTEXT_FILE_NEXT		(-1)
#define EDJ_CONTEXT_FILE_SAME		(-2)
#define EDJ_CONTEXT_FILE_PREVIOUS	(-3)

/* Function declarations */
edjfunc_t *edj_calc_function_first(void);
void edj_calc_aggregate_hook(
        const char    *name,
        const char	*args,
        const char	*type,
        edj_t *(*fn)(edj_t *args, void *agdata),
        void   (*agfn)(edj_t *args, void *agdata),
        size_t  agsize,
        int	jfoptions);
void edj_calc_function_hook(
	const char	*name,
	const char	*args,
	const char	*type,
        edj_t *(*fn)(edj_t *args, void *agdata));
int edj_calc_function_user(
	char *name,
	edj_t *params,
	char *paramstr,
	char *returntype,
	edjcmd_t *cmd);
edjfunc_t *edj_calc_function_by_name(const char *name);
char *edj_calc_op_name(edjop_t op);
void edj_calc_dump(edjcalc_t *calc);
edjcalc_t *edj_calc_parse(const char *str, const char **refend, const char **referr, int canassign);
edjcalc_t *edj_calc_list(edjcalc_t *list, edjcalc_t *item);
void edj_calc_free(edjcalc_t *calc);
void *edj_calc_ag(edjcalc_t *calc, void *agdata);
edj_t *edj_calc(edjcalc_t *calc, edjcontext_t *context, void *agdata);

void edj_context_hook(edjcontext_t *(*addcontext)(edjcontext_t *context));
edjcontext_t *edj_context_free(edjcontext_t *context);
edjcontext_t *edj_context(edjcontext_t *context, edj_t *data, edjcontextflags_t flags);
edjcontext_t *edj_context_insert(edjcontext_t **refcontext, edjcontextflags_t flags);
edjcontext_t *edj_context_std(edj_t *data);
edj_t *edj_context_file(edjcontext_t *context, const char *filename, int writable, int *refcurrent);
edjcontext_t *edj_context_func(edjcontext_t *context, edjfunc_t *fn, edj_t *args);
edj_t *edj_context_by_key(edjcontext_t *context, char *key, edjcontext_t **reflayer);
edj_t *edj_context_assign(edjcalc_t *lvalue, edj_t *rvalue, edjcontext_t *context);
edj_t *edj_context_append(edjcalc_t *lvalue, edj_t *rvalue, edjcontext_t *context);
edj_t *edj_context_delete(edjcalc_t *lvalue, edjcontext_t *context);
int edj_context_declare(edjcontext_t **refcontext, char *key, edj_t *value, edjcontextflags_t flags);
edj_t *edj_context_default_table(edjcontext_t *context, char **refexpr);

void edj_user_printf(edjformat_t *format, const char *face, const char *fmt, ...);
void edj_user_ch(int ch);
int edj_user_result(edj_t *result);
void edj_user_hook(int (*handler)(edj_t *jface, int newface, const char *text, size_t len));
void edj_user_result_hook(int (*handler)(edj_t *result));

/****************************************************************************/

/* This value is returned by edj_cmd_parse() and edj_cmd_parse_string()
 * if an error is detected.  Note that NULL does *NOT* indicate an error.
 */
extern edjcmd_t EDJ_CMD_ERROR[];

edjcmdname_t *edj_cmd_hook(char *pluginname, char *cmdname, edjcmd_t *(*argparser)(edjsrc_t *src, edjcmdout_t **referr), edjcmdout_t *(*run)(edjcmd_t *cmd, edjcontext_t **refcontext));
int edj_cmd_lineno(edjsrc_t *src);
edjcmdout_t *edj_cmd_error(const char *where, const char *fmt, ...);
edjcmdout_t *edj_cmd_src_error(edjsrc_t *src, int code, char *fmt, ...);
void edj_cmd_parse_whitespace(edjsrc_t *src);
char *edj_cmd_parse_key(edjsrc_t *src, int quotable);
char *edj_cmd_parse_paren(edjsrc_t *src);
edjcmd_t *edj_cmd(edjsrc_t *src, edjcmdname_t *name);
void edj_cmd_free(edjcmd_t *cmd);
edjcmd_t *edj_cmd_parse_single(edjsrc_t *src, edjcmdout_t **referr);
edjcmd_t *edj_cmd_parse_curly(edjsrc_t *src, edjcmdout_t **referr);
edjcmd_t *edj_cmd_parse_string(char *str);
edjcmd_t *edj_cmd_parse_file(const char *filename);
edjcmdout_t *edj_cmd_run(edjcmd_t *cmd, edjcontext_t **refcontext);
edj_t *edj_cmd_fncall(edj_t *args, edjfunc_t *fn, edjcontext_t *context);
edjcmd_t *edj_cmd_append(edjcmd_t *existing, edjcmd_t *added, edjcontext_t *context);


/* The following are for debugging memory leaks.  They're only used if your
 * program defined EDJ_DEBUG_MEMORY.
 */
extern int edj_debug_count;
extern void edj_debug_free(const char *file, int line, edj_t *json);
extern edj_t *edj_debug_simple(const char *file, int line, const char *str, size_t len, edjtype_t type);
extern edj_t *edj_debug_string(const char *file, int line, const char *str, size_t len);
extern edj_t *edj_debug_number(const char *file, int line, const char *str, size_t len);
extern edj_t *edj_debug_boolean(const char *file, int line, int boolean);
extern edj_t *edj_debug_null(const char *file, int line);
extern edj_t *edj_debug_error_null(const char *file, int line, char *fmt, ...);
extern edj_t *edj_debug_from_int(const char *file, int line, int i);
extern edj_t *edj_debug_from_double(const char *file, int line, double f);
extern edj_t *edj_debug_key(const char *file, int line, const char *key, edj_t *value);
extern edj_t *edj_debug_object(const char *file, int line);
extern edj_t *edj_debug_array(const char *file, int line);
extern edj_t *edj_debug_defer(const char *file, int line, edjdeffns_t *fns);
extern edj_t *edj_debug_first(const char *file, int line, edj_t *array);
extern edj_t *edj_debug_parse_string(const char *file, int line, const char *str);
extern edj_t *edj_debug_copy(const char *file, int line, edj_t *json);
extern edj_t *edj_debug_copy_filter(const char *file, int line, edj_t *json, int (*filter)(edj_t *item));
extern edj_t *edj_debug_calc(const char *file, int line, edjcalc_t *calc, edjcontext_t *context, void *agdata);
#ifdef EDJ_DEBUG_MEMORY
#define edj_free(json)			edj_debug_free(__FILE__, __LINE__, json)
#define edj_simple(str, len, type)	edj_debug_simple(__FILE__, __LINE__, str, len, type)
#define edj_string(str, len)		edj_debug_string(__FILE__, __LINE__, str, len)
#define edj_number(str, len)		edj_debug_number(__FILE__, __LINE__, str, len)
#define edj_boolean(boolean)		edj_debug_boolean(__FILE__, __LINE__, boolean)
#define edj_null()			edj_debug_null(__FILE__, __LINE__)
#define edj_error(...)			edj_debug_error(__FILE__, __LINE__, __VA_ARGS__)
#define edj_from_int(i)		edj_debug_from_int(__FILE__, __LINE__, i)
#define edj_from_double(f)		edj_debug_from_double(__FILE__, __LINE__, f)
#define edj_key(key, value)		edj_debug_key(__FILE__, __LINE__, key, value)
#define edj_object()			edj_debug_object(__FILE__, __LINE__)
#define edj_array()			edj_debug_array(__FILE__, __LINE__)
#define edj_defer(fns)			edj_debug_defer(__FILE__, __LINE__, (fns))
#define edj_first(array)		edj_debug_first(__FILE__, __LINE__, (array))
#define edj_parse_string(str)          edj_debug_parse_string(__FILE__, __LINE__, str)
#define edj_copy(json)			edj_debug_copy(__FILE__, __LINE__, json)
#define edj_copy_filter(json, filter)	edj_debug_copy_filter(__FILE__, __LINE__, json, filter)
#define edj_calc(calc,context,agdata)	edj_debug_calc(__FILE__, __LINE__, calc, context, agdata)
#endif
END_C

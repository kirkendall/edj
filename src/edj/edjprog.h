/* edjprog.h */

extern int interactive;
extern edjcontext_t *context;

char **edj_completion(const char *text, int start, int end);
void interact(edjcontext_t **contextref, edjcmd_t *initcmd);
void batch(edjcontext_t **contextref, edjcmd_t *initcmd);
void edjcmds(void);
char *save_config(void);
void load_config(void);
void format_usage(void);
void color_usage(void);
void debug_usage(void);
void run(edjcmd_t *jc, edjcontext_t **refcontext);

void edjprog_repl_hook(const char *pluginname, int priority, void (*repl)(edjcontext_t *));

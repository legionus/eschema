%language "C"
%debug
%error-verbose

%output "uevent.parser.c"

/* Pure yylex.  */
%define api.pure
%lex-param { void *scanner }

/* Pure yyparse.  */
%parse-param { void *scanner }

%code requires {
#include "uevent.atom.h"
}

%{
#include <sys/types.h>
#include <sys/param.h>
#include <sys/stat.h>

#include <unistd.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include <limits.h>
#include <dirent.h>
#include <fcntl.h>
#include <error.h>
#include <errno.h>
#include <assert.h>

#include "uevent.parser.h"
#include "uevent.scanner.h"
#include "uevent.h"

struct atom *root = NULL;

struct dirent *rulefile;
char str[4096];
int linenumber = 1;

int yyerror(yyscan_t scanner, const char *s);

%}

%token ERROR LEFT_BRACKET SYMBOL STRING NUMBER BOOL RIGHT_BRACKET

%union {
	long long int num;
	char *str;
	struct atom *atom;
}

%type <str> STRING
%type <num> NUMBER
%type <num> BOOL
%type <str> SYMBOL
%type <atom> input
%type <atom> s_expr
%type <atom> arg
%type <atom> args

%%
input		:
		{
			struct atom *a = calloc(1, sizeof(struct atom));
			a->t = T_S_EXPR;
			a->v.s_expr = NULL;
			root = a;
			$$ = a;
		}
		| s_expr input
		{
			assert($2->t == T_S_EXPR);
			struct s_expr *s = calloc(1, sizeof(struct s_expr));
			s->atom = $1;
			s->next = $2->v.s_expr;
			$2->v.s_expr = s;
			$$ = $2;
		}
		;
s_expr		: LEFT_BRACKET args RIGHT_BRACKET
		{
			$$ = $2;
		}
		;
args		: arg
		{
			// (cons arg '())
			struct s_expr *s = calloc(1, sizeof(struct s_expr));
			s->atom = $1;
			s->next = NULL;
			struct atom *a = calloc(1, sizeof(struct atom));
			a->t = T_S_EXPR;
			a->v.s_expr = s;
			$$ = a;
		}
		| arg args
		{
			// (cons arg args)
			assert($2->t == T_S_EXPR);
			struct s_expr *s = calloc(1, sizeof(struct s_expr));
			s->atom = $1;
			s->next = $2->v.s_expr;
			$2->v.s_expr = s;
			$$ = $2;
		}
		;
arg		: s_expr
		| SYMBOL
		{
			struct atom *a = calloc(1, sizeof(struct atom));
			a->t = T_SYMBOL;
			a->v.str = $1;
			$$ = a;
		}
		| STRING
		{
			struct atom *a = calloc(1, sizeof(struct atom));
			a->t = T_STRING;
			a->v.str = $1;
			$$ = a;
		}
		| NUMBER
		{
			struct atom *a = calloc(1, sizeof(struct atom));
			a->t = T_NUMBER;
			a->v.num = $1;
			$$ = a;
		}
		| BOOL
		{
			struct atom *a = calloc(1, sizeof(struct atom));
			a->t = T_BOOL;
			a->v.num = $1;
			$$ = a;
		}
		;
%%

int
yyerror(yyscan_t scanner, const char *s)
{
	error(EXIT_SUCCESS, 0, "%s:%d: %s", rulefile->d_name, linenumber, s);
	return 0;
}

static void
rules_parse(FILE *fp)
{
	yyscan_t scanner;

	yylex_init(&scanner);
	yyset_in(fp, scanner);
	while (yyparse(scanner));
	yylex_destroy(scanner);
}

static int
rules_filter(const struct dirent *ent)
{
	size_t len;
	if (ent->d_type != DT_REG)
		return 0;
	len = strlen(ent->d_name);
	if (len <= 6 || strcmp(ent->d_name + (len - 6), ".rules"))
		return 0;
	return 1;
}

int
read_rules(const char *rulesdir)
{
	int i;
	ssize_t n;
	char path[PATH_MAX];
	struct dirent **namelist;

	error(EXIT_SUCCESS, 0, "load rules from %s", rulesdir);

	n = scandir(rulesdir, &namelist, &rules_filter, alphasort);
	if (n < 0) {
		error(EXIT_SUCCESS, 0, "scandir: %s: %m", rulesdir);
		return -1;
	}

	for (i = 0; i < n; i++) {
		FILE *fp;

		rulefile = namelist[i];

		snprintf(path, PATH_MAX - 1, "%s/%s", rulesdir, namelist[i]->d_name);

		if((fp = fopen(path, "r")) == NULL) {
			error(EXIT_SUCCESS, 0, "fopen: %s: %m", path);
			return -1;
		}

		rules_parse(fp);
		fclose(fp);
	}

	return 0;
}

int
main(int argc, char **argv)
{
	read_rules(argv[1]);

	print_atoms(root);
	printf("\n");

	struct stack *s = calloc(1, sizeof(struct stack));

	atom_init(s);

	struct atom *res = eval_atom(root, s);

	print_atoms(res);
	printf("\n");

	free(root);
	free(res);

	return 0;
}

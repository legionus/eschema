%language "C"
%debug
%error-verbose

%output "uevent.parser.c"

/* Pure yylex.  */
%define api.pure
%lex-param { void *scanner }

/* Pure yyparse.  */
%parse-param { void *scanner }
%parse-param { struct stack *stack }

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

int yyerror(yyscan_t scanner, struct stack *stack, const char *s);

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
%type <atom> pair
%type <atom> arg
%type <atom> args

%%
input		:
		{
			struct pair *s = calloc(1, sizeof(struct pair));
			s->car = NULL;
			s->cdr = NULL;

			struct atom *a = calloc(1, sizeof(struct atom));
			a->t = T_BEGIN;
			a->v.pair = s;

			root = a;

			$$ = a;
		}
		| input pair
		{
			assert($1->t == T_PAIR || $1->t == T_BEGIN);
			assert($1->v.pair->cdr == NULL);

			struct pair *s = calloc(1, sizeof(struct pair));
			s->car = $2;
			s->cdr = NULL;

			struct atom *a = calloc(1, sizeof(struct atom));
			a->t = T_PAIR;
			a->v.pair = s;

			$1->v.pair->cdr = a;

			$$ = a;
		}
		;
pair		: LEFT_BRACKET args RIGHT_BRACKET
		{
			$$ = $2;
		}
		;
args		: arg
		{
			// (cons arg '())
			struct pair *s = calloc(1, sizeof(struct pair));
			s->car = $1;
			s->cdr = NULL;

			struct atom *a = calloc(1, sizeof(struct atom));
			a->t = T_PAIR;
			a->v.pair = s;

			$$ = a;
		}
		| arg args
		{
			// (cons arg args)
			assert($2->t == T_PAIR);

			struct pair *s = calloc(1, sizeof(struct pair));
			s->car = $1;
			s->cdr = $2;

			struct atom *a = calloc(1, sizeof(struct atom));
			a->t = T_PAIR;
			a->v.pair = s;

			$$ = a;
		}
		;
arg		: pair
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
			$$ = $1 != 0 ? stack->atom_true : stack->atom_false;
		}
		;
%%

int
yyerror(yyscan_t scanner YY_ATTRIBUTE_UNUSED, struct stack *stack YY_ATTRIBUTE_UNUSED, const char *s)
{
	error(EXIT_SUCCESS, 0, "%s:%d: %s", rulefile->d_name, linenumber, s);
	return 0;
}

static void
rules_parse(FILE *fp, struct stack *stack)
{
	yyscan_t scanner;

	yylex_init(&scanner);
	yyset_in(fp, scanner);
	while (yyparse(scanner, stack));
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
read_rules(const char *rulesdir, struct stack *stack)
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

		rules_parse(fp, stack);
		fclose(fp);
	}

	free(namelist);

	return 0;
}

int
main(int argc __attribute__ ((__unused__)), char **argv)
{
	struct stack *s = atom_init();

	read_rules(argv[1], s);

	print_atom(root);
	printf("\n");

	struct atom *result = eval_atom(root, s);

	print_atom(result);
	printf("\n");

	free_stack(s);

	free_atom_recursive(result);
	free_atom_recursive(root);

	return 0;
}

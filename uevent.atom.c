#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <error.h>

#include "uevent.h"

static struct atom *ATOM_TRUE;
static struct atom *ATOM_FALSE;

const char *
get_atom_type(enum type t)
{
	switch (t) {
		case T_BOOL:   return "boolean";
		case T_ERROR:  return "error";
		case T_NUMBER: return "number";
		case T_PROC:   return "procedure";
		case T_STRING: return "string";
		case T_SYMBOL: return "symbol";
		case T_S_EXPR: return "expression";
	}
	error(EXIT_FAILURE, 0, "unknown type=%d", t);
	return "";
}

void
print_atoms(struct atom *a)
{
	struct s_expr *e = NULL;

	switch (a->t) {
		case T_PROC:
			printf("%p", a->v.proc);
			break;
		case T_ERROR:
			printf("ERR:%s", a->v.str);
			break;
		case T_NUMBER:
			printf("%lld", a->v.num);
			break;
		case T_STRING:
			printf("\"%s\"", a->v.str);
			break;
		case T_BOOL:
			printf("#%c", a->v.num ? 't' : 'f');
			break;
		case T_SYMBOL:
			printf("%s", a->v.str);
			break;
		case T_S_EXPR:
			printf("(");
			e = a->v.s_expr;
			while (e) {
				if (e != a->v.s_expr)
					printf(" ");
				print_atoms(e->atom);
				e = e->next;
			}
			printf(")");
			break;
	}
}

static struct atom *
resolve_proc(char *sym, struct stack *s)
{
	struct atom *a = calloc(1, sizeof(struct atom));
	struct procs *p = s->procs;

	while (p) {
		if (!strcmp(sym, p->sym)) {
			a->t = T_PROC;
			a->v.proc = p->proc;
			return a;
		}
		p = p->next;
	}

	a->t = T_ERROR;
	a->v.str = strdup("function not found");

	return a;
}

void *
free_atom(struct atom *a)
{
	struct s_expr *n, *e;

	switch (a->t) {
		case T_ERROR:
		case T_STRING:
			free(a->v.str);
			break;
		case T_S_EXPR:
			e = a->v.s_expr;
			while (e) {
				n = e->next;
				free_atom(e->atom);
				free(e);
				e = n;
			}
			break;
		default:
			break;
	}

	free(a);
	return NULL;
}

struct atom *
eval_atom(struct atom *a, struct stack *s)
{
	struct atom *n, *r;

	switch (a->t) {
		case T_BOOL:
		case T_ERROR:
		case T_NUMBER:
		case T_PROC:
		case T_STRING:
			return a;
		case T_SYMBOL:
			return resolve_proc(a->v.str, s);
		case T_S_EXPR:
			n = eval_atom(a->v.s_expr->atom, s);
			if (n->t != T_PROC)
				error(EXIT_FAILURE, 0, "unknown command: %s", a->v.s_expr->atom->v.str);
			r = n->v.proc(a->v.s_expr->next, s);
			free_atom(n);
			return r;
	}

	return NULL;
}

int
register_builtin(struct stack *s, char *name, atom_proc_t proc)
{
	struct procs *n, *l;
	
	n = calloc(1, sizeof(struct procs));
	n->sym = name;
	n->proc = proc;

	if (!s->procs) {
		s->procs = n;
		return 0;
	}

	l = s->procs;
	while (l->next)
		l = l->next;

	l->next = n;
	return 0;
}

static struct atom *
builtin_eq(struct s_expr *expr, struct stack *s)
{
	struct atom *n, *f;
	struct s_expr *e = expr;

	if (!e || !e->next)
		error(EXIT_FAILURE, 0, "eq: more arguments required");

	f = eval_atom(e->atom, s);

	if (f->t == T_ERROR)
		return ATOM_FALSE;

	do {
		e = e->next;

		n = eval_atom(e->atom, s);
		if (n->t == T_ERROR) {
			error(EXIT_FAILURE, 0, "error: %s", n->v.str);
			return ATOM_FALSE;
		}

		if (f->t != n->t)
			return ATOM_FALSE;

		if (f->t == T_BOOL && f->v.num != n->v.num)
			return ATOM_FALSE;

		if (f->t == T_NUMBER && f->v.num != n->v.num)
			return ATOM_FALSE;

		if (f->t == T_STRING && strcmp(f->v.str, n->v.str))
			return ATOM_FALSE;

	} while (e->next);

	return ATOM_TRUE;
}

static struct atom *
builtin_not(struct s_expr *expr, struct stack *s)
{
	struct atom *n;
	size_t n_args = 0;
	struct s_expr *e = expr;
	while (e) {
		n_args++;
		e = e->next;
	}

	if (n_args == 0)
		error(EXIT_FAILURE, 0, "not: more arguments required");

	if (n_args > 1)
		error(EXIT_FAILURE, 0, "not: too many arguments");

	n = eval_atom(expr->atom, s);

	if (n->t != T_BOOL)
		error(EXIT_FAILURE, 0, "unexpected result. expected bool, got %s", get_atom_type(n->t));

	return n->v.num ? ATOM_FALSE : ATOM_TRUE;
}

static struct atom *
builtin_and(struct s_expr *expr, struct stack *s)
{
	struct s_expr *e = expr;
	while (e) {
		struct atom *n = eval_atom(e->atom, s);
		if (n->t == T_ERROR) {
			error(EXIT_FAILURE, 0, "error: %s", n->v.str);
			return ATOM_FALSE;
		}

		if (n->t != T_BOOL)
			error(EXIT_FAILURE, 0, "unexpected result. expected bool, got %s", get_atom_type(n->t));

		if (!n->v.num)
			return ATOM_FALSE;

		e = e->next;
	}

	return ATOM_TRUE;
}

static struct atom *
builtin_or(struct s_expr *expr, struct stack *s)
{
	struct s_expr *e = expr;
	while (e) {
		struct atom *n = eval_atom(e->atom, s);
		if (n->t == T_ERROR) {
			error(EXIT_FAILURE, 0, "error: %s", n->v.str);
			return ATOM_FALSE;
		}

		if (n->t != T_BOOL)
			error(EXIT_FAILURE, 0, "unexpected result. expected bool, got %s", get_atom_type(n->t));

		if (n->v.num)
			return ATOM_TRUE;

		e = e->next;
	}

	return ATOM_FALSE;
}

static struct atom *
builtin_run(struct s_expr *expr, struct stack *s)
{
	struct atom *n;
	struct s_expr *e = expr;
	while (e) {
		n = eval_atom(e->atom, s);
		if (n->t == T_ERROR) {
			error(EXIT_FAILURE, 0, "error: %s", n->v.str);
			return ATOM_FALSE;
		}

		if (n->t != T_STRING)
			error(EXIT_FAILURE, 0, "unexpected result. expected string, got %s", get_atom_type(n->t));

		printf("EXEC: %s\n", n->v.str);

		e = e->next;
	}

	return ATOM_TRUE;
}

void
atom_init(struct stack *s)
{
	ATOM_TRUE = calloc(1, sizeof(struct atom));
	ATOM_TRUE->t = T_BOOL;
	ATOM_TRUE->v.num = 1;

	ATOM_FALSE = calloc(1, sizeof(struct atom));
	ATOM_FALSE->t = T_BOOL;
	ATOM_FALSE->v.num = 0;

	register_builtin(s, "not", builtin_not);
	register_builtin(s, "and", builtin_and);
	register_builtin(s, "or", builtin_or);
	register_builtin(s, "eq",  builtin_eq);
	register_builtin(s, "run", builtin_run);
}

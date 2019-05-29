#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <error.h>
#include <assert.h>

#include "uevent.h"

static struct atom *ATOM_TRUE;
static struct atom *ATOM_FALSE;

const char *
get_atom_type(enum type t)
{
	switch (t) {
		case T_BEGIN:  return "begin";
		case T_BOOL:   return "boolean";
		case T_ERROR:  return "error";
		case T_NUMBER: return "number";
		case T_PROC:   return "procedure";
		case T_STRING: return "string";
		case T_SYMBOL: return "symbol";
		case T_PAIR:   return "pair";
	}
	error(EXIT_FAILURE, 0, "unknown type=%d", t);
	return "";
}

void
print_atoms(struct atom *a)
{
	struct atom *n;

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
		case T_BEGIN:
			printf("{");
			n = a->v.pair->cdr;
			while (n) {
				if (n != a->v.pair->cdr)
					printf(" ");
				print_atoms(n->v.pair->car);
				n = n->v.pair->cdr;
			}
			printf("}");
			break;
		case T_PAIR:
			printf("(");
			n = a;
			while (n) {
				if (n != a)
					printf(" ");
				print_atoms(n->v.pair->car);
				n = n->v.pair->cdr;
			}
			printf(")");
			break;
	}
}

static struct atom *
resolve_proc(char *sym, struct stack *s)
{
	struct procs *p = s->procs;

	while (p) {
		if (!strcmp(sym, p->sym))
			return p->atom;
		p = p->next;
	}

	struct atom *a = calloc(1, sizeof(struct atom));
	a->t = T_ERROR;
	asprintf(&a->v.str, "symbol '%s' not found", sym);

	return a;
}

void *
free_atom(struct atom *a)
{
	if (!a)
		return NULL;

	switch (a->t) {
		case T_ERROR:
		case T_STRING:
		case T_SYMBOL:
			free(a->v.str);
			break;
		case T_BEGIN:
		case T_PAIR:
			if (a->v.pair->car)
				free_atom(a->v.pair->car);
			if (a->v.pair->cdr)
				free_atom(a->v.pair->cdr);
			free(a->v.pair);
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
		case T_BEGIN:
			n = a->v.pair->cdr;
			while (n) {
				r = eval_atom(n->v.pair->car, s);

				if (r->t == T_ERROR)
					error(EXIT_FAILURE, 0, "error: %s", r->v.str);

				print_atoms(r);

				if (r != n->v.pair->car)
					free_atom(r);

				n = n->v.pair->cdr;
			}
			return NULL;
		case T_PAIR:
			n = eval_atom(a->v.pair->car, s);

			if (n->t == T_ERROR)
				error(EXIT_FAILURE, 0, "error: %s", n->v.str);

			if (n->t != T_PROC)
				error(EXIT_FAILURE, 0, "unexpected atom '%s'", get_atom_type(n->t));

			r = n->v.proc(a->v.pair->cdr, s);

			if (n != a->v.pair->car)
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
	n->atom = calloc(1, sizeof(struct atom));
	n->atom->t = T_PROC;
	n->atom->v.proc = proc;

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
/*
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
*/

static struct atom *
builtin_not(struct atom *a, struct stack *s)
{
	struct atom *r = ATOM_FALSE;
	struct atom *n = eval_atom(a, s);

	if (n->t == T_BOOL && n->v.num)
		r = ATOM_TRUE;

	if (a != n)
		free_atom(n);
	return r;
}

static struct atom *
builtin_and(struct atom *a, struct stack *s)
{
	struct atom *r = ATOM_TRUE;
	struct atom *e = a;

	while (e) {
		if (e->t != T_PAIR)
			return e;

		struct atom *n = eval_atom(e->v.pair->car, s);

		if (n->t == T_ERROR)
			error(EXIT_FAILURE, 0, "error: %s", n->v.str);

		if (n->t == T_BOOL) {
			if (!n->v.num)
				return ATOM_FALSE;
			if (n != e->v.pair->car)
				free_atom(n);
		} else {
			if (r != ATOM_TRUE)
				free_atom(r);
			r = n;
		}

		e = e->v.pair->cdr;
	}

	return r;
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

/*
	register_builtin(s, "or", builtin_or);
	register_builtin(s, "eq",  builtin_eq);
	register_builtin(s, "run", builtin_run);
*/
}

void
free_stack(struct stack *s)
{
	struct procs *n, *p = s->procs;

	while (p) {
		n = p->next;
		free_atom(p->atom);
		free(p);
		p = n;
	}

	free_atom(ATOM_TRUE);
	free_atom(ATOM_FALSE);

	free(s);
}

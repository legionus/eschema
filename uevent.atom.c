#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <error.h>
#include <assert.h>

#include "uevent.h"

void *
free_atom_recursive(struct atom *a)
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
			if (ATOM_CAR(a))
				free_atom_recursive(ATOM_CAR(a));
			if (ATOM_CDR(a))
				free_atom_recursive(ATOM_CDR(a));
			free(a->v.pair);
			break;
		default:
			break;
	}

	free(a);

	return NULL;
}

void
free_stack(struct stack *s)
{
	struct procs *n, *p = s->procs;

	while (p) {
		n = p->next;
		free_atom_recursive(p->atom);
		free(p);
		p = n;
	}

	free_atom_recursive(s->atom_true);
	free_atom_recursive(s->atom_false);

	free_atom_recursive(s->root);

	free(s);
}

struct atom *
atom_inc(struct atom *a)
{
	a->refcount++;
	return a;
}

struct atom *
atom_dec(struct atom *a)
{
	a->refcount--;
	if (!a->refcount) {
		free_atom_recursive(a);
		return NULL;
	}
	return a;
}

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
print_atom(struct atom *a)
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
			n = ATOM_CDR(a);
			while (n) {
				if (n != ATOM_CDR(a))
					printf(" ");
				print_atom(ATOM_CAR(n));
				n = ATOM_CDR(n);
			}
			printf("}");
			break;
		case T_PAIR:
			printf("(");
			n = a;
			while (n) {
				if (n != a)
					printf(" ");
				print_atom(ATOM_CAR(n));
				n = ATOM_CDR(n);
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
			n = ATOM_CDR(a);
			while (n) {
				r = eval_atom(ATOM_CAR(n), s);

				if (r->t == T_ERROR)
					error(EXIT_FAILURE, 0, "error: %s", r->v.str);

				print_atom(r);
				printf("\n");

				n = ATOM_CDR(n);
			}
			return r;
		case T_PAIR:
			n = atom_inc(eval_atom(ATOM_CAR(a), s));

			if (n->t == T_ERROR)
				error(EXIT_FAILURE, 0, "error: %s", n->v.str);

			if (n->t != T_PROC)
				error(EXIT_FAILURE, 0, "unexpected atom '%s'", get_atom_type(n->t));

			r = n->v.proc(ATOM_CDR(a), s);

			return r;
	}

	return NULL;
}

static struct atom *
builtin_not(struct atom *a, struct stack *s)
{
	struct atom *r = s->atom_false;
	struct atom *n = eval_atom(a, s);

	if (n->t == T_BOOL && n->v.num)
		r = s->atom_true;

	return r;
}

static struct atom *
builtin_and(struct atom *a, struct stack *s)
{
	struct atom *r = s->atom_true;
	struct atom *e = a;

	while (e) {
		if (e->t != T_PAIR)
			return e;

		struct atom *n = eval_atom(ATOM_CAR(e), s);

		if (n->t == T_ERROR)
			error(EXIT_FAILURE, 0, "error: %s", n->v.str);

		if (n->t == T_BOOL) {
			if (!n->v.num)
				return n;
		} else {
//			if (r != ATOM_TRUE)
//				free_atom_recursive(r);
			r = n;
		}

		e = ATOM_CDR(e);
	}

	return r;
}

static struct atom *
builtin_or(struct atom *a, struct stack *s)
{
	struct atom *r = s->atom_false;
	struct atom *e = a;

	while (e) {
		if (e->t != T_PAIR)
			return e;

		struct atom *n = eval_atom(ATOM_CAR(e), s);

		if (n->t == T_ERROR)
			error(EXIT_FAILURE, 0, "error: %s", n->v.str);

		if (n->t == T_BOOL) {
			if (n->v.num)
				return n;
		} else {
//			if (r != ATOM_FALSE)
//				free_atom_recursive(r);
			r = n;
		}

		e = ATOM_CDR(e);
	}

	return r;
}

struct stack *
atom_init(void)
{
	struct stack *s = calloc(1, sizeof(struct stack));

	s->atom_true = calloc(1, sizeof(struct atom));
	s->atom_true->t = T_BOOL;
	s->atom_true->v.num = 1;

	s->atom_false = calloc(1, sizeof(struct atom));
	s->atom_false->t = T_BOOL;
	s->atom_false->v.num = 0;

	register_builtin(s, "not", builtin_not);
	register_builtin(s, "and", builtin_and);
	register_builtin(s, "or",  builtin_or);

	return s;
}

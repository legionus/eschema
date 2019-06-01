#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <error.h>
#include <assert.h>

#include "uevent.h"

static void
free_atom(struct atom *a)
{
	if (!a)
		return;

	switch (a->t) {
		case T_STRING:
		case T_SYMBOL:
			free(a->v.str);
			break;
		case T_BEGIN:
		case T_PAIR:
			free(a->v.pair);
			break;
		default:
			break;
	}

	free(a);
}

struct atom *
atom_new(enum type t)
{
	struct atom *a = calloc(1, sizeof(struct atom));
	a->t = t;
	return a;
}

struct atom *
atom_inc(struct atom *a)
{
	if (a)
		a->refcount++;
	return a;
}

void
atom_dec(struct atom *a)
{
	if (!a)
		return;

	if (--(a->refcount) > 0)
		return;

	switch (a->t) {
		case T_BEGIN:
		case T_PAIR:
			atom_dec(ATOM_CAR(a));
			atom_dec(ATOM_CDR(a));
			break;
		default:
			break;
	}

	free_atom(a);
}

const char *
get_atom_type(enum type t)
{
	switch (t) {
		case T_BEGIN:  return "begin";
		case T_BOOL:   return "boolean";
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
			printf("%d:%p", a->refcount, a->v.proc);
			break;
		case T_NUMBER:
			printf("%d:%lld", a->refcount, a->v.num);
			break;
		case T_STRING:
			printf("%d:\"%s\"", a->refcount, a->v.str);
			break;
		case T_BOOL:
			printf("%d:#%c", a->refcount, a->v.num ? 't' : 'f');
			break;
		case T_SYMBOL:
			printf("%d:%s", a->refcount, a->v.str);
			break;
		case T_BEGIN:
			printf("%d:{", a->refcount);
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
			printf("%d:(", a->refcount);
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

	error(EXIT_FAILURE, 0, "symbol '%s' not found", sym);
	return NULL;
}

int
register_builtin(struct stack *s, char *name, atom_proc_t proc)
{
	struct procs *n, *l;

	n = calloc(1, sizeof(struct procs));
	n->sym = name;

	n->atom = atom_new(T_PROC);
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
atom_eval(struct atom *a, struct stack *s)
{
	struct atom *n, *r = NULL;
	switch (a->t) {
		case T_BOOL:
		case T_NUMBER:
		case T_PROC:
		case T_STRING:
			return a;
		case T_SYMBOL:
			return resolve_proc(a->v.str, s);
		case T_BEGIN:
			while ((a = ATOM_CDR(a))) {
				if (r)
					atom_dec(r);

				r = atom_eval(ATOM_CAR(a), s);

				printf("> ");
				print_atom(r);
				printf("\n");
			}
			if (r)
				return r;
			break;
		case T_PAIR:
			n = atom_eval(ATOM_CAR(a), s);

			if (n->t != T_PROC)
				error(EXIT_FAILURE, 0, "procedure expected, got '%s'", get_atom_type(n->t));

			r = n->v.proc(ATOM_CDR(a), s);

			return atom_inc(r);
	}

	return NULL;
}

static struct atom *
builtin_not(struct atom *a, struct stack *s)
{
	struct atom *r = s->atom_false;
	struct atom *n = atom_eval(a, s);

	if (n->t == T_BOOL && n->v.num)
		r = s->atom_true;

	return r;
}

static struct atom *
builtin_and(struct atom *a, struct stack *s)
{
	struct atom *e = a;

	while (e) {
		if (e->t != T_PAIR)
			return e;

		struct atom *n = atom_eval(ATOM_CAR(e), s);

		if (n->t == T_BOOL && !n->v.num)
			return n;

		if (!ATOM_CDR(e))
			return n;

		e = ATOM_CDR(e);
	}

	return s->atom_true;
}

static struct atom *
builtin_or(struct atom *a, struct stack *s)
{
	struct atom *e = a;

	while (e) {
		if (e->t != T_PAIR)
			return e;

		struct atom *n = atom_eval(ATOM_CAR(e), s);

		if (n->t == T_BOOL && n->v.num)
			return n;

		if (!ATOM_CDR(e))
			return n;

		e = ATOM_CDR(e);
	}

	return s->atom_false;
}

struct stack *
create_stack(void)
{
	struct stack *s = calloc(1, sizeof(struct stack));

	s->atom_true = atom_new(T_BOOL);
	s->atom_true->v.num = 1;

	atom_inc(s->atom_true);

	s->atom_false = atom_new(T_BOOL);
	s->atom_false->v.num = 0;

	atom_inc(s->atom_false);

	register_builtin(s, "not", builtin_not);
	register_builtin(s, "and", builtin_and);
	register_builtin(s, "or",  builtin_or);

	return s;
}

void
free_stack(struct stack *s)
{
	struct procs *p = s->procs;

	while (p) {
		struct procs *n = p->next;
		atom_dec(p->atom);
		free(p);
		p = n;
	}

	atom_dec(s->root);
	atom_dec(s->atom_true);
	atom_dec(s->atom_false);

	free(s);
}

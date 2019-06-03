#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <error.h>
#include <assert.h>

#include "uevent.h"

static void
atom_free(struct atom *a)
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

struct atom *
atom_dec(struct atom *a)
{
	if (!a)
		return NULL;

	if (--(a->refcount) > 0)
		return a;

	switch (a->t) {
		case T_BEGIN:
		case T_PAIR:
			atom_dec(ATOM_CAR(a));
			atom_dec(ATOM_CDR(a));
			break;
		default:
			break;
	}

	atom_free(a);
	return NULL;
}

struct atom *
atom_pair(struct atom *car, struct atom *cdr)
{
	struct atom *a = atom_new(T_PAIR);

	a->v.pair = calloc(1, sizeof(struct pair));
	a->v.pair->car = car;
	a->v.pair->cdr = cdr;

	return a;
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
resolve_symbol(char *sym, struct stack *s)
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

static void
add_symbol(struct stack *s, char *name, atom_proc_t proc)
{
	struct procs *n = calloc(1, sizeof(struct procs));

	n->sym = name;
	n->next = s->procs;

	n->atom = atom_new(T_PROC);
	n->atom->v.proc = proc;

	atom_inc(n->atom);

	s->procs = n;
}

struct atom *
atom_eval(struct atom *a, struct stack *s)
{
	struct atom *n;
	switch (a->t) {
		case T_BOOL:
		case T_NUMBER:
		case T_PROC:
		case T_STRING:
			return a;
		case T_SYMBOL:
			return resolve_symbol(a->v.str, s);
		case T_BEGIN:
			while ((a = ATOM_CDR(a))) {
				n = atom_eval(ATOM_CAR(a), s);
				printf("> ");
				print_atom(n);
				printf("\n");
			}
			return atom_inc(n);
		case T_PAIR:
			n = atom_eval(ATOM_CAR(a), s);
			if (n->t != T_PROC)
				error(EXIT_FAILURE, 0, "procedure expected, got '%s'", get_atom_type(n->t));
			return n->v.proc(ATOM_CDR(a), s);
	}
	return NULL;
}

static struct atom *
proc_not(struct atom *a, struct stack *s)
{
	struct atom *r = s->atom_false;
	struct atom *n = atom_eval(a, s);

	if (n->t == T_BOOL && n->v.num)
		r = s->atom_true;

	return r;
}

static struct atom *
proc_is_symbol(struct atom *a, struct stack *s)
{
	return (a && ATOM_CAR(a) && ATOM_CAR(a)->t == T_SYMBOL) ? s->atom_true : s->atom_false;
}

static struct atom *
proc_is_boolean(struct atom *a, struct stack *s)
{
	a = ATOM_CAR(a);
	return (a && ATOM_CAR(a) && ATOM_CAR(a)->t == T_BOOL) ? s->atom_true : s->atom_false;
}

static struct atom *
proc_is_string(struct atom *a, struct stack *s)
{
	return (a && ATOM_CAR(a) && ATOM_CAR(a)->t == T_STRING) ? s->atom_true : s->atom_false;
}

static struct atom *
proc_is_number(struct atom *a, struct stack *s)
{
	a = ATOM_CAR(a);
	return (a && ATOM_CAR(a) && ATOM_CAR(a)->t == T_NUMBER) ? s->atom_true : s->atom_false;
}

static struct atom *
proc_is_procedure(struct atom *a, struct stack *s)
{
	a = ATOM_CAR(a);
	return (a && ATOM_CAR(a) && ATOM_CAR(a)->t == T_PROC) ? s->atom_true : s->atom_false;
}

static struct atom *
proc_and(struct atom *a, struct stack *s)
{
	while (a) {
		if (a->t != T_PAIR)
			return a;

		struct atom *n = atom_eval(ATOM_CAR(a), s);

		if (n->t == T_BOOL && !n->v.num)
			return n;

		if (!ATOM_CDR(a))
			return n;

		if (!n->refcount)
			atom_dec(n);

		a = ATOM_CDR(a);
	}

	return s->atom_true;
}

static struct atom *
proc_or(struct atom *a, struct stack *s)
{
	while (a) {
		if (a->t != T_PAIR)
			return a;

		struct atom *n = atom_eval(ATOM_CAR(a), s);

		if (n->t == T_BOOL && n->v.num)
			return n;

		if (!ATOM_CDR(a))
			return n;

		if (!n->refcount)
			atom_dec(n);

		a = ATOM_CDR(a);
	}

	return s->atom_false;
}

static struct atom *
proc_add(struct atom *a, struct stack *s)
{
	int pos = 0;
	long long int v = 0;

	while (a) {
		pos++;

		struct atom *n = atom_eval(ATOM_CAR(a), s);

		if (n->t != T_NUMBER)
			error(EXIT_FAILURE, 0, "In procedure '+': Wrong type argument in position %d", pos);

		v += n->v.num;

		if (!n->refcount)
			atom_dec(n);

		a = ATOM_CDR(a);
	}

	a = atom_new(T_NUMBER);
	a->v.num = v;

	return a;
}

static struct atom *
proc_multiply(struct atom *a, struct stack *s)
{
	int pos = 0;
	long long int v = 1;

	while (a) {
		pos++;

		struct atom *n = atom_eval(ATOM_CAR(a), s);

		if (n->t != T_NUMBER)
			error(EXIT_FAILURE, 0, "In procedure '+': Wrong type argument in position %d", pos);

		v *= n->v.num;

		if (!n->refcount)
			atom_dec(n);

		a = ATOM_CDR(a);
	}

	a = atom_new(T_NUMBER);
	a->v.num = v;

	return a;
}

static struct atom *
proc_sub(struct atom *a, struct stack *s)
{
	int pos = 0;
	long long int v = 0;

	while (a) {
		pos++;

		struct atom *n = atom_eval(ATOM_CAR(a), s);

		if (n->t != T_NUMBER)
			error(EXIT_FAILURE, 0, "In procedure '-': Wrong type argument in position %d", pos);

		if (pos == 1)
			v = n->v.num;
		else
			v -= n->v.num;

		if (!n->refcount)
			atom_dec(n);

		a = ATOM_CDR(a);
	}

	a = atom_new(T_NUMBER);
	a->v.num = (pos == 1) ? -v : v;

	return a;
}

static struct atom *
proc_if(struct atom *a, struct stack *s)
{
	struct atom *n, *test, *if_true, *if_false = NULL;

	test = ATOM_CAR(a);

	if (!ATOM_CDR(a))
		error(EXIT_FAILURE, 0, "source expression failed to find consequent expression");

	a = ATOM_CDR(a);

	if_true = ATOM_CAR(a);

	if (ATOM_CDR(a))
		if_false = ATOM_CDR(a);

	n = atom_eval(test, s);

	if (n->t != T_BOOL || (n->t == T_BOOL && n->v.num)) {
		if (!n->refcount)
			atom_dec(n);

		return atom_eval(if_true, s);
	}

	if (!n->refcount)
		atom_dec(n);

	if (if_false)
		return atom_eval(if_false, s);

	return s->atom_true;
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

	add_symbol(s, "not",        proc_not);
	add_symbol(s, "and",        proc_and);
	add_symbol(s, "or",         proc_or);
	add_symbol(s, "if",         proc_if);
	add_symbol(s, "symbol?",    proc_is_symbol);
	add_symbol(s, "boolean?",   proc_is_boolean);
	add_symbol(s, "string?",    proc_is_string);
	add_symbol(s, "number?",    proc_is_number);
	add_symbol(s, "procedure?", proc_is_procedure);
	add_symbol(s, "+",          proc_add);
	add_symbol(s, "-",          proc_sub);
	add_symbol(s, "*",          proc_multiply);

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

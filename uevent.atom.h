#ifndef _UEVENT_ATOM_H_
#define _UEVENT_ATOM_H_

struct atom;

struct pair {
	struct atom *car;
	struct atom *cdr;
};

enum type {
	T_BEGIN,
	T_BOOL,
	T_NUMBER,
	T_PROC,
	T_STRING,
	T_SYMBOL,
	T_PAIR,
};

struct stack;
typedef struct atom *(*atom_proc_t)(const char *name, struct atom *, struct stack *);

struct proc {
	const char *name;
	atom_proc_t func;
};

union value {
	long long int num;
	char *str;
	struct proc *proc;
	struct pair *pair;
};

#define ATOM_CAR(x) (x->v.pair->car)
#define ATOM_CDR(x) (x->v.pair->cdr)

struct atom {
	int refcount;
	enum type t;
	union value v;
};

struct procs {
	char *sym;
	struct atom *atom;
	struct procs *next;
};

struct stack {
	struct atom *root;

	struct atom *atom_true;
	struct atom *atom_false;

	struct procs *procs;
};

void print_atom(struct atom *a);
const char *get_atom_type(enum type t);

struct atom *atom_eval(struct atom *a, struct stack *s);

struct stack *create_stack(void);
void free_stack(struct stack *s);

struct atom *atom_pair(struct atom *car, struct atom *cdr);
struct atom *atom_new(enum type t);
struct atom *atom_inc(struct atom *a);
struct atom *atom_dec(struct atom *a);

#endif /* _UEVENT_ATOM_H_ */

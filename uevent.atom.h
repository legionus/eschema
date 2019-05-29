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
	T_ERROR,
	T_NUMBER,
	T_PROC,
	T_STRING,
	T_SYMBOL,
	T_PAIR,
};

struct stack;
typedef struct atom *(*atom_proc_t)(struct atom *, struct stack *);

union value {
	long long int num;
	char *str;
	atom_proc_t proc;
	struct pair *pair;
};

#define ATOM_CAR(x) x->v.pair->car
#define ATOM_CDR(x) x->v.pair->cdr

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
	struct atom *atom_true;
	struct atom *atom_false;
	struct procs *procs;
};

struct stack *atom_init(void);
void print_atom(struct atom *a);
const char *get_atom_type(enum type t);
struct atom *eval_atom(struct atom *a, struct stack *s);
void *free_atom_recursive(struct atom *a);
void free_stack(struct stack *s);

struct atom *atom_inc(struct atom *a);
struct atom *atom_dec(struct atom *a);

int register_builtin(struct stack *s, char *name, atom_proc_t proc);

#endif /* _UEVENT_ATOM_H_ */

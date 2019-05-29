#ifndef _UEVENT_ATOM_H_
#define _UEVENT_ATOM_H_

struct atom;

struct s_expr {
	struct atom *atom;
	struct s_expr *next;
};

enum type {
	T_BEGIN,
	T_BOOL,
	T_ERROR,
	T_NUMBER,
	T_PROC,
	T_STRING,
	T_SYMBOL,
	T_S_EXPR,
};

struct stack;
typedef struct atom *(*atom_proc_t)(struct s_expr *, struct stack *);

union value {
	long long int num;
	char *str;
	atom_proc_t proc;
	struct s_expr *s_expr;
};

struct atom {
	enum type t;
	union value v;
};

struct procs {
	char *sym;
	struct atom *atom;
	struct procs *next;
};

struct stack {
	struct procs *procs;
};

void atom_init(struct stack *s);
void print_atoms(struct atom *a);
const char *get_atom_type(enum type t);
struct atom *eval_atom(struct atom *a, struct stack *s);
void *free_atom(struct atom *a);

int register_builtin(struct stack *s, char *name, atom_proc_t proc);

#endif /* _UEVENT_ATOM_H_ */

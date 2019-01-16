/* Library inclusions */
#include "mpc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* If we are compiling on Windows compile these functions */
#ifdef _WIN32

/*A static global variable or a function is "seen" only in
the file it's declared in */
static char buffer[2048];

/* Fake readline function */
char* readline(char* prompt) {
  fputs(prompt, stdout);
  fgets(buffer, 2048, stdin);
  char* cpy = malloc(strlen(buffer)+1);
  strcpy(cpy, buffer);
  cpy[strlen(cpy)-1] = '\0';
  return cpy;
}

/* Fake add_history function */
void add_history(char* unused) {}

/* Otherwise include the editline headers */
#else
#include <editline/readline.h>
#include <editline/history.h>
#endif

/* Forward declarations of lval and lenv */
struct lval;
struct lenv;
typedef struct lval lval;
typedef struct lenv lenv;

/* Lisp value create enumeration of possible lval types */
enum { LVAL_ERR, LVAL_NUM, LVAL_SYM,
       LVAL_FUN, LVAL_SEXPR, LVAL_QEXPR };

/* Declare a new function pointer type called lbuiltin */
typedef lval*(*lbuiltin)(lenv*, lval*);

/* Decalre new Lval struct */
struct lval {
  int type;
  double num;

  /* Error and Symbol types have some string data */
  char* err;
  char* sym;

  lbuiltin fun;

  /* Count and Pointer to a list of "lval*" */
  int count;
  lval** cell;
};

/* Construct a pointer to a new Number lval */
/* Remember! foo->bar is equivalent to (*foo).bar, i.e. it
gets the member called bar from the struct that foo points to. */
lval* lval_num(double x) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_NUM;
  v->num = x;
  return v;
}

/* Construct a pointer to a new Error lval */
lval* lval_err(char* fmt, ...) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_ERR;

  /* Create a va list and initialize it */
  va_list va;
  va_start(va, fmt);

  /* Allocate 512 bytes of space */
  v->err = malloc(512);

  /* printf the error string with a maximum of 511 characters */
  vsnprintf(v->err, 511, fmt, va);

  /* Reallocate to number of bytes actually used */
  v->err = realloc(v->err, strlen(v->err)+1);

  /* Cleanup our va list */
  va_end(va);

  return v;
}

/* Construct a pointer to a new Symbol lval */
lval* lval_sym(char* s) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_SYM;
  v->sym = malloc(strlen(s) + 1);
  strcpy(v->sym, s);
  return v;
}

/* Construct a pointer to a new Function lval */
lval* lval_fun(lbuiltin func) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_FUN;
  v->fun = func;
  return v;
}

/* A pointer to a new empty Sexpr lval */
lval* lval_sexpr(void) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_SEXPR;
  v->count = 0;
  v->cell = NULL;
  return v;
}

/* A pointer to a new empty Qexpr lval */
lval* lval_qexpr(void) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_QEXPR;
  v->count = 0;
  v->cell = NULL;
  return v;
}

/* A function to delete an lval* */
void lval_del(lval* v) {
  switch (v->type) {
    /* Do nothing special for Number and Function type */
    case LVAL_NUM: break;
    case LVAL_FUN: break;

    /* For Err or Sym free the string data */
    case LVAL_ERR: free(v->err); break;
    case LVAL_SYM: free(v->sym); break;

    /* If Sexpr then delete all elements inside */
    case LVAL_SEXPR:
    case LVAL_QEXPR:
      for (int i = 0; i < v->count; i++) {
        lval_del(v->cell[i]);
      }
      /* Also free the memory allocated to contain the pointers */
      free(v->cell);
    break;
  }

  /* Free the memory allocated for the "lval" struct itself */
  free(v);
}

/* A function to copy an lval */
lval* lval_copy(lval* v) {
  lval* x = malloc(sizeof(lval));
  x->type = v->type;

  switch (v->type) {
    /* Copy Functions and Numbers directly */
    case LVAL_FUN: x->fun = v->fun; break;
    case LVAL_NUM: x->num = v->num; break;

    /* Copy Strings using malloc and strcpy */
    case LVAL_ERR:
      x->err = malloc(strlen(v->err) + 1);
      strcpy(x->err, v->err); break;

    case LVAL_SYM:
      x->sym = malloc(strlen(v->sym) + 1);
      strcpy(x->sym, v->sym); break;

    /* Copy Lists by copying each sub-expression */
    case LVAL_SEXPR:
    case LVAL_QEXPR:
      x->count = v->count;
      x->cell = malloc(sizeof(lval*) * x->count);
      for (int i = 0; i < x->count; i++) {
        x->cell[i] = lval_copy(v->cell[i]);
      }
    break;
  }

  return x;
}

/* A function to add an element to an S-Expression */
lval* lval_add(lval* v, lval* x) {
  v->count++;
  v->cell = realloc(v->cell, sizeof(lval*) * v->count);
  v->cell[v->count-1] = x;
  return v;
}

/* A helper function for builtin_join */
lval* lval_join(lval* x, lval* y) {
  /* For each cell in 'y' add it to 'x' */
  for (int i = 0; i < y->count; i++) {
    x = lval_add(x, y->cell[i]);
  }

  /* Free the empty 'y' and return 'x' */
  free(y->cell);
  free(y);
  return x;
}

/* A function that extracts a single element form an S-expression at
index i and shifts the rest of the list backward so that it no longer
contains that lval*. */
lval* lval_pop(lval* v, int i) {
  /* Find the item at "i" */
  lval* x = v->cell[i];

  /* Shift memory after the item at "i" over the top */
  memmove(&v->cell[i], &v->cell[i+1],
    sizeof(lval*) * (v->count-i-1));

  /* Decrease the count of items in the list */
  v->count--;

  /* Reallocate the memory used */
  v->cell = realloc(v->cell, sizeof(lval*) * v->count);
  return x;
}

/* A function similar to lval_pop(), instead it deltes the list it
has extracted the element from */
lval* lval_take(lval* v, int i) {
  lval* x = lval_pop(v, i);
  lval_del(v);
  return x;
}

/* Forward declaration of lval_print function */
void lval_print(lval* v);

/* A function to print S-expressions */
void lval_expr_print(lval* v, char open, char close) {
  putchar(open);
  for (int i = 0; i < v->count; i++) {
    /* Print Value contained within */
    lval_print(v->cell[i]);

    /* Don't print trailing space if last element */
    if (i != (v->count-1)) {
      putchar(' ');
    }
  }
  putchar(close);
}

/* Print an "lval" switch statements */
void lval_print(lval* v) {
  switch (v->type) {
    case LVAL_NUM:   printf("%f", v->num); break;
    case LVAL_ERR:   printf("Error: %s", v->err); break;
    case LVAL_SYM:   printf("%s", v->sym); break;
    case LVAL_FUN:   printf("<function>"); break;
    case LVAL_SEXPR: lval_expr_print(v, '(', ')'); break;
    case LVAL_QEXPR: lval_expr_print(v, '{', '}'); break;
  }
}

/* Print an "lval" followed by a newline */
void lval_println(lval* v) { lval_print(v); putchar('\n'); }

/* A function that converts a type enumeration to a string
representation of that type. */
char* ltype_name(int t) {
  switch(t) {
    case LVAL_FUN: return "Function";
    case LVAL_NUM: return "Number";
    case LVAL_ERR: return "Error";
    case LVAL_SYM: return "Symbol";
    case LVAL_SEXPR: return "S-Expression";
    case LVAL_QEXPR: return "Q-Expression";
    default: return "Unknown";
  }
}

/* LISP ENVIRONMENT */
/* Declare new Lenv struct */
struct lenv {
  int count;
  char** syms;
  lval** vals;
};

/* A function to initialize an Lenv */
lenv* lenv_new(void) {
  lenv* e = malloc(sizeof(lenv));
  e->count = 0;
  e->syms = NULL;
  e->vals = NULL;
  return e;
}

/* A function to delete an Lenv */
void lenv_del(lenv* e) {
  /* Iterate over all items in environment deleting them */
  for (int i = 0; i < e->count; i++) {
    free(e->syms[i]);
    lval_del(e->vals[i]);
  }

  /* Free allocated memory for lists */
  free(e->syms);
  free(e->vals);
  free(e);
}

/* A function to get a variable from the environment */
lval* lenv_get(lenv* e, lval* k) {
  /* Iterate over all items in environment */
  for (int i = 0; i < e->count; i++) {
    /* Check if the stored string matches the symbol string */
    /* If it does, return a copy of the value */
    if (strcmp(e->syms[i], k->sym) == 0) {
      return lval_copy(e->vals[i]);
    }
  }
  /* If no symbol found return error */
  return lval_err("Unbound symbol '%s'!", k->sym);
}


/* A function to put new variables into the environment */
void lenv_put(lenv* e, lval* k, lval* v) {
  /* Iterate over all items in environment */
  /* This is to see if variable already exists */
  for (int i = 0; i < e->count; i++) {

    /* If variable is found delete item at that position */
    /* And replace with variable supplied by user */
    if (strcmp(e->syms[i], k->sym) == 0) {
      lval_del(e->vals[i]);
      e->vals[i] = lval_copy(v);
      return;
    }
  }

  /* If no existing entry found allocate space for new entry */
  e->count++;
  e->vals = realloc(e->vals, sizeof(lval*) * e->count);
  e->syms = realloc(e->syms, sizeof(char*) * e->count);

  /* Copy contents of lval and symbol string into new location */
  e->vals[e->count-1] = lval_copy(v);
  e->syms[e->count-1] = malloc(strlen(k->sym)+1);
  strcpy(e->syms[e->count-1], k->sym);
}


/* BUILTINS */

#define LASSERT(args, cond, fmt, ...) \
  if (!(cond)) { \
    lval* err = lval_err(fmt, ##__VA_ARGS__); \
    lval_del(args); \
    return err; \
}

#define LASSERT_TYPE(func, args, index, expect) \
  LASSERT(args, args->cell[index]->type == expect, \
    "Function '%s' passed incorrect type for argument %i. Got %s, but "\
    "expected %s.", \
    func, index, ltype_name(a->cell[index]->type), ltype_name(expect))

#define LASSERT_ARGS(func, args, num) \
  LASSERT(args, args->count == num, \
    "Function '%s' passed incorrect number of arguments. Got %i, but "\
    "expected %i.", \
    func, args->count, num)

#define LASSERT_EMPTY(func, args) \
  LASSERT(args, args->cell[0]->count != 0, \
    "Function '%s' passed empty Q-expression!", \
    func)

/* Forward declaration of lval evaluation function */
lval* lval_eval(lenv* e, lval* v);

/* A function that converts the input S-Expression
to a Q-Expression and returns it */
lval* builtin_list(lenv* e, lval* a) {
  a->type = LVAL_QEXPR;
  return a;
}

/* A function that takes a Q-Expression and returns a
Q-Expression with only the first element */
lval* builtin_head(lenv* e, lval* a) {
  /* Check error conditions */
  LASSERT_ARGS("head", a, 1);

  LASSERT_TYPE("head", a, 0, LVAL_QEXPR);

  LASSERT_EMPTY("head", a);

  /* Otherwise take first argument */
  lval* v = lval_take(a, 0);

  /* Delete all elements that are not head and return */
  while (v->count > 1) { lval_del(lval_pop(v, 1)); }
  return v;
}

/* A function that takes a Q-Expression and returns a
Q-Expression with the first element removed */
lval* builtin_tail(lenv* e, lval* a) {
  /* Check error conditions */
  LASSERT_ARGS("tail", a, 1);

  LASSERT_TYPE("tail", a, 0, LVAL_QEXPR);

  LASSERT_EMPTY("tail", a);

  /* Take first argument */
  lval* v = lval_take(a, 0);

  /* Delete first element and return */
  lval_del(lval_pop(v, 0));
  return v;
}

/* A function that takes as input some single Q-expression,
which it converts to an S-Expression, and evaluates using
lval_eval() */
lval* builtin_eval(lenv* e, lval* a) {
  LASSERT_ARGS("eval", a, 1);

  LASSERT_TYPE("eval", a, 0, LVAL_QEXPR);

  lval* x = lval_take(a, 0);
  x->type = LVAL_SEXPR;
  return lval_eval(e, x);
}

/* A function that joins Q-expressions together, one by one */
lval* builtin_join(lenv* e, lval* a) {
  for (int i = 0; i < a->count; i++) {
    LASSERT_TYPE("join", a, i, LVAL_QEXPR);
  }

  lval* x = lval_pop(a, 0);

  while (a->count) {
    x = lval_join(x, lval_pop(a, 0));
  }

  lval_del(a);
  return x;
}

/* A function to evaluate operators (symbols) */
lval* builtin_op(lenv* e, lval* a, char* op) {
  /* Ensure all arguments are numbers */
  for (int i = 0; i < a->count; i++) {
    if (a->cell[i]->type != LVAL_NUM) {
      lval_del(a);
      return lval_err("Cannot operate on non-number!");
    }
  }

  /* Pop the first element */
  lval* x = lval_pop(a, 0);

  /* If no arguments and sub then perform unary negation */
  if ((strcmp(op, "-") == 0) && a->count == 0) {
    x->num = -x->num;
  }

  /* While there are still elements remaining */
  while (a->count > 0) {
    /* Pop the next element */
    lval* y = lval_pop(a, 0);

    if (strcmp(op, "+") == 0 || strcmp(op, "add") == 0) { x->num += y->num; }
    if (strcmp(op, "-") == 0 || strcmp(op, "sub") == 0) { x->num -= y->num; }
    if (strcmp(op, "*") == 0 || strcmp(op, "mul") == 0) { x->num *= y->num; }

    if (strcmp(op, "min") == 0) {
      (x->num <= y->num) ? (x->num = x->num) : (x->num = y->num);
    }

    if (strcmp(op, "max") == 0) {
      (x->num <= y->num) ? (x->num = y->num) : (x->num = x->num);
    }

    if (strcmp(op, "%") == 0) {
      int c = (int) x->num % (int) y->num;
      x->num = (double) c;
    }

    if (strcmp(op, "/") == 0 || strcmp(op, "div") == 0) {
      if (y->num == 0) {
        lval_del(x); lval_del(y);
        x = lval_err("Division by zero!"); break;
      }

      x->num /= y->num;
    }

    lval_del(y);
  }

  lval_del(a); return x;
}

/* A function that adds two values */
lval* builtin_add(lenv* e, lval* a) {
  return builtin_op(e, a, "+");
}

/* A function that subtracts two values */
lval* builtin_sub(lenv* e, lval* a) {
  return builtin_op(e, a, "-");
}

/* A function that multiplies two values */
lval* builtin_mul(lenv* e, lval* a) {
  return builtin_op(e, a, "*");
}

/* A function that divides two values */
lval* builtin_div(lenv* e, lval* a) {
  return builtin_op(e, a, "/");
}

/* A function that performs the modulo of two numbers */
lval* builtin_mod(lenv* e, lval* a) {
  return builtin_op(e, a, "%");
}

/* A function that returns the minimum value of a sequence of numbers */
lval* builtin_min(lenv* e, lval* a) {
  return builtin_op(e, a, "min");
}

/* A function that returns the maximum value of a sequence of numbers */
lval* builtin_max(lenv* e, lval* a) {
  return builtin_op(e, a, "max");
}

/* A function to define variables */
lval* builtin_def(lenv* e, lval* a) {
  LASSERT_TYPE("def", a, 0, LVAL_QEXPR);

  /* First argument is symbol list */
  lval* syms = a->cell[0];

  /* Ensure all elements of first list are symbols */
  for (int i = 0; i < syms->count; i++) {
    LASSERT(a, syms->cell[i]->type == LVAL_SYM,
    "Function 'def' cannot define non-symbol!");
  }

  /* Check correct number of symbols and values */
  LASSERT(a, syms->count == a->count-1,
    "Function 'def' cannot define incorrect "
    "number of values to symbols!");

  /* Assign copies of values to symbols */
  for (int i = 0; i < syms->count; i++) {
    lenv_put(e, syms->cell[i], a->cell[i+1]);
  }

  lval_del(a);
  return lval_sexpr();
}

/* A helper function to lenv_add_builtins */
void lenv_add_builtin(lenv* e, char* name, lbuiltin func) {
  lval* k = lval_sym(name);
  lval* v = lval_fun(func);
  lenv_put(e, k, v);
  lval_del(k); lval_del(v);
}

/* A function that registers all builtins into an environment */
void lenv_add_builtins(lenv* e) {
  /* Variable Functions */
  lenv_add_builtin(e, "def", builtin_def);

  /* List Functions */
  lenv_add_builtin(e, "list", builtin_list);
  lenv_add_builtin(e, "head", builtin_head);
  lenv_add_builtin(e, "tail", builtin_tail);
  lenv_add_builtin(e, "eval", builtin_eval);
  lenv_add_builtin(e, "join", builtin_join);

  /* Mathematical Functions */
  lenv_add_builtin(e, "+", builtin_add);
  lenv_add_builtin(e, "-", builtin_sub);
  lenv_add_builtin(e, "*", builtin_mul);
  lenv_add_builtin(e, "/", builtin_div);
  lenv_add_builtin(e, "%", builtin_mod);
  lenv_add_builtin(e, "min", builtin_min);
  lenv_add_builtin(e, "max", builtin_max);
}

/* EVALUATION */
/* A function that evaluates S-expressions" */
lval* lval_eval_sexpr(lenv* e, lval* v) {
  for (int i = 0; i < v->count; i++) {
    v->cell[i] = lval_eval(e, v->cell[i]);
  }

  for (int i = 0; i < v->count; i++) {
    if (v->cell[i]->type == LVAL_ERR) { return lval_take(v, i); }
  }

  if (v->count == 0) { return v; }
  if (v->count == 1) { return lval_take(v, 0); }

  /* Ensure first element is a function after evaluation */
  lval* f = lval_pop(v, 0);
  if (f->type != LVAL_FUN) {
    lval_del(v); lval_del(f);
    return lval_err("First element is not a function!");
  }

  /* If so, call function to get result */
  lval* result = f->fun(e, v);
  lval_del(f);
  return result;
}

/* A function that evaluates variables */
lval* lval_eval(lenv* e, lval* v) {
  if (v->type == LVAL_SYM) {
    lval* x = lenv_get(e, v);
    lval_del(v);
    return x;
  }

  if (v->type == LVAL_SEXPR) { return lval_eval_sexpr(e, v); }
  return v;
}

/* READING */
/* A function to read a number from an S-expression */
lval* lval_read_num(mpc_ast_t* t) {
  errno = 0;
  double x = strtod(t->contents, NULL);
  return errno != ERANGE ?
    lval_num(x) : lval_err("invalid number");
}

/* A function to read an Lval */
lval* lval_read(mpc_ast_t* t) {

  /* If Symbol or Number return conversion to that type */
  if (strstr(t->tag, "number")) { return lval_read_num(t); }
  if (strstr(t->tag, "symbol")) { return lval_sym(t->contents); }

  /* If root (>) or sexpr then create empty list */
  lval* x = NULL;
  if (strcmp(t->tag, ">") == 0) { x = lval_sexpr(); }
  if (strstr(t->tag, "sexpr"))  { x = lval_sexpr(); }
  if (strstr(t->tag, "qexpr"))  { x = lval_qexpr(); }

  /* Fill this list with any valid expression contained within */
  for (int i = 0; i < t->children_num; i++) {
    if (strcmp(t->children[i]->contents, "(") == 0) { continue; }
    if (strcmp(t->children[i]->contents, ")") == 0) { continue; }
    if (strcmp(t->children[i]->contents, "{") == 0) { continue; }
    if (strcmp(t->children[i]->contents, "}") == 0) { continue; }
    if (strcmp(t->children[i]->tag,  "regex") == 0) { continue; }
    x = lval_add(x, lval_read(t->children[i]));
  }

  return x;
}

/* MAIN */
int main(int argc, char** argv) {
  /* Create Some Parsers */
  mpc_parser_t* Number   = mpc_new("number");
  mpc_parser_t* Symbol = mpc_new("symbol");
  mpc_parser_t* Sexpr = mpc_new("sexpr");
  mpc_parser_t* Qexpr = mpc_new("qexpr");
  mpc_parser_t* Expr     = mpc_new("expr");
  mpc_parser_t* Skippy = mpc_new("skippy");

  /* Define them with the following Language */
  mpca_lang(MPCA_LANG_DEFAULT,
    "                                                       \
      number   : /-?[0-9]+([.][0-9]+)?/ ;                   \
      symbol : /[a-zA-Z0-9_+\\-*\\/\\\\=<>!&%]+/ ;          \
      sexpr : '(' <expr>* ')' ;                             \
      qexpr : '{' <expr>* '}' ;                             \
      expr     : <number> | <symbol> | <sexpr>  | <qexpr> ; \
      skippy: /^/ <expr>* /$/ ;                             \
    ",
    Number, Symbol, Sexpr, Qexpr, Expr, Skippy);

  puts("Skippy Version 0.0.0.0.7");
  puts("Author: Bas Straathof");
  puts("Press Ctrl+c to Exit\n");

  /* Create an environment */
  lenv* e = lenv_new();

  /* Call the lenv_add_builtins() function */
  lenv_add_builtins(e);

  while (1) {
    /* Now in either case readline will be correctly defined */
    char* input = readline("skippy> ");
    add_history(input);

    /* Attempt to parse the user Input */
    mpc_result_t r;
    if (mpc_parse("<stdin>", input, Skippy, &r)) {
      lval* x = lval_eval(e, lval_read(r.output));
      lval_println(x);
      lval_del(x);
      mpc_ast_delete(r.output);
    } else {
      /* Otherwise Print the Error */
      mpc_err_print(r.error);
      mpc_err_delete(r.error);
    }

    free(input);
  }

  /* Delete the environment */
  lenv_del(e);

  /* Undefine and Delete our Parsers */
  mpc_cleanup(5, Number, Symbol, Sexpr, Qexpr, Expr, Skippy);

  return 0;
}

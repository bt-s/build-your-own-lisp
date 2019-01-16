#include "mpc.h"
#include <stdio.h>
#include <stdlib.h>

/* If we are compiling on Windows compile these functions */
#ifdef _WIN32
#include <string.h>

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

/* Decalre New lval Struct */
typedef struct {
  int type;
  long num;
  int err;
} lval;

/* Create Enumeration of Possible lval Types */
enum { LVAL_NUM, LVAL_ERR };

/* Create Enumeration of Possible Error Types */
enum { LERR_DIV_ZERO, LERR_BAD_OP, LERR_BAD_NUM };

/* Create a new number type lval */
lval lval_num(long x) {
  lval v;
  v.type = LVAL_NUM;
  v.num = x;
  return v;
}

/* Create a new error type lval */
lval lval_err(int x) {
  lval v;
  v.type = LVAL_ERR;
  v.err = x;
  return v;
}

/* Print an "lval" */
void lval_print(lval v) {
  switch (v.type) {
    /* In the case the type is a number pint it */
    /* Then 'break' out of the switch */
    case LVAL_NUM: printf("%li", v.num); break;

    /* In the case the type is an error */
    case LVAL_ERR:
      /* Check what type of error it is and print it */
      if (v.err == LERR_DIV_ZERO) {
        printf("Error: Division by Zero!");
      }
      if (v.err == LERR_BAD_OP) {
        printf("Error: Invalid operator!");
      }
      if (v.err == LERR_BAD_NUM) {
        printf("Error: Invalid Number!");
      }
    break;
  }
}

/* Print an "lval" followed by a newline */
void lval_println(lval v) {
  lval_print(v);
  putchar('\n');
}

int number_of_nodes(mpc_ast_t* t) {
  if (t->children_num == 0) { return 1; }
  if (t->children_num >= 1) {
    int total = 1;
    for (int i = 0; i < t->children_num; i++) {
      total = total + number_of_nodes(t->children[i]);
    }
    return total;
  }
  return 0;
}

int number_of_leaves(mpc_ast_t* t) {
  if (t->children_num == 0) { return 1; }
  if (t->children_num >= 1) {
    int total = 0;
    for (int i = 0; i < t->children_num; i++) {
      total = total + number_of_leaves(t->children[i]);
    }
    return total;
  }
  return 0;
}

lval eval_op(lval x, char* op, lval y) {
  /* If either value is an error return it */
  if (x.type == LVAL_ERR) { return x; }
  if (y.type == LVAL_ERR) { return y; }

  if (strcmp(op, "+") == 0) {
    return lval_num(x.num + y.num);
  }
  if (strcmp(op, "-") == 0) { return lval_num(x.num - y.num); }
  if (strcmp(op, "*") == 0) { return lval_num(x.num * y.num); }
  if (strcmp(op, "%") == 0) { return lval_num(x.num % y.num); }
  if (strcmp(op, "min") == 0) { return (x.num <= y.num) ? lval_num(x.num) : lval_num(y.num); }
  if (strcmp(op, "max") == 0) { return (x.num <= y.num) ? lval_num(y.num) : lval_num(x.num); }

  if (strcmp(op, "/") == 0) {
    /* If second operand is zero return erro */
    return y.num == 0
      ? lval_err(LERR_DIV_ZERO)
      : lval_num(x.num / y.num);
  }

  if (strcmp(op, "^") == 0) {
    long power = 1;
    for (int i = 0; i < y.num; i++) {
      power = x.num * power;
    }
    return lval_num(power);
  }

  return lval_err(LERR_BAD_OP);
}

lval eval(mpc_ast_t* t) {
  /* If tagged as number return it directly. */
  if (strstr(t->tag, "number")) {
    /* Check if there is some error in conversion */
    errno = 0;
    long x = strtol(t->contents, NULL, 10);
    return errno != ERANGE ? lval_num(x) : lval_err(LERR_BAD_NUM);
  }

  /* The operator is always second child. */
  char* op = t->children[1]->contents;

  /* We store the third child in `x` */
  lval x = eval(t->children[2]);

  /* Negate the "-" operator if it only receives one input argument */
  //if (strcmp(op, "-") == 0 && t->children_num < 5) { return lval_num(0) - lval_num(x); }

  /* Iterate the remaining children and combining. */
  int i = 3;
  while (strstr(t->children[i]->tag, "expr")) {
    x = eval_op(x, op, eval(t->children[i]));
    i++;
  }

  return x;
}

int main(int argc, char** argv) {
  /* Create Some Parsers */
  mpc_parser_t* Number   = mpc_new("number");
  mpc_parser_t* Operator = mpc_new("operator");
  mpc_parser_t* Expr     = mpc_new("expr");
  mpc_parser_t* Skippy = mpc_new("skippy");

  /* Define them with the following Language */
  mpca_lang(MPCA_LANG_DEFAULT,
    "                                                     \
      number   : /-?[0-9]+([.][0-9]+)?/ ;                 \
      operator : '+' | '-' | '*' | '/' | '%' | '^' |      \
                 \"min\" | \"max\" | \"add\" | \"sub\" |  \
                 \"mul\" | \"div\" ;                      \
      expr     : <number> | '(' <operator> <expr>+ ')' ;  \
      skippy: /^/ <operator> <expr>+ /$/ ;                \
    ",
    Number, Operator, Expr, Skippy);

  puts("Skippy Version 0.0.0.0.4");
  puts("Author: Bas Straathof");
  puts("Press Ctrl+c to Exit\n");

  while (1) {
    /* Now in either case readline will be correctly defined */
    char* input = readline("skippy> ");
    add_history(input);

    /* Attempt to Parse the user Input */
    mpc_result_t r;
    if (mpc_parse("<stdin>", input, Skippy, &r)) {
      /* On Success Print the AST */
      mpc_ast_print(r.output);

      /* Load AST from output */
      mpc_ast_t* a = r.output;
      printf("Number of children: %i\n", a->children_num);

      long num_nodes = number_of_nodes(r.output);
      printf("Number of nodes: %li\n", num_nodes);

      long num_leaves = number_of_leaves(r.output);
      printf("Number of leaves: %li\n", num_leaves);

      lval result = eval(r.output);
      lval_println(result);
      mpc_ast_delete(r.output);
    } else {
      /* Otherwise Print the Error */
      mpc_err_print(r.error);
      mpc_err_delete(r.error);
    }

    free(input);
  }

  /* Undefine and Delete our Parsers */
  mpc_cleanup(4, Number, Operator, Expr, Skippy);

  return 0;
}

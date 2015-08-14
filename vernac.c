#include "vernac.h"
#include "telescope.h"
#include "context.h"
#include "typing_context.h"
#include "typecheck.h"
#include "term.h"
#include "normalize.h"
#include "parser.h"
#include "printing.h"
#include "elaborate.h"

#include <stdlib.h>

static telescope *Gamma;
static context *Sigma;
static typing_context *Delta;
static char* wd;

void vernac_init(char* working_directory) {
  Gamma = telescope_empty();
  Sigma = context_empty();
  Delta = typing_context_empty();
  wd = strdup(working_directory);
}

int print_command(FILE* stream, command* c) {
  if (c == NULL) return fprintf(stream, "NULL");

  switch (c->tag) {
  case DEF:
    return fprintf(stream, "def %W : %W := %W.",
                   c->var, print_variable,
                   c->left, print_term,
                   c->right, print_term);
  default:
    return fprintf(stream, "<command>");
  }
}

static void vernac_run_def(command* c) {
  term* Type = make_type();
  term* e = elaborate(Gamma, Sigma, Delta, c->left, Type);
  check(typecheck_check(Gamma, Sigma, Delta, e, Type), "%W is not well typed.", e, print_term);
  free_term(Type);
  Type = NULL;

  term* er = elaborate(Gamma, Sigma, Delta, c->right, e);
  check(typecheck_check(Gamma, Sigma, Delta, er, e), "Term %W\n failed to have type %W\n",
        er, print_term,
        e, print_term);


  Gamma = telescope_add(variable_dup(c->var), term_dup(e), Gamma);
  Sigma = context_add(variable_dup(c->var), term_dup(er), Sigma);
  printf("%W defined\n", c->var, print_variable);
  return;
 error:
  free_term(Type);
}

static void vernac_run_check(command* c) {
  term* Type = make_type();
  term* er = elaborate(Gamma, Sigma, Delta, c->right, Type);
  term* e = elaborate(Gamma, Sigma, Delta, c->left, er);
  if (er == NULL) {
    term* ty = typecheck(Gamma, Sigma, Delta, e);
    printf("%W : %W\n", e, print_term, ty, print_term);
    free_term(ty);
  } else {
    check(typecheck_check(Gamma, Sigma, Delta, er, Type), "RHS of check is ill-typed");
    check(typecheck_check(Gamma, Sigma, Delta, e, er), "Check failed.");
    printf("check succeeded.\n");
  }
  free_term(Type);

  return;
 error:
  free_term(Type);
}

static int check_datatype(command *c) {
  int res = 0;
  telescope *Gamma_prime = telescope_add(variable_dup(c->var), make_type(), Gamma);
  term *tA = make_datatype_term(variable_dup(c->var));
  context *Sigma_prime = context_add(variable_dup(c->var), tA, Sigma);
      
  term *A = make_var(variable_dup(c->var));
  int num_constructors = c->num_args;
  int i;
  term *type = NULL;
  for (i = 0; i < num_constructors; i++) {
    term *constructor = c->args[i];
    if (constructor->left == NULL) {
      constructor->left = term_dup(A);
    }
    type = make_type();
    check(has_type(Gamma_prime, Sigma_prime, Delta, constructor->left, type),
          "constructor %W has type %W instead of Type", constructor->var,
          print_variable,
          typecheck(Gamma_prime, Sigma_prime, Delta, constructor->left),
          print_term);
    check(is_pi_returning(Sigma_prime, Delta,
                          constructor->left, A), "constructor %W does not return %W",
          constructor->var, print_variable, A, print_term);
    free_term(type);
    type = NULL;
    // todo: positivity
    // todo: allow constructor types to normalize to pi-types
  }
  res = 1;
 error:
  telescope_pop(Gamma_prime);
  Gamma_prime = NULL;

  context_pop(Sigma_prime);
  Sigma_prime = NULL;

  free_term(A);
  A = NULL;

  if (type) {
    free_term(type);
    type = NULL;
  }
  
  return res;
}

static void add_datatype_to_context(command *c) {
  Gamma = telescope_add(variable_dup(c->var), make_type(), Gamma);
  term *tA = make_datatype_term(variable_dup(c->var));
  Sigma = context_add(variable_dup(c->var), tA, Sigma);
}

static void build_constructors(command *c, datatype *T) {
  int i;
  int total_args = 0;
  term *A = make_var(variable_dup(c->var));  
  for (i = 0; i < T->num_intros; i++) {
    term *constructor_type = c->args[i]->left;
    while (constructor_type->tag == PI) {
      total_args++;
      constructor_type = constructor_type->right;
    }
  }
  T->inductive_args = calloc(total_args, sizeof(int));
  int total_arg_index = 0;
  T->intros = malloc(T->num_intros * sizeof(term*));

  for (i = 0; i < c->num_args; i++) {
    term *constructor_type = c->args[i]->left;
    int num_args = 0;
    while (constructor_type->tag == PI) {
      num_args++;
      constructor_type = constructor_type->right;
    }
    term *intro = make_intro(variable_dup(c->args[i]->var), term_dup(A), num_args);
    T->intros[i] = intro;
    term *lambda_wrapped_intro = intro;
    term *prev = NULL;
    int j;
    constructor_type = c->args[i]->left;
    for (j = 0; j < num_args; j++) {
      variable *x = gensym("x");
      term *new_lambda = make_lambda(x, term_dup(constructor_type->left), intro);
      if (prev == NULL) {
        lambda_wrapped_intro = new_lambda;
      }
      else {
        prev->right = new_lambda;
      }
      term *arg = make_var(variable_dup(x));
      if (definitionally_equal(Sigma, Delta, constructor_type->left, A)) {
        T->inductive_args[total_arg_index] = 1;
      }
      total_arg_index++;
      intro->args[j] = arg;
      prev = new_lambda;
      constructor_type = constructor_type->right;
    }
    Sigma = context_add(variable_dup(c->args[i]->var), lambda_wrapped_intro, Sigma);
    Gamma = telescope_add(variable_dup(c->args[i]->var),
                          typecheck(Gamma, Sigma, Delta, lambda_wrapped_intro),
                          Gamma);
  }
  free_term(A);
  A = NULL;
}

static void build_eliminator(command *c, datatype *T) {
  variable **vars = malloc((c->num_args+2) * sizeof(variable*));
  term *A = make_var(variable_dup(c->var));  
  vars[0] = gensym("M");
  vars[c->num_args+1] = gensym("a");
  int i;
  for (i = 0; i < c->num_args; i++) {
    vars[i+1] = gensym("c");
  }
  char *elim_name;
  asprintf(&elim_name, "%W_elim", A, print_term);
  term *wrapped_eliminator;
  T->elim = make_elim(make_variable(strdup(elim_name)), c->num_args + 2);
  for (i = 0; i < c->num_args+2; i++) {
    T->elim->args[i] = make_var(variable_dup(vars[i]));
  }
  wrapped_eliminator = T->elim;
  wrapped_eliminator = make_lambda(variable_dup(vars[c->num_args+1]),
                                   term_dup(A), wrapped_eliminator);
  for (i = c->num_args-1; i >= 0; i--) {
    term *constructor_type = c->args[i]->left;
    term *app = make_app(make_var(variable_dup(vars[0])), make_var(variable_dup(c->args[i]->var)));
    term *prev = NULL;
    term *wrapped = app;
    while (constructor_type->tag == PI) {
      term *new_wrapper;
      variable *x = gensym("x");
      new_wrapper = make_pi(variable_dup(x), term_dup(constructor_type->left), app);
      if (prev == NULL) {
        wrapped = new_wrapper;
      }
      else
      {
        prev->right = new_wrapper;
      }
      // check to see if this is an inductive case
      if (definitionally_equal(Sigma, Delta, constructor_type->left, A)) {
        new_wrapper->right = make_pi(variable_dup(&ignore),
                                     make_app(make_var(variable_dup(vars[0])),
                                              make_var(variable_dup(x))),
                                     app);
        new_wrapper = new_wrapper->right;
      }          
      app->right = make_app(app->right, make_var(variable_dup(x)));
      prev = new_wrapper;
      constructor_type = constructor_type->right;
      free_variable(x);
      x = NULL;
    }
    wrapped_eliminator = make_lambda(variable_dup(vars[i+1]),
                       wrapped,
                       wrapped_eliminator);
  }
  wrapped_eliminator = make_lambda(variable_dup(vars[0]),
                                   make_pi(variable_dup(&ignore), term_dup(A), make_type()),
                     wrapped_eliminator);
  for (i = 0; i < c->num_args+2; i++) {
    free_variable(vars[i]);
    vars[i] = NULL;
  }
  free(vars);
  vars = NULL;
  free_term(A);
  A = NULL;
  Sigma = context_add(make_variable(strdup(elim_name)), wrapped_eliminator, Sigma);
  Gamma = telescope_add(make_variable(strdup(elim_name)),
                        typecheck(Gamma, Sigma, Delta, wrapped_eliminator),
                        Gamma);
  free(elim_name);
  elim_name = NULL;
}

static void vernac_run_data(command *c) {
  if (!check_datatype(c)) {
    return;
  }
  add_datatype_to_context(c);
  datatype *T = make_datatype(variable_dup(c->var), c->num_args);
  
  build_constructors(c, T);
  build_eliminator(c, T);
  Delta = typing_context_add(T, Delta);
  printf("added datatype %W\n", T->name, print_variable);
}

void vernac_run(command *c) {
  switch (c->tag) {
  case DEF:
    vernac_run_def(c);
    break;
  case PRINT:
    printf("%W\n", context_lookup(c->var, Sigma), print_term);
    break;
  case CHECK:
    vernac_run_check(c);
    break;
  case SIMPL:
    {
      term* e = elaborate(Gamma, Sigma, Delta, c->left, NULL);
      term* t = normalize(Sigma, Delta, e);
      printf("%W ==> %W\n", e, print_term, t, print_term);
      free_term(t);
      break;
    }
  case DATA:
    {
      vernac_run_data(c);
      break;
    }
  case AXIOM:
    {
      term* Type = make_type();
      term* e = elaborate(Gamma, Sigma, Delta, c->left, Type);
      term* ty = normalize_and_free(Sigma, Delta, typecheck(Gamma, Sigma, Delta, e));
      check(ty != NULL, "Axiom's type %W does not typecheck", e, print_term);
      check(ty->tag == TYPE, "Axiom's type %W has type %W instead of Type", e, print_term, ty, print_term);
      Gamma = telescope_add(variable_dup(c->var), term_dup(e), Gamma);
      Sigma = context_add(variable_dup(c->var), NULL, Sigma);
      printf("%W added as axiom\n", c->var, print_variable);
      free_term(ty);
      free_term(Type);
      break;
    }
  case IMPORT:
    {
      log_info("importing file %s", c->var->name);
      char* filename;
      asprintf(&filename, "%s/%s%s", wd, c->var->name, ".arvo");
      log_info("path = %s", filename);
      fflush(stdout);
      check(process_file(filename) == 0, "Failed to import file %s.", c->var->name);
      free(filename);
      break;
    }
  }

 error:
  return;
}

static command* make_command() {
  command* ans = malloc(sizeof(command));
  ans->var = NULL;
  ans->left = NULL;
  ans->right = NULL;
  ans->num_args = 0;
  ans->args = NULL;
  return ans;
}

void free_command(command* c) {
  if (c == NULL) return;
  free_variable(c->var);
  c->var = NULL;
  free_term(c->left);
  c->left = NULL;
  free_term(c->right);
  c->right = NULL;
  int i;
  for (i = 0; i < c->num_args; i++) {
    free_term(c->args[i]);
    c->args[i] = NULL;
  }
  free(c->args);
  c->args = NULL;
  free(c);
}


command *make_def(variable *var, term *ty, term *t) {
  command *ans = make_command();
  ans->tag = DEF;
  ans->var = var;
  ans->left = ty;
  ans->right = t;
  return ans;
}
command *make_print(variable *t) {
  command *ans = make_command();
  ans->tag = PRINT;
  ans->var = t;
  return ans;
}

command *make_check(term *t) {
  command *ans = make_command();
  ans->tag = CHECK;
  ans->left = t;
  return ans;
}
command *make_simpl(term *t) {
  command *ans = make_command();
  ans->tag = SIMPL;
  ans->left = t;
  return ans;
}

command *make_data(variable* name, int num_constructors) {
  command* ans = make_command();
  ans->tag = DATA;
  ans->var = name;
  ans->num_args = num_constructors;
  ans->args = malloc(num_constructors * sizeof(term*));
  return ans;
}

command *make_axiom(variable *var, term *ty) {
  command *ans = make_command();
  ans->tag = AXIOM;
  ans->var = var;
  ans->left = ty;
  return ans;
}

command *make_import(variable* name) {
  command *ans = make_command();
  ans->tag = IMPORT;
  ans->var = name;
  return ans;
}

int process_file(char* filename) {
  command *c;

  parsing_context* pc = parse(filename);
  check(pc, "Parse error");

  while((c = next_command(pc))) {
    vernac_run(c);
    free_command(c);
  }

  free_parsing_context(pc);

  return 0;

 error:
  return 1;
}


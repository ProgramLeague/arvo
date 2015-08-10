#include "parser.h"
#include "vernac.h"

#include "mpc/mpc.h"

#include <stdlib.h>

static int prefix(char* s, char* t) {
  check(s != NULL && t != NULL, "prefix requires non-NULL arguments");

  while (*s) {
    if (*t) {
      if (*s == *t) {
        s++;
        t++;
      } else {
        return 0;
      }
    } else {
      return 0;
    }
  }
  return 1;
 error:
  return 0;
}

static mpc_result_t r;
static int command_index;


term* ast_to_term(mpc_ast_t* ast) {
  check(ast, "cannot convert NULL to term");

  if (strstr(ast->tag, "var")) {
    return make_var(make_variable(strdup(ast->contents)));
  } else if (strstr(ast->tag, "type")) {
    return make_type();
  } else if (prefix("lambda", ast->tag)) {
    check(ast->children_num == 6, "malformed lambda node");
    return make_lambda(make_variable(strdup(ast->children[1]->contents)),
                       ast_to_term(ast->children[3]),
                       ast_to_term(ast->children[5]));
  } else if (prefix("pi", ast->tag)) {
    if (ast->children[0]->contents[0] == '(') {
      check(ast->children_num == 7, "malformed pi node");
      return make_pi(make_variable(strdup(ast->children[1]->contents)),
                     ast_to_term(ast->children[3]),
                     ast_to_term(ast->children[6]));
    } else {
      check(ast->children_num == 3, "malformed pi node");
      return make_pi(variable_dup(&ignore),
                     ast_to_term(ast->children[0]),
                     ast_to_term(ast->children[2]));
    }
  } else if (prefix("app", ast->tag)) {
    check(ast->children_num > 0, "malformed app node");
    term *ans = ast_to_term(ast->children[0]);
    int i;
    for (i = 1; i < ast->children_num; i++) {
      ans = make_app(ans, ast_to_term(ast->children[i]));
    }

    return ans;
  } else if (prefix("base", ast->tag)) {
    check(ast->children_num == 3, "malformed base/parens node");
    return ast_to_term(ast->children[1]);
  } else if (prefix("term", ast->tag)) {
    check(ast->children_num == 0, "malformed term node");
    if (strstr(ast->tag, "var")) {
      return make_var(make_variable(strdup(ast->contents)));
    } else if (strstr(ast->tag, "type")) {
      return make_type();
    }
    sentinel("malformed parse tree with tag %s", ast->tag);
  } else if (prefix(">", ast->tag)) {
    check(ast->children_num == 3, "malformed root node");
    return ast_to_term(ast->children[1]);
  } else {
    log_err("Unknown tag %s", ast->tag);
  }

 error:
  return NULL;
}

command *ast_to_command(mpc_ast_t *ast) {
  check(ast, "cannot convert NULL to command");
  if (prefix("def", ast->tag)) {
    check(ast->children_num == 7, "malformed def");

    return make_def(make_variable(strdup(ast->children[1]->contents)),
                    ast_to_term(ast->children[3]),
                    ast_to_term(ast->children[5]));
  }
  else if (prefix("axiom", ast->tag)) {
    check(ast->children_num == 5, "malformed def");

    return make_axiom(make_variable(strdup(ast->children[1]->contents)),
                      ast_to_term(ast->children[3]));
  }
  else if (prefix("print", ast->tag)) {
    check(ast->children_num == 3, "malformed print");

    return make_print(make_variable(strdup(ast->children[1]->contents)));
  }
  else if (prefix("check", ast->tag)) {
    check(ast->children_num == 3, "malformed check");

    return make_check(ast_to_term(ast->children[1]));
  }
  else if (prefix("simpl", ast->tag)) {
    check(ast->children_num == 3, "malformed simpl");

    return make_simpl(ast_to_term(ast->children[1]));
  }
  else if (prefix("data", ast->tag)) {
    int i, j, num_constructors=0;
    command *data;
    for (i=0; i < ast->children_num; i++) {
      if (prefix("constructor", ast->children[i]->tag)) {
        num_constructors++;
      }
    }
    data = make_data(make_variable(strdup(ast->children[1]->contents)), num_constructors);
    j = 0;
    for (i = 0; i < ast->children_num; i++) {
      if (prefix("constructor", ast->children[i]->tag)) {
        if (ast->children[i]->children_num == 0) {
          data->args[j++] = make_var(make_variable(strdup(ast->children[i]->contents)));
        }
        else {
          check(ast->children[i]->children_num == 3, "malformed constructor");
          term *c = make_var(make_variable(strdup(ast->children[i]->children[0]->contents)));
          c->left = ast_to_term(ast->children[i]->children[2]);
          data->args[j++] = c;
        }
      }
    }
    return data;
  }
 error:
  return NULL;
}

static mpc_parser_t* pVar;
static mpc_parser_t* pBound;
static mpc_parser_t* pLambda;
static mpc_parser_t* pPi;
static mpc_parser_t* pApp;
static mpc_parser_t* pBase;
static mpc_parser_t* pType;
static mpc_parser_t* pTerm;
static mpc_parser_t* pCommand;
static mpc_parser_t* pDef;
static mpc_parser_t* pAxiom;
static mpc_parser_t* pPrint;
static mpc_parser_t* pCheck;
static mpc_parser_t* pSimpl;
static mpc_parser_t* pConstructor;
static mpc_parser_t* pData;
static mpc_parser_t* pProgram;

int parse(char* filename) {
  pVar = mpc_new("var");
  pBound = mpc_new("bound");
  pLambda = mpc_new("lambda");
  pPi = mpc_new("pi");
  pApp = mpc_new("app");
  pBase = mpc_new("base");
  pType = mpc_new("type");
  pTerm = mpc_new("term");
  pCommand = mpc_new("command");
  pDef = mpc_new("def");
  pAxiom = mpc_new("axiom");
  pPrint = mpc_new("print");
  pCheck = mpc_new("check");
  pSimpl = mpc_new("simpl");
  pConstructor = mpc_new("constructor");
  pData = mpc_new("data");
  pProgram = mpc_new("program");

  mpc_err_t* err =
    mpca_lang(MPCA_LANG_DEFAULT,
              " var     : /[a-zA-Z][a-zA-Z0-9_]*/ ;                \n"
              " bound   : \"_\" | <var> ;                          \n"
              " lambda  : \"\\\\\" <bound> ':' <term> '.' <term> ; \n"
              " pi      : '(' <bound> ':' <term> ')' \"->\" <term> \n"
              "         |  <app> \"->\" <term> ; \n"
              " base    : <type> | <var> | '(' <term> ')' ; \n"
              " app     : <base> <base>* ;\n"
              " type    : \"Type\" ;\n"
              " term    : <lambda> | <pi> | <app>;\n"
              " def     : \"def\" <var> ':' <term> \":=\" <term> '.' ;\n"
              " axiom     : \"axiom\" <var> ':' <term> '.' ;\n"
              " print   : \"print\" <var> '.' ;\n"
              " check   : \"check\" <term> '.' ;\n"
              " simpl   : \"simpl\" <term> '.' ;\n"
              " constructor : <var> (':' <term>)? ;\n"
              " data    : \"data\" <var> \":=\" <constructor>? ('|' <constructor>)* '.' ;\n"
              " command : <def> | <print> | <check> | <simpl> | <data> | <axiom> ;\n"
              " program  : /^/ <command> * /$/ ;\n",
              pVar, pBound, pLambda, pPi, pBase, pApp, pType,
              pTerm,
              pDef, pAxiom, pPrint, pCheck, pSimpl, pConstructor, pData, pCommand, pProgram, NULL);

  if (err != NULL) {
    mpc_err_print(err);
    mpc_err_delete(err);
    goto error;
  }

  if (!mpc_parse_contents(filename, pProgram, &r)) {
    printf ("error: \n");
    mpc_err_print(r.error);
    mpc_err_delete(r.error);
    goto error;
  }
  //mpc_ast_print(r.output);
  command_index = 1;
  return 1;
 error:
  return 0;
}

command *next_command() {
  mpc_ast_t *ast = r.output;
  if (command_index >= ast->children_num-1) {
    return NULL;
  }
  return ast_to_command(ast->children[command_index++]);
}

void free_ast() {
  mpc_cleanup(16, pVar, pBound, pLambda, pPi, pApp, pBase, pType,
              pTerm, pCommand, pDef, pAxiom, pPrint, pCheck, pSimpl,
              pConstructor, pData, pProgram);
  mpc_ast_delete(r.output);
}

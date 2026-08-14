#include "baf.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    TokenArray *tokens;
    size_t current;
    Diagnostics *diag;
} Parser;

static Token *peek_token(Parser *parser) {
    return &parser->tokens->items[parser->current];
}

static Token *previous_token(Parser *parser) {
    return &parser->tokens->items[parser->current - 1];
}

static Token *lookahead(Parser *parser, size_t offset) {
    size_t index = parser->current + offset;
    if (index >= parser->tokens->count) index = parser->tokens->count - 1;
    return &parser->tokens->items[index];
}

static bool check(Parser *parser, TokenKind kind) {
    return peek_token(parser)->kind == kind;
}

static bool at_end(Parser *parser) {
    return check(parser, TOK_EOF);
}

static Token *advance_token(Parser *parser) {
    if (!at_end(parser)) parser->current++;
    return previous_token(parser);
}

static bool match(Parser *parser, TokenKind kind) {
    if (!check(parser, kind)) return false;
    advance_token(parser);
    return true;
}

static Token *consume(Parser *parser, TokenKind kind, const char *message) {
    if (check(parser, kind)) return advance_token(parser);
    Token *token = peek_token(parser);
    diag_error(parser->diag, token->line, token->column, "%s; found %s",
               message, token_kind_name(token->kind));
    return NULL;
}

static bool is_separator(TokenKind kind) {
    return kind == TOK_NEWLINE || kind == TOK_SEMICOLON;
}

static void skip_separators(Parser *parser) {
    while (is_separator(peek_token(parser)->kind)) advance_token(parser);
}

static void skip_newlines(Parser *parser) {
    while (match(parser, TOK_NEWLINE)) {
    }
}

static Expr *new_expr(ExprKind kind, Token token) {
    Expr *expr = baf_xmalloc(sizeof(*expr));
    memset(expr, 0, sizeof(*expr));
    expr->kind = kind;
    expr->token = token;
    expr->inferred_type = TYPE_UNKNOWN;
    return expr;
}

static Call *new_call(Token token) {
    Call *call = baf_xmalloc(sizeof(*call));
    memset(call, 0, sizeof(*call));
    call->token = token;
    call->return_type = TYPE_UNKNOWN;
    return call;
}

static Stmt *new_stmt(StmtKind kind, Token token) {
    Stmt *stmt = baf_xmalloc(sizeof(*stmt));
    memset(stmt, 0, sizeof(*stmt));
    stmt->kind = kind;
    stmt->token = token;
    return stmt;
}

static Block *new_block(void) {
    Block *block = baf_xmalloc(sizeof(*block));
    *block = (Block){0};
    return block;
}

static void block_push(Block *block, Stmt *stmt) {
    if (block->count == block->capacity) {
        size_t next = block->capacity ? block->capacity * 2 : 8;
        block->items = baf_xrealloc(block->items, next * sizeof(Stmt *));
        block->capacity = next;
    }
    block->items[block->count++] = stmt;
}

static void call_push_arg(Call *call, CallArg arg) {
    if (call->arg_count == call->arg_capacity) {
        size_t next = call->arg_capacity ? call->arg_capacity * 2 : 4;
        call->args = baf_xrealloc(call->args, next * sizeof(CallArg));
        call->arg_capacity = next;
    }
    call->args[call->arg_count++] = arg;
}

static void switch_push_case(Stmt *stmt, SwitchCase switch_case) {
    if (stmt->as.switch_stmt.case_count == stmt->as.switch_stmt.case_capacity) {
        size_t next = stmt->as.switch_stmt.case_capacity
                          ? stmt->as.switch_stmt.case_capacity * 2
                          : 4;
        stmt->as.switch_stmt.cases = baf_xrealloc(
            stmt->as.switch_stmt.cases, next * sizeof(SwitchCase));
        stmt->as.switch_stmt.case_capacity = next;
    }
    stmt->as.switch_stmt.cases[stmt->as.switch_stmt.case_count++] = switch_case;
}

static void function_push_param(Function *function, Parameter parameter) {
    if (function->param_count == function->param_capacity) {
        size_t next = function->param_capacity ? function->param_capacity * 2 : 4;
        function->params = baf_xrealloc(function->params,
                                        next * sizeof(Parameter));
        function->param_capacity = next;
    }
    function->params[function->param_count++] = parameter;
}

static void program_push_function(Program *program, Function *function) {
    if (program->function_count == program->function_capacity) {
        size_t next = program->function_capacity ? program->function_capacity * 2 : 8;
        program->functions = baf_xrealloc(program->functions,
                                          next * sizeof(Function *));
        program->function_capacity = next;
    }
    program->functions[program->function_count++] = function;
}

static bool starts_type(Parser *parser) {
    TokenKind kind = peek_token(parser)->kind;
    return kind == TOK_INT || kind == TOK_STR || kind == TOK_BOOL;
}

static BafType parse_type_name(Parser *parser) {
    Token *token = peek_token(parser);
    if (match(parser, TOK_INT)) return TYPE_INT;
    if (match(parser, TOK_STR)) return TYPE_STRING;
    if (match(parser, TOK_BOOL)) return TYPE_BOOL;
    diag_error(parser->diag, token->line, token->column,
               "expected type name ('int', 'str', or 'bool')");
    return TYPE_UNKNOWN;
}

static char *parse_qualified_name(Parser *parser, Token first) {
    StrBuf name;
    sb_init(&name);
    sb_append(&name, first.text);
    while (match(parser, TOK_DOT)) {
        Token *part = consume(parser, TOK_IDENTIFIER,
                              "expected identifier after '.'");
        if (!part) break;
        sb_append(&name, ".");
        sb_append(&name, part->text);
    }
    if (!name.data) return baf_xstrdup(first.text);
    return name.data;
}

static Expr *parse_expression(Parser *parser);

static Call *parse_call_after_name(Parser *parser, Token name_token,
                                   char *qualified_name) {
    Call *call = new_call(name_token);
    call->callee = qualified_name;
    consume(parser, TOK_LPAREN, "expected '(' after function name");
    skip_newlines(parser);

    bool saw_named = false;
    if (!check(parser, TOK_RPAREN)) {
        do {
            skip_newlines(parser);
            CallArg arg;
            memset(&arg, 0, sizeof(arg));
            arg.resolved_parameter = -1;
            arg.token = *peek_token(parser);

            if (check(parser, TOK_IDENTIFIER) &&
                lookahead(parser, 1)->kind == TOK_COLON) {
                Token *arg_name = advance_token(parser);
                advance_token(parser); /* ':' */
                arg.name = baf_xstrdup(arg_name->text);
                arg.token = *arg_name;
                saw_named = true;
                skip_newlines(parser);
            } else if (saw_named) {
                Token *token = peek_token(parser);
                diag_error(parser->diag, token->line, token->column,
                           "positional arguments cannot follow named arguments");
            }

            arg.value = parse_expression(parser);
            if (arg.value) call_push_arg(call, arg);
            skip_newlines(parser);
        } while (match(parser, TOK_COMMA));
    }

    skip_newlines(parser);
    consume(parser, TOK_RPAREN, "expected ')' after arguments");
    return call;
}

static Expr *parse_primary(Parser *parser) {
    if (match(parser, TOK_INTEGER)) {
        Token token = *previous_token(parser);
        Expr *expr = new_expr(EXPR_INTEGER, token);
        expr->as.integer = token.integer;
        return expr;
    }
    if (match(parser, TOK_STRING)) {
        Token token = *previous_token(parser);
        Expr *expr = new_expr(EXPR_STRING, token);
        expr->as.string = baf_xstrdup(token.text);
        return expr;
    }
    if (match(parser, TOK_TRUE) || match(parser, TOK_FALSE)) {
        Token token = *previous_token(parser);
        Expr *expr = new_expr(EXPR_BOOL, token);
        expr->as.boolean = token.kind == TOK_TRUE;
        return expr;
    }
    if (match(parser, TOK_IDENTIFIER)) {
        Token token = *previous_token(parser);
        char *name = parse_qualified_name(parser, token);
        if (check(parser, TOK_LPAREN)) {
            Expr *expr = new_expr(EXPR_CALL, token);
            expr->as.call = parse_call_after_name(parser, token, name);
            return expr;
        }
        if (strchr(name, '.')) {
            diag_error(parser->diag, token.line, token.column,
                       "qualified name '%s' must be called", name);
        }
        Expr *expr = new_expr(EXPR_NAME, token);
        expr->as.name = name;
        return expr;
    }
    if (match(parser, TOK_LPAREN)) {
        skip_newlines(parser);
        Expr *expr = parse_expression(parser);
        skip_newlines(parser);
        consume(parser, TOK_RPAREN, "expected ')' after expression");
        return expr;
    }

    Token *token = peek_token(parser);
    diag_error(parser->diag, token->line, token->column,
               "expected expression; found %s", token_kind_name(token->kind));
    return NULL;
}

static Expr *parse_unary(Parser *parser) {
    if (match(parser, TOK_MINUS) || match(parser, TOK_BANG)) {
        Token op = *previous_token(parser);
        skip_newlines(parser);
        Expr *operand = parse_unary(parser);
        if (!operand) return NULL;
        if (op.kind == TOK_MINUS && operand->kind == EXPR_INTEGER) {
            operand->as.integer = -operand->as.integer;
            operand->token = op;
            return operand;
        }
        Expr *expr = new_expr(EXPR_UNARY, op);
        expr->as.unary.op = op.kind == TOK_MINUS ? UNOP_NEG : UNOP_NOT;
        expr->as.unary.operand = operand;
        return expr;
    }
    return parse_primary(parser);
}

static bool binary_op_for(TokenKind kind, BinaryOp *op_out, int *precedence_out) {
    switch (kind) {
        case TOK_STAR:          *op_out = BINOP_MUL; *precedence_out = 6; return true;
        case TOK_SLASH:         *op_out = BINOP_DIV; *precedence_out = 6; return true;
        case TOK_PERCENT:       *op_out = BINOP_MOD; *precedence_out = 6; return true;
        case TOK_PLUS:          *op_out = BINOP_ADD; *precedence_out = 5; return true;
        case TOK_MINUS:         *op_out = BINOP_SUB; *precedence_out = 5; return true;
        case TOK_LESS:          *op_out = BINOP_LT;  *precedence_out = 4; return true;
        case TOK_LESS_EQUAL:    *op_out = BINOP_LE;  *precedence_out = 4; return true;
        case TOK_GREATER:       *op_out = BINOP_GT;  *precedence_out = 4; return true;
        case TOK_GREATER_EQUAL: *op_out = BINOP_GE;  *precedence_out = 4; return true;
        case TOK_EQUAL_EQUAL:   *op_out = BINOP_EQ;  *precedence_out = 3; return true;
        case TOK_BANG_EQUAL:    *op_out = BINOP_NE;  *precedence_out = 3; return true;
        case TOK_AND_AND:       *op_out = BINOP_AND; *precedence_out = 2; return true;
        case TOK_OR_OR:         *op_out = BINOP_OR;  *precedence_out = 1; return true;
        default: return false;
    }
}

/* Precedence climbing. Every operator here is left associative. */
static Expr *parse_binary(Parser *parser, int min_precedence) {
    Expr *left = parse_unary(parser);
    if (!left) return NULL;

    for (;;) {
        BinaryOp op;
        int precedence;
        if (!binary_op_for(peek_token(parser)->kind, &op, &precedence)) break;
        if (precedence < min_precedence) break;
        Token op_token = *advance_token(parser);
        skip_newlines(parser);
        Expr *right = parse_binary(parser, precedence + 1);
        if (!right) return left;
        Expr *binary = new_expr(EXPR_BINARY, op_token);
        binary->as.binary.op = op;
        binary->as.binary.left = left;
        binary->as.binary.right = right;
        left = binary;
    }
    return left;
}

static Expr *parse_expression(Parser *parser) {
    return parse_binary(parser, 1);
}

static void consume_statement_end(Parser *parser) {
    if (check(parser, TOK_RBRACE) || check(parser, TOK_EOF)) return;
    if (!is_separator(peek_token(parser)->kind)) {
        Token *token = peek_token(parser);
        diag_error(parser->diag, token->line, token->column,
                   "expected newline or ';' after statement");
        return;
    }
    skip_separators(parser);
}

static Block *parse_block(Parser *parser);

static Stmt *parse_while(Parser *parser, Token while_token) {
    Stmt *stmt = new_stmt(STMT_WHILE, while_token);
    consume(parser, TOK_LPAREN, "expected '(' after 'while'");
    skip_newlines(parser);
    stmt->as.while_stmt.condition = parse_expression(parser);
    skip_newlines(parser);
    consume(parser, TOK_RPAREN, "expected ')' after while condition");
    skip_newlines(parser);
    stmt->as.while_stmt.body = parse_block(parser);
    return stmt;
}

static void if_push_branch(Stmt *stmt, IfBranch branch) {
    if (stmt->as.if_stmt.branch_count == stmt->as.if_stmt.branch_capacity) {
        size_t next = stmt->as.if_stmt.branch_capacity
                          ? stmt->as.if_stmt.branch_capacity * 2
                          : 4;
        stmt->as.if_stmt.branches = baf_xrealloc(stmt->as.if_stmt.branches,
                                                 next * sizeof(IfBranch));
        stmt->as.if_stmt.branch_capacity = next;
    }
    stmt->as.if_stmt.branches[stmt->as.if_stmt.branch_count++] = branch;
}

static Stmt *parse_if(Parser *parser, Token if_token) {
    Stmt *stmt = new_stmt(STMT_IF, if_token);
    IfBranch branch;
    memset(&branch, 0, sizeof(branch));
    branch.token = if_token;
    consume(parser, TOK_LPAREN, "expected '(' after 'if'");
    skip_newlines(parser);
    branch.condition = parse_expression(parser);
    skip_newlines(parser);
    consume(parser, TOK_RPAREN, "expected ')' after the if condition");
    skip_newlines(parser);
    branch.body = parse_block(parser);
    if_push_branch(stmt, branch);

    for (;;) {
        size_t rewind = parser->current;
        skip_separators(parser);

        if (match(parser, TOK_ELSIF)) {
            IfBranch next_branch;
            memset(&next_branch, 0, sizeof(next_branch));
            next_branch.token = *previous_token(parser);
            consume(parser, TOK_LPAREN, "expected '(' after 'elsif'");
            skip_newlines(parser);
            next_branch.condition = parse_expression(parser);
            skip_newlines(parser);
            consume(parser, TOK_RPAREN, "expected ')' after the elsif condition");
            skip_newlines(parser);
            next_branch.body = parse_block(parser);
            if_push_branch(stmt, next_branch);
            continue;
        }

        if (match(parser, TOK_ELSE)) {
            Token else_token = *previous_token(parser);
            skip_newlines(parser);
            /* 'else if (...)' is accepted as a spelling of 'elsif (...)'. */
            if (match(parser, TOK_IF)) {
                IfBranch next_branch;
                memset(&next_branch, 0, sizeof(next_branch));
                next_branch.token = *previous_token(parser);
                consume(parser, TOK_LPAREN, "expected '(' after 'else if'");
                skip_newlines(parser);
                next_branch.condition = parse_expression(parser);
                skip_newlines(parser);
                consume(parser, TOK_RPAREN,
                        "expected ')' after the else if condition");
                skip_newlines(parser);
                next_branch.body = parse_block(parser);
                if_push_branch(stmt, next_branch);
                continue;
            }
            IfBranch else_branch;
            memset(&else_branch, 0, sizeof(else_branch));
            else_branch.token = else_token;
            else_branch.condition = NULL;
            else_branch.body = parse_block(parser);
            if_push_branch(stmt, else_branch);
            break;
        }

        parser->current = rewind;
        break;
    }
    return stmt;
}

static Stmt *parse_simple_statement(Parser *parser, bool require_end);

static Stmt *parse_for(Parser *parser, Token for_token) {
    Stmt *stmt = new_stmt(STMT_FOR, for_token);
    consume(parser, TOK_LPAREN, "expected '(' after 'for'");
    skip_newlines(parser);

    if (!check(parser, TOK_SEMICOLON)) {
        stmt->as.for_stmt.init = parse_simple_statement(parser, false);
    }
    consume(parser, TOK_SEMICOLON, "expected ';' after the for initialiser");
    skip_newlines(parser);

    if (!check(parser, TOK_SEMICOLON)) {
        stmt->as.for_stmt.condition = parse_expression(parser);
    }
    consume(parser, TOK_SEMICOLON, "expected ';' after the for condition");
    skip_newlines(parser);

    if (!check(parser, TOK_RPAREN)) {
        stmt->as.for_stmt.step = parse_simple_statement(parser, false);
    }
    skip_newlines(parser);
    consume(parser, TOK_RPAREN, "expected ')' after the for header");
    skip_newlines(parser);
    stmt->as.for_stmt.body = parse_block(parser);
    return stmt;
}

static Stmt *parse_switch(Parser *parser, Token switch_token) {
    Stmt *stmt = new_stmt(STMT_SWITCH, switch_token);
    consume(parser, TOK_LPAREN, "expected '(' after 'switch'");
    skip_newlines(parser);
    stmt->as.switch_stmt.subject = parse_expression(parser);
    skip_newlines(parser);
    consume(parser, TOK_RPAREN, "expected ')' after switch value");
    skip_newlines(parser);
    consume(parser, TOK_LBRACE, "expected '{' to begin switch");
    skip_separators(parser);

    bool saw_default = false;
    while (!check(parser, TOK_RBRACE) && !at_end(parser)) {
        SwitchCase switch_case;
        memset(&switch_case, 0, sizeof(switch_case));

        if (match(parser, TOK_CASE)) {
            switch_case.token = *previous_token(parser);
            skip_newlines(parser);
            switch_case.value = parse_expression(parser);
        } else if (match(parser, TOK_DEFAULT)) {
            switch_case.token = *previous_token(parser);
            switch_case.is_default = true;
            if (saw_default) {
                diag_error(parser->diag, switch_case.token.line,
                           switch_case.token.column,
                           "switch may contain only one default case");
            }
            saw_default = true;
        } else {
            Token *token = peek_token(parser);
            diag_error(parser->diag, token->line, token->column,
                       "expected 'case', 'default', or '}' in switch");
            advance_token(parser);
            skip_separators(parser);
            continue;
        }

        skip_newlines(parser);
        switch_case.body = parse_block(parser);
        switch_push_case(stmt, switch_case);
        skip_separators(parser);
    }

    consume(parser, TOK_RBRACE, "expected '}' after switch");
    return stmt;
}

static bool compound_op_for(TokenKind kind, BinaryOp *op_out) {
    switch (kind) {
        case TOK_PLUS_EQUAL:    *op_out = BINOP_ADD; return true;
        case TOK_MINUS_EQUAL:   *op_out = BINOP_SUB; return true;
        case TOK_STAR_EQUAL:    *op_out = BINOP_MUL; return true;
        case TOK_SLASH_EQUAL:   *op_out = BINOP_DIV; return true;
        case TOK_PERCENT_EQUAL: *op_out = BINOP_MOD; return true;
        default: return false;
    }
}

/* Declarations, assignments and calls: the statements a for header allows. */
static Stmt *parse_simple_statement(Parser *parser, bool require_end) {
    if (starts_type(parser)) {
        Token type_token = *peek_token(parser);
        BafType type = parse_type_name(parser);
        Token *name = consume(parser, TOK_IDENTIFIER,
                              "expected variable name after type");
        if (!name) return NULL;
        Stmt *stmt = new_stmt(STMT_VAR, type_token);
        stmt->as.var.type = type;
        stmt->as.var.name = baf_xstrdup(name->text);
        if (match(parser, TOK_EQUAL)) {
            skip_newlines(parser);
            stmt->as.var.initializer = parse_expression(parser);
        }
        if (require_end) consume_statement_end(parser);
        return stmt;
    }

    if (match(parser, TOK_IDENTIFIER)) {
        Token name_token = *previous_token(parser);
        char *name = parse_qualified_name(parser, name_token);
        if (check(parser, TOK_LPAREN)) {
            Stmt *stmt = new_stmt(STMT_CALL, name_token);
            stmt->as.call = parse_call_after_name(parser, name_token, name);
            if (require_end) consume_statement_end(parser);
            return stmt;
        }
        BinaryOp compound;
        if (!strchr(name, '.') && match(parser, TOK_EQUAL)) {
            Stmt *stmt = new_stmt(STMT_ASSIGN, name_token);
            stmt->as.assign.name = name;
            skip_newlines(parser);
            stmt->as.assign.value = parse_expression(parser);
            if (require_end) consume_statement_end(parser);
            return stmt;
        }
        if (!strchr(name, '.') &&
            compound_op_for(peek_token(parser)->kind, &compound)) {
            Token op_token = *advance_token(parser);
            skip_newlines(parser);
            Expr *rhs = parse_expression(parser);
            /* 'x += e' is rewritten as 'x = x + (e)'. */
            Expr *lhs = new_expr(EXPR_NAME, name_token);
            lhs->as.name = baf_xstrdup(name);
            Expr *combined = new_expr(EXPR_BINARY, op_token);
            combined->as.binary.op = compound;
            combined->as.binary.left = lhs;
            combined->as.binary.right = rhs;
            Stmt *stmt = new_stmt(STMT_ASSIGN, name_token);
            stmt->as.assign.name = name;
            stmt->as.assign.value = combined;
            if (require_end) consume_statement_end(parser);
            return stmt;
        }
        diag_error(parser->diag, name_token.line, name_token.column,
                   "expected '(' or '=' after '%s'", name);
        free(name);
        return NULL;
    }

    Token *token = peek_token(parser);
    diag_error(parser->diag, token->line, token->column,
               "expected statement; found %s", token_kind_name(token->kind));
    advance_token(parser);
    return NULL;
}

static Stmt *parse_statement(Parser *parser) {
    if (match(parser, TOK_WHILE)) {
        Token token = *previous_token(parser);
        return parse_while(parser, token);
    }

    if (match(parser, TOK_FOR)) {
        Token token = *previous_token(parser);
        return parse_for(parser, token);
    }

    if (match(parser, TOK_IF)) {
        Token token = *previous_token(parser);
        return parse_if(parser, token);
    }

    if (match(parser, TOK_SWITCH)) {
        Token token = *previous_token(parser);
        return parse_switch(parser, token);
    }

    if (match(parser, TOK_RETURN)) {
        Token token = *previous_token(parser);
        Stmt *stmt = new_stmt(STMT_RETURN, token);
        if (!is_separator(peek_token(parser)->kind) &&
            !check(parser, TOK_RBRACE) && !at_end(parser)) {
            stmt->as.return_value = parse_expression(parser);
        }
        consume_statement_end(parser);
        return stmt;
    }

    if (match(parser, TOK_BREAK)) {
        Stmt *stmt = new_stmt(STMT_BREAK, *previous_token(parser));
        consume_statement_end(parser);
        return stmt;
    }

    if (match(parser, TOK_CONTINUE)) {
        Stmt *stmt = new_stmt(STMT_CONTINUE, *previous_token(parser));
        consume_statement_end(parser);
        return stmt;
    }

    return parse_simple_statement(parser, true);
}

static Block *parse_block(Parser *parser) {
    if (!consume(parser, TOK_LBRACE, "expected '{' to begin block")) {
        return new_block();
    }
    Block *block = new_block();
    skip_separators(parser);

    while (!check(parser, TOK_RBRACE) && !at_end(parser)) {
        Stmt *stmt = parse_statement(parser);
        if (stmt) block_push(block, stmt);
        if (parser->diag->errors > 100) break;
        skip_separators(parser);
    }

    consume(parser, TOK_RBRACE, "expected '}' after block");
    return block;
}

static Function *parse_function(Parser *parser, bool is_static) {
    Token *func_token = consume(parser, TOK_FUNC, "expected 'func'");
    consume(parser, TOK_ARROW, "expected '->' after 'func'");
    Token *name = consume(parser, TOK_IDENTIFIER, "expected function name");
    if (!func_token || !name) return NULL;

    Function *function = baf_xmalloc(sizeof(*function));
    memset(function, 0, sizeof(*function));
    function->name = baf_xstrdup(name->text);
    function->token = *name;
    function->is_static = is_static;

    consume(parser, TOK_LPAREN, "expected '(' after function name");
    skip_newlines(parser);
    if (!check(parser, TOK_RPAREN)) {
        do {
            skip_newlines(parser);
            Token *param_name = consume(parser, TOK_IDENTIFIER,
                                        "expected parameter name");
            if (!param_name) break;
            Parameter param = {
                .name = baf_xstrdup(param_name->text),
                .token = *param_name,
                .type = TYPE_UNKNOWN,
                .explicitly_typed = false,
            };
            if (match(parser, TOK_COLON)) {
                skip_newlines(parser);
                param.type = parse_type_name(parser);
                param.explicitly_typed = true;
            }
            function_push_param(function, param);
            skip_newlines(parser);
        } while (match(parser, TOK_COMMA));
    }
    skip_newlines(parser);
    consume(parser, TOK_RPAREN, "expected ')' after parameters");

    function->return_type = TYPE_VOID;
    function->explicit_return_type = false;
    if (match(parser, TOK_COLON)) {
        skip_newlines(parser);
        if (match(parser, TOK_VOID)) {
            function->return_type = TYPE_VOID;
        } else {
            function->return_type = parse_type_name(parser);
        }
        function->explicit_return_type = true;
    } else if (match(parser, TOK_ARROW)) {
        /* 'func -> name(...) -> int' is accepted as an alternative spelling. */
        skip_newlines(parser);
        if (match(parser, TOK_VOID)) {
            function->return_type = TYPE_VOID;
        } else {
            function->return_type = parse_type_name(parser);
        }
        function->explicit_return_type = true;
    }

    skip_newlines(parser);
    function->body = parse_block(parser);
    return function;
}

Program *parse_program(TokenArray *tokens, Diagnostics *diag) {
    Parser parser = {.tokens = tokens, .current = 0, .diag = diag};
    Program *program = baf_xmalloc(sizeof(*program));
    memset(program, 0, sizeof(*program));

    skip_separators(&parser);
    while (!at_end(&parser)) {
        bool is_static = match(&parser, TOK_STATIC);
        if (is_static || check(&parser, TOK_FUNC)) {
            Function *function = parse_function(&parser, is_static);
            if (function) program_push_function(program, function);
        } else if (match(&parser, TOK_BEGIN)) {
            Token *begin_token = previous_token(&parser);
            if (program->begin_block) {
                diag_error(diag, begin_token->line, begin_token->column,
                           "a program may contain only one begin block");
                Block *discard = parse_block(&parser);
                (void)discard;
            } else {
                skip_newlines(&parser);
                program->begin_block = parse_block(&parser);
            }
        } else {
            Token *token = peek_token(&parser);
            diag_error(diag, token->line, token->column,
                       "expected function or begin block; found %s",
                       token_kind_name(token->kind));
            advance_token(&parser);
        }
        skip_separators(&parser);
    }

    if (!program->begin_block) {
        Token *token = peek_token(&parser);
        diag_error(diag, token->line, token->column,
                   "program is missing a begin block");
    }
    return program;
}

static void free_expr(Expr *expr);
static void free_block(Block *block);

static void free_call(Call *call) {
    if (!call) return;
    free(call->callee);
    for (size_t i = 0; i < call->arg_count; i++) {
        free(call->args[i].name);
        free_expr(call->args[i].value);
    }
    free(call->args);
    free(call);
}

static void free_expr(Expr *expr) {
    if (!expr) return;
    switch (expr->kind) {
        case EXPR_STRING: free(expr->as.string); break;
        case EXPR_NAME: free(expr->as.name); break;
        case EXPR_BINARY:
            free_expr(expr->as.binary.left);
            free_expr(expr->as.binary.right);
            break;
        case EXPR_UNARY:
            free_expr(expr->as.unary.operand);
            break;
        case EXPR_CALL: free_call(expr->as.call); break;
        case EXPR_INTEGER:
        case EXPR_BOOL:
            break;
    }
    free(expr);
}

static void free_stmt(Stmt *stmt);

static void free_stmt(Stmt *stmt) {
    if (!stmt) return;
    switch (stmt->kind) {
        case STMT_VAR:
            free(stmt->as.var.name);
            free_expr(stmt->as.var.initializer);
            break;
        case STMT_ASSIGN:
            free(stmt->as.assign.name);
            free_expr(stmt->as.assign.value);
            break;
        case STMT_CALL:
            free_call(stmt->as.call);
            break;
        case STMT_WHILE:
            free_expr(stmt->as.while_stmt.condition);
            free_block(stmt->as.while_stmt.body);
            break;
        case STMT_SWITCH:
            free_expr(stmt->as.switch_stmt.subject);
            for (size_t i = 0; i < stmt->as.switch_stmt.case_count; i++) {
                free_expr(stmt->as.switch_stmt.cases[i].value);
                free_block(stmt->as.switch_stmt.cases[i].body);
            }
            free(stmt->as.switch_stmt.cases);
            break;
        case STMT_IF:
            for (size_t i = 0; i < stmt->as.if_stmt.branch_count; i++) {
                free_expr(stmt->as.if_stmt.branches[i].condition);
                free_block(stmt->as.if_stmt.branches[i].body);
            }
            free(stmt->as.if_stmt.branches);
            break;
        case STMT_FOR:
            free_stmt(stmt->as.for_stmt.init);
            free_expr(stmt->as.for_stmt.condition);
            free_stmt(stmt->as.for_stmt.step);
            free_block(stmt->as.for_stmt.body);
            break;
        case STMT_RETURN:
            free_expr(stmt->as.return_value);
            break;
        case STMT_BREAK:
        case STMT_CONTINUE:
            break;
    }
    free(stmt);
}

static void free_block(Block *block) {
    if (!block) return;
    for (size_t i = 0; i < block->count; i++) free_stmt(block->items[i]);
    free(block->items);
    free(block);
}

void program_free(Program *program) {
    if (!program) return;
    for (size_t i = 0; i < program->function_count; i++) {
        Function *function = program->functions[i];
        free(function->name);
        for (size_t j = 0; j < function->param_count; j++) {
            free(function->params[j].name);
        }
        free(function->params);
        free_block(function->body);
        free(function);
    }
    free(program->functions);
    free_block(program->begin_block);
    free(program);
}

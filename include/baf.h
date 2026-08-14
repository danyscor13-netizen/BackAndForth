#ifndef BAF_H
#define BAF_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* ---------- shared utilities ---------- */

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} StrBuf;

void *baf_xmalloc(size_t size);
void *baf_xrealloc(void *ptr, size_t size);
char *baf_xstrdup(const char *text);
char *baf_xstrndup(const char *text, size_t length);
char *baf_read_file(const char *path, size_t *length_out);
void sb_init(StrBuf *buf);
void sb_append(StrBuf *buf, const char *text);
void sb_append_n(StrBuf *buf, const char *text, size_t n);
void sb_printf(StrBuf *buf, const char *fmt, ...);
void sb_free(StrBuf *buf);

/* ---------- diagnostics ---------- */

typedef struct {
    const char *path;
    int errors;
} Diagnostics;

void diag_error(Diagnostics *diag, int line, int column, const char *fmt, ...);
char *baf_expand_includes(const char *source_path, Diagnostics *diag);

/* ---------- lexer ---------- */

typedef enum {
    TOK_EOF,
    TOK_NEWLINE,
    TOK_IDENTIFIER,
    TOK_INTEGER,
    TOK_STRING,

    TOK_BEGIN,
    TOK_FUNC,
    TOK_STATIC,
    TOK_INT,
    TOK_STR,
    TOK_BOOL,
    TOK_TRUE,
    TOK_FALSE,
    TOK_WHILE,
    TOK_FOR,
    TOK_SWITCH,
    TOK_CASE,
    TOK_DEFAULT,
    TOK_IF,
    TOK_ELSIF,
    TOK_ELSE,
    TOK_RETURN,
    TOK_BREAK,
    TOK_CONTINUE,
    TOK_VOID,

    TOK_LBRACE,
    TOK_RBRACE,
    TOK_LPAREN,
    TOK_RPAREN,
    TOK_COMMA,
    TOK_COLON,
    TOK_SEMICOLON,
    TOK_DOT,
    TOK_EQUAL,
    TOK_PLUS,
    TOK_MINUS,
    TOK_STAR,
    TOK_SLASH,
    TOK_PERCENT,
    TOK_PLUS_EQUAL,
    TOK_MINUS_EQUAL,
    TOK_STAR_EQUAL,
    TOK_SLASH_EQUAL,
    TOK_PERCENT_EQUAL,
    TOK_EQUAL_EQUAL,
    TOK_BANG_EQUAL,
    TOK_LESS,
    TOK_LESS_EQUAL,
    TOK_GREATER,
    TOK_GREATER_EQUAL,
    TOK_AND_AND,
    TOK_OR_OR,
    TOK_BANG,
    TOK_ARROW
} TokenKind;

typedef struct {
    TokenKind kind;
    char *text;
    int64_t integer;
    int line;
    int column;
} Token;

typedef struct {
    Token *items;
    size_t count;
    size_t capacity;
} TokenArray;

bool lex_source(const char *source, TokenArray *out, Diagnostics *diag);
void token_array_free(TokenArray *tokens);
const char *token_kind_name(TokenKind kind);

/* ---------- AST ---------- */

typedef enum {
    TYPE_UNKNOWN,
    TYPE_VOID,
    TYPE_INT,
    TYPE_STRING,
    TYPE_BOOL
} BafType;

const char *baf_type_name(BafType type);

typedef struct Expr Expr;
typedef struct Call Call;
typedef struct Stmt Stmt;
typedef struct Block Block;
typedef struct Function Function;
typedef struct Program Program;

typedef enum {
    BUILTIN_NONE,
    BUILTIN_PUTSC,
    BUILTIN_PUTL,
    BUILTIN_INPT,
    BUILTIN_STR_EQ,
    BUILTIN_CONSOLE_CLEAR,
    BUILTIN_CONSOLE_SET_TEXT_COLOR,
    BUILTIN_CONSOLE_SET_BACKGROUND_COLOR,
    BUILTIN_POWER_SHUTDOWN,
    BUILTIN_POWER_REBOOT,
    BUILTIN_DISK_SCAN,
    BUILTIN_DISK_COUNT,
    BUILTIN_DISK_LIST,
    BUILTIN_DISK_HEX,
    BUILTIN_DISK_SELECT,
    BUILTIN_DISK_FORMAT,
    BUILTIN_DISK_FILES,
    BUILTIN_DISK_WRITE,
    BUILTIN_DISK_READ,
    BUILTIN_DISK_REM,
    BUILTIN_DISK_EXISTS,
    BUILTIN_DISK_SIZE,
    BUILTIN_DISK_INFO,
    BUILTIN_DISK_CREATE_DIR,
    BUILTIN_DISK_GOTO_DIR,
    BUILTIN_DISK_GET_DIR,
    BUILTIN_STR_LENGTH,
    BUILTIN_STR_CONCAT,
    BUILTIN_STR_SUB,
    BUILTIN_STR_FROM_INT,
    BUILTIN_STR_FROM_BOOL,
    BUILTIN_STR_TO_INT,
    BUILTIN_MATH_ABS,
    BUILTIN_MATH_MIN,
    BUILTIN_MATH_MAX
} BuiltinKind;

typedef enum {
    EXPR_INTEGER,
    EXPR_STRING,
    EXPR_BOOL,
    EXPR_NAME,
    EXPR_BINARY,
    EXPR_UNARY,
    EXPR_CALL
} ExprKind;

typedef enum {
    BINOP_ADD,
    BINOP_SUB,
    BINOP_MUL,
    BINOP_DIV,
    BINOP_MOD,
    BINOP_EQ,
    BINOP_NE,
    BINOP_LT,
    BINOP_LE,
    BINOP_GT,
    BINOP_GE,
    BINOP_AND,
    BINOP_OR
} BinaryOp;

typedef enum {
    UNOP_NEG,
    UNOP_NOT
} UnaryOp;

const char *baf_binop_name(BinaryOp op);

typedef struct {
    char *name; /* NULL for positional arguments */
    Expr *value;
    Token token;
    int resolved_parameter;
} CallArg;

struct Call {
    char *callee;
    Token token;
    CallArg *args;
    size_t arg_count;
    size_t arg_capacity;
    Function *resolved_function; /* NULL for built-ins */
    BuiltinKind builtin;
    BafType return_type;
};

struct Expr {
    ExprKind kind;
    Token token;
    BafType inferred_type;
    union {
        int64_t integer;
        char *string;
        bool boolean;
        char *name;
        struct {
            BinaryOp op;
            Expr *left;
            Expr *right;
        } binary;
        struct {
            UnaryOp op;
            Expr *operand;
        } unary;
        Call *call;
    } as;
};

typedef struct {
    Expr *value;
    Token token;
    bool is_default;
    Block *body;
} SwitchCase;

typedef struct IfBranch IfBranch;

typedef enum {
    STMT_VAR,
    STMT_ASSIGN,
    STMT_CALL,
    STMT_WHILE,
    STMT_FOR,
    STMT_SWITCH,
    STMT_IF,
    STMT_RETURN,
    STMT_BREAK,
    STMT_CONTINUE
} StmtKind;

struct IfBranch {
    Expr *condition; /* NULL for the trailing else */
    Block *body;
    Token token;
};

struct Stmt {
    StmtKind kind;
    Token token;
    union {
        struct {
            BafType type;
            char *name;
            Expr *initializer;
        } var;
        struct {
            char *name;
            Expr *value;
        } assign;
        Call *call;
        struct {
            Expr *condition;
            Block *body;
        } while_stmt;
        struct {
            Expr *subject;
            SwitchCase *cases;
            size_t case_count;
            size_t case_capacity;
        } switch_stmt;
        struct {
            IfBranch *branches;
            size_t branch_count;
            size_t branch_capacity;
        } if_stmt;
        struct {
            Stmt *init;      /* may be NULL: var or assignment */
            Expr *condition; /* may be NULL: infinite loop */
            Stmt *step;      /* may be NULL: assignment or call */
            Block *body;
        } for_stmt;
        Expr *return_value; /* may be NULL */
    } as;
};

struct Block {
    Stmt **items;
    size_t count;
    size_t capacity;
};

typedef struct {
    char *name;
    Token token;
    BafType type;
    bool explicitly_typed;
} Parameter;

struct Function {
    char *name;
    Token token;
    bool is_static;
    BafType return_type;
    bool explicit_return_type;
    Parameter *params;
    size_t param_count;
    size_t param_capacity;
    Block *body;
};

struct Program {
    Function **functions;
    size_t function_count;
    size_t function_capacity;
    Block *begin_block;
};

/* ---------- parser ---------- */

Program *parse_program(TokenArray *tokens, Diagnostics *diag);
void program_free(Program *program);

/* ---------- semantic analysis ---------- */

bool analyze_program(Program *program, Diagnostics *diag);

/* ---------- LLVM IR generation ---------- */

typedef enum {
    BAF_TARGET_HOSTED,
    BAF_TARGET_I386_FREESTANDING
} BafTarget;

const char *baf_target_name(BafTarget target);
bool parse_baf_target(const char *name, BafTarget *target_out);

bool emit_llvm_ir(Program *program, const char *source_path,
                  const char *output_path, BafTarget target,
                  Diagnostics *diag);

#endif

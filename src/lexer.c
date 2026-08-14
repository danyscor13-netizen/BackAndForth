#include "baf.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *source;
    size_t current;
    int line;
    int column;
    TokenArray *tokens;
    Diagnostics *diag;
} Lexer;

static void token_push(TokenArray *array, Token token) {
    if (array->count == array->capacity) {
        size_t next = array->capacity ? array->capacity * 2 : 64;
        array->items = baf_xrealloc(array->items, next * sizeof(Token));
        array->capacity = next;
    }
    array->items[array->count++] = token;
}

static char peek(Lexer *lexer) {
    return lexer->source[lexer->current];
}

static char advance_char(Lexer *lexer) {
    char ch = lexer->source[lexer->current++];
    if (ch == '\n') {
        lexer->line++;
        lexer->column = 1;
    } else {
        lexer->column++;
    }
    return ch;
}

static void add_simple(Lexer *lexer, TokenKind kind, int line, int column,
                       const char *text) {
    Token token = {
        .kind = kind,
        .text = baf_xstrdup(text),
        .integer = 0,
        .line = line,
        .column = column,
    };
    token_push(lexer->tokens, token);
}

static TokenKind keyword_kind(const char *text) {
    if (strcmp(text, "begin") == 0) return TOK_BEGIN;
    if (strcmp(text, "func") == 0) return TOK_FUNC;
    if (strcmp(text, "static") == 0) return TOK_STATIC;
    if (strcmp(text, "int") == 0) return TOK_INT;
    if (strcmp(text, "str") == 0 || strcmp(text, "string") == 0) return TOK_STR;
    if (strcmp(text, "bool") == 0) return TOK_BOOL;
    if (strcmp(text, "true") == 0) return TOK_TRUE;
    if (strcmp(text, "false") == 0) return TOK_FALSE;
    if (strcmp(text, "while") == 0) return TOK_WHILE;
    if (strcmp(text, "for") == 0) return TOK_FOR;
    if (strcmp(text, "if") == 0) return TOK_IF;
    if (strcmp(text, "elsif") == 0 || strcmp(text, "elif") == 0) return TOK_ELSIF;
    if (strcmp(text, "else") == 0) return TOK_ELSE;
    if (strcmp(text, "return") == 0) return TOK_RETURN;
    if (strcmp(text, "break") == 0) return TOK_BREAK;
    if (strcmp(text, "continue") == 0) return TOK_CONTINUE;
    if (strcmp(text, "void") == 0) return TOK_VOID;
    if (strcmp(text, "switch") == 0) return TOK_SWITCH;
    if (strcmp(text, "case") == 0) return TOK_CASE;
    if (strcmp(text, "default") == 0) return TOK_DEFAULT;
    return TOK_IDENTIFIER;
}

static void lex_identifier(Lexer *lexer, int line, int column, size_t start) {
    while (isalnum((unsigned char)peek(lexer)) || peek(lexer) == '_') {
        advance_char(lexer);
    }
    size_t length = lexer->current - start;
    char *text = baf_xstrndup(lexer->source + start, length);
    Token token = {
        .kind = keyword_kind(text),
        .text = text,
        .integer = 0,
        .line = line,
        .column = column,
    };
    token_push(lexer->tokens, token);
}

static void lex_number(Lexer *lexer, int line, int column, size_t start) {
    while (isdigit((unsigned char)peek(lexer))) {
        advance_char(lexer);
    }
    size_t length = lexer->current - start;
    char *text = baf_xstrndup(lexer->source + start, length);
    char *end = NULL;
    long long value = strtoll(text, &end, 10);
    if (!end || *end != '\0') {
        diag_error(lexer->diag, line, column, "invalid integer literal '%s'", text);
    }
    Token token = {
        .kind = TOK_INTEGER,
        .text = text,
        .integer = (int64_t)value,
        .line = line,
        .column = column,
    };
    token_push(lexer->tokens, token);
}

static void append_decoded(StrBuf *buf, char ch) {
    sb_append_n(buf, &ch, 1);
}

static void lex_string(Lexer *lexer, int line, int column) {
    StrBuf value;
    sb_init(&value);
    bool terminated = false;

    while (peek(lexer) != '\0') {
        char ch = advance_char(lexer);
        if (ch == '"') {
            terminated = true;
            break;
        }
        if (ch == '\n') {
            diag_error(lexer->diag, line, column,
                       "unterminated string literal before end of line");
            break;
        }
        if (ch == '\\') {
            char escaped = peek(lexer);
            if (escaped == '\0') break;
            advance_char(lexer);
            switch (escaped) {
                case 'n': append_decoded(&value, '\n'); break;
                case 'r': append_decoded(&value, '\r'); break;
                case 't': append_decoded(&value, '\t'); break;
                case '\\': append_decoded(&value, '\\'); break;
                case '"': append_decoded(&value, '"'); break;
                case '0':
                    diag_error(lexer->diag, lexer->line, lexer->column - 1,
                               "NUL escapes are not supported yet");
                    break;
                default:
                    diag_error(lexer->diag, lexer->line, lexer->column - 1,
                               "unknown escape sequence '\\%c'", escaped);
                    append_decoded(&value, escaped);
                    break;
            }
        } else {
            append_decoded(&value, ch);
        }
    }

    if (!terminated && peek(lexer) == '\0') {
        diag_error(lexer->diag, line, column, "unterminated string literal");
    }
    if (!value.data) value.data = baf_xstrdup("");

    Token token = {
        .kind = TOK_STRING,
        .text = value.data,
        .integer = 0,
        .line = line,
        .column = column,
    };
    token_push(lexer->tokens, token);
}

bool lex_source(const char *source, TokenArray *out, Diagnostics *diag) {
    *out = (TokenArray){0};
    Lexer lexer = {
        .source = source,
        .current = 0,
        .line = 1,
        .column = 1,
        .tokens = out,
        .diag = diag,
    };

    while (peek(&lexer) != '\0') {
        int line = lexer.line;
        int column = lexer.column;
        size_t start = lexer.current;
        char ch = advance_char(&lexer);

        switch (ch) {
            case ' ':
            case '\t':
            case '\r':
                break;
            case '\n': add_simple(&lexer, TOK_NEWLINE, line, column, "\\n"); break;
            case '/':
                if (peek(&lexer) == '/') {
                    while (peek(&lexer) != '\n' && peek(&lexer) != '\0') {
                        advance_char(&lexer);
                    }
                } else if (peek(&lexer) == '*') {
                    advance_char(&lexer);
                    bool closed = false;
                    while (peek(&lexer) != '\0') {
                        char c = advance_char(&lexer);
                        if (c == '*' && peek(&lexer) == '/') {
                            advance_char(&lexer);
                            closed = true;
                            break;
                        }
                    }
                    if (!closed) {
                        diag_error(diag, line, column, "unterminated block comment");
                    }
                } else if (peek(&lexer) == '=') {
                    advance_char(&lexer);
                    add_simple(&lexer, TOK_SLASH_EQUAL, line, column, "/=");
                } else {
                    add_simple(&lexer, TOK_SLASH, line, column, "/");
                }
                break;
            case '{': add_simple(&lexer, TOK_LBRACE, line, column, "{"); break;
            case '}': add_simple(&lexer, TOK_RBRACE, line, column, "}"); break;
            case '(': add_simple(&lexer, TOK_LPAREN, line, column, "("); break;
            case ')': add_simple(&lexer, TOK_RPAREN, line, column, ")"); break;
            case ',': add_simple(&lexer, TOK_COMMA, line, column, ","); break;
            case ':': add_simple(&lexer, TOK_COLON, line, column, ":"); break;
            case ';': add_simple(&lexer, TOK_SEMICOLON, line, column, ";"); break;
            case '.': add_simple(&lexer, TOK_DOT, line, column, "."); break;
            case '=':
                if (peek(&lexer) == '=') {
                    advance_char(&lexer);
                    add_simple(&lexer, TOK_EQUAL_EQUAL, line, column, "==");
                } else {
                    add_simple(&lexer, TOK_EQUAL, line, column, "=");
                }
                break;
            case '+':
                if (peek(&lexer) == '=') {
                    advance_char(&lexer);
                    add_simple(&lexer, TOK_PLUS_EQUAL, line, column, "+=");
                } else {
                    add_simple(&lexer, TOK_PLUS, line, column, "+");
                }
                break;
            case '*':
                if (peek(&lexer) == '=') {
                    advance_char(&lexer);
                    add_simple(&lexer, TOK_STAR_EQUAL, line, column, "*=");
                } else {
                    add_simple(&lexer, TOK_STAR, line, column, "*");
                }
                break;
            case '%':
                if (peek(&lexer) == '=') {
                    advance_char(&lexer);
                    add_simple(&lexer, TOK_PERCENT_EQUAL, line, column, "%=");
                } else {
                    add_simple(&lexer, TOK_PERCENT, line, column, "%");
                }
                break;
            case '<':
                if (peek(&lexer) == '=') {
                    advance_char(&lexer);
                    add_simple(&lexer, TOK_LESS_EQUAL, line, column, "<=");
                } else {
                    add_simple(&lexer, TOK_LESS, line, column, "<");
                }
                break;
            case '>':
                if (peek(&lexer) == '=') {
                    advance_char(&lexer);
                    add_simple(&lexer, TOK_GREATER_EQUAL, line, column, ">=");
                } else {
                    add_simple(&lexer, TOK_GREATER, line, column, ">");
                }
                break;
            case '!':
                if (peek(&lexer) == '=') {
                    advance_char(&lexer);
                    add_simple(&lexer, TOK_BANG_EQUAL, line, column, "!=");
                } else {
                    add_simple(&lexer, TOK_BANG, line, column, "!");
                }
                break;
            case '&':
                if (peek(&lexer) == '&') {
                    advance_char(&lexer);
                    add_simple(&lexer, TOK_AND_AND, line, column, "&&");
                } else {
                    diag_error(diag, line, column, "expected '&' after '&'");
                }
                break;
            case '|':
                if (peek(&lexer) == '|') {
                    advance_char(&lexer);
                    add_simple(&lexer, TOK_OR_OR, line, column, "||");
                } else {
                    diag_error(diag, line, column, "expected '|' after '|'");
                }
                break;
            case '-':
                if (peek(&lexer) == '>') {
                    advance_char(&lexer);
                    add_simple(&lexer, TOK_ARROW, line, column, "->");
                } else if (peek(&lexer) == '=') {
                    advance_char(&lexer);
                    add_simple(&lexer, TOK_MINUS_EQUAL, line, column, "-=");
                } else {
                    add_simple(&lexer, TOK_MINUS, line, column, "-");
                }
                break;
            case '"': lex_string(&lexer, line, column); break;
            default:
                if (isalpha((unsigned char)ch) || ch == '_') {
                    lex_identifier(&lexer, line, column, start);
                } else if (isdigit((unsigned char)ch)) {
                    lex_number(&lexer, line, column, start);
                } else {
                    diag_error(diag, line, column, "unexpected character '%c'", ch);
                }
                break;
        }
    }

    add_simple(&lexer, TOK_EOF, lexer.line, lexer.column, "<eof>");
    return diag->errors == 0;
}

void token_array_free(TokenArray *tokens) {
    for (size_t i = 0; i < tokens->count; i++) free(tokens->items[i].text);
    free(tokens->items);
    *tokens = (TokenArray){0};
}

const char *token_kind_name(TokenKind kind) {
    switch (kind) {
        case TOK_EOF: return "end of file";
        case TOK_NEWLINE: return "newline";
        case TOK_IDENTIFIER: return "identifier";
        case TOK_INTEGER: return "integer";
        case TOK_STRING: return "string";
        case TOK_BEGIN: return "begin";
        case TOK_FUNC: return "func";
        case TOK_STATIC: return "static";
        case TOK_INT: return "int";
        case TOK_STR: return "str";
        case TOK_BOOL: return "bool";
        case TOK_TRUE: return "true";
        case TOK_FALSE: return "false";
        case TOK_WHILE: return "while";
        case TOK_FOR: return "for";
        case TOK_IF: return "if";
        case TOK_ELSIF: return "elsif";
        case TOK_ELSE: return "else";
        case TOK_RETURN: return "return";
        case TOK_BREAK: return "break";
        case TOK_CONTINUE: return "continue";
        case TOK_VOID: return "void";
        case TOK_SWITCH: return "switch";
        case TOK_CASE: return "case";
        case TOK_DEFAULT: return "default";
        case TOK_LBRACE: return "{";
        case TOK_RBRACE: return "}";
        case TOK_LPAREN: return "(";
        case TOK_RPAREN: return ")";
        case TOK_COMMA: return ",";
        case TOK_COLON: return ":";
        case TOK_SEMICOLON: return ";";
        case TOK_DOT: return ".";
        case TOK_EQUAL: return "=";
        case TOK_PLUS: return "+";
        case TOK_MINUS: return "-";
        case TOK_STAR: return "*";
        case TOK_SLASH: return "/";
        case TOK_PERCENT: return "%";
        case TOK_PLUS_EQUAL: return "+=";
        case TOK_MINUS_EQUAL: return "-=";
        case TOK_STAR_EQUAL: return "*=";
        case TOK_SLASH_EQUAL: return "/=";
        case TOK_PERCENT_EQUAL: return "%=";
        case TOK_EQUAL_EQUAL: return "==";
        case TOK_BANG_EQUAL: return "!=";
        case TOK_LESS: return "<";
        case TOK_LESS_EQUAL: return "<=";
        case TOK_GREATER: return ">";
        case TOK_GREATER_EQUAL: return ">=";
        case TOK_AND_AND: return "&&";
        case TOK_OR_OR: return "||";
        case TOK_BANG: return "!";
        case TOK_ARROW: return "->";
    }
    return "token";
}

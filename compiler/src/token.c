#include "study/token.h"

#include <stdlib.h>

void study_token_list_init(StudyTokenList *tokens) {
    if (tokens == NULL) {
        return;
    }

    tokens->items = NULL;
    tokens->count = 0U;
    tokens->capacity = 0U;
}

void study_token_list_free(StudyTokenList *tokens) {
    if (tokens == NULL) {
        return;
    }

    free(tokens->items);
    study_token_list_init(tokens);
}

const char *study_token_kind_name(StudyTokenKind kind) {
    switch (kind) {
        case STUDY_TOKEN_EOF:
            return "eof";
        case STUDY_TOKEN_IDENTIFIER:
            return "identifier";
        case STUDY_TOKEN_INTEGER:
            return "integer";
        case STUDY_TOKEN_FN:
            return "fn";
        case STUDY_TOKEN_VAR:
            return "var";
        case STUDY_TOKEN_IF:
            return "if";
        case STUDY_TOKEN_ELSE:
            return "else";
        case STUDY_TOKEN_WHILE:
            return "while";
        case STUDY_TOKEN_RETURN:
            return "return";
        case STUDY_TOKEN_PRINT:
            return "print";
        case STUDY_TOKEN_TRUE:
            return "true";
        case STUDY_TOKEN_FALSE:
            return "false";
        case STUDY_TOKEN_I32:
            return "i32";
        case STUDY_TOKEN_BOOL:
            return "bool";
        case STUDY_TOKEN_LEFT_PAREN:
            return "(";
        case STUDY_TOKEN_RIGHT_PAREN:
            return ")";
        case STUDY_TOKEN_LEFT_BRACE:
            return "{";
        case STUDY_TOKEN_RIGHT_BRACE:
            return "}";
        case STUDY_TOKEN_COMMA:
            return ",";
        case STUDY_TOKEN_COLON:
            return ":";
        case STUDY_TOKEN_SEMICOLON:
            return ";";
        case STUDY_TOKEN_ARROW:
            return "->";
        case STUDY_TOKEN_EQUAL:
            return "=";
        case STUDY_TOKEN_EQUAL_EQUAL:
            return "==";
        case STUDY_TOKEN_BANG:
            return "!";
        case STUDY_TOKEN_BANG_EQUAL:
            return "!=";
        case STUDY_TOKEN_LESS:
            return "<";
        case STUDY_TOKEN_LESS_EQUAL:
            return "<=";
        case STUDY_TOKEN_GREATER:
            return ">";
        case STUDY_TOKEN_GREATER_EQUAL:
            return ">=";
        case STUDY_TOKEN_PLUS:
            return "+";
        case STUDY_TOKEN_MINUS:
            return "-";
        case STUDY_TOKEN_STAR:
            return "*";
    }

    return "unknown";
}

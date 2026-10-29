#ifndef STUDY_TOKEN_H
#define STUDY_TOKEN_H

#include "study/location.h"

#include <stddef.h>
#include <stdint.h>

typedef enum StudyTokenKind {
    STUDY_TOKEN_EOF = 0,
    STUDY_TOKEN_IDENTIFIER,
    STUDY_TOKEN_INTEGER,

    STUDY_TOKEN_FN,
    STUDY_TOKEN_VAR,
    STUDY_TOKEN_IF,
    STUDY_TOKEN_ELSE,
    STUDY_TOKEN_WHILE,
    STUDY_TOKEN_RETURN,
    STUDY_TOKEN_PRINT,
    STUDY_TOKEN_TRUE,
    STUDY_TOKEN_FALSE,
    STUDY_TOKEN_I32,
    STUDY_TOKEN_BOOL,

    STUDY_TOKEN_LEFT_PAREN,
    STUDY_TOKEN_RIGHT_PAREN,
    STUDY_TOKEN_LEFT_BRACE,
    STUDY_TOKEN_RIGHT_BRACE,
    STUDY_TOKEN_COMMA,
    STUDY_TOKEN_COLON,
    STUDY_TOKEN_SEMICOLON,
    STUDY_TOKEN_ARROW,
    STUDY_TOKEN_EQUAL,
    STUDY_TOKEN_EQUAL_EQUAL,
    STUDY_TOKEN_BANG,
    STUDY_TOKEN_BANG_EQUAL,
    STUDY_TOKEN_LESS,
    STUDY_TOKEN_LESS_EQUAL,
    STUDY_TOKEN_GREATER,
    STUDY_TOKEN_GREATER_EQUAL,
    STUDY_TOKEN_PLUS,
    STUDY_TOKEN_MINUS,
    STUDY_TOKEN_STAR
} StudyTokenKind;

typedef struct StudyToken {
    StudyTokenKind kind;
    StudySourceRange range;
    uint32_t integer_value;
} StudyToken;

typedef struct StudyTokenList {
    StudyToken *items;
    size_t count;
    size_t capacity;
} StudyTokenList;

void study_token_list_init(StudyTokenList *tokens);
void study_token_list_free(StudyTokenList *tokens);
const char *study_token_kind_name(StudyTokenKind kind);

#endif

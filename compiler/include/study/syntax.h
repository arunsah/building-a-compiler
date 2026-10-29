#ifndef STUDY_SYNTAX_H
#define STUDY_SYNTAX_H

#include "study/source.h"
#include "study/token.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef size_t StudySyntaxNodeId;

#define STUDY_SYNTAX_NODE_NONE SIZE_MAX

typedef enum StudySyntaxType {
    STUDY_SYNTAX_TYPE_I32 = 0,
    STUDY_SYNTAX_TYPE_BOOL
} StudySyntaxType;

typedef enum StudySyntaxKind {
    STUDY_SYNTAX_PROGRAM = 0,
    STUDY_SYNTAX_FUNCTION,
    STUDY_SYNTAX_PARAMETER,
    STUDY_SYNTAX_BLOCK,
    STUDY_SYNTAX_VAR_STATEMENT,
    STUDY_SYNTAX_ASSIGNMENT_STATEMENT,
    STUDY_SYNTAX_IF_STATEMENT,
    STUDY_SYNTAX_WHILE_STATEMENT,
    STUDY_SYNTAX_RETURN_STATEMENT,
    STUDY_SYNTAX_PRINT_STATEMENT,
    STUDY_SYNTAX_BINARY_EXPRESSION,
    STUDY_SYNTAX_UNARY_EXPRESSION,
    STUDY_SYNTAX_GROUP_EXPRESSION,
    STUDY_SYNTAX_CALL_EXPRESSION,
    STUDY_SYNTAX_NAME_EXPRESSION,
    STUDY_SYNTAX_INTEGER_EXPRESSION,
    STUDY_SYNTAX_BOOLEAN_EXPRESSION
} StudySyntaxKind;

typedef struct StudySyntaxNode {
    StudySyntaxKind kind;
    StudySourceRange range;
    StudySyntaxNodeId first_child;
    StudySyntaxNodeId last_child;
    StudySyntaxNodeId next_sibling;
    union {
        struct {
            size_t name_token;
            StudySyntaxType result_type;
        } function;
        struct {
            size_t name_token;
            StudySyntaxType type;
        } binding;
        struct {
            size_t name_token;
        } named;
        struct {
            StudyTokenKind operator_kind;
        } operation;
        struct {
            uint32_t value;
        } integer;
        struct {
            bool value;
        } boolean;
    } as;
} StudySyntaxNode;

typedef struct StudySyntaxTree {
    StudySyntaxNode *nodes;
    size_t count;
    size_t capacity;
    StudySyntaxNodeId root;
} StudySyntaxTree;

void study_syntax_tree_init(StudySyntaxTree *tree);
void study_syntax_tree_free(StudySyntaxTree *tree);

const char *study_syntax_kind_name(StudySyntaxKind kind);
const char *study_syntax_type_name(StudySyntaxType type);

bool study_syntax_tree_dump(
    FILE *stream,
    const StudySyntaxTree *tree,
    const StudySource *source,
    const StudyTokenList *tokens
);

#endif

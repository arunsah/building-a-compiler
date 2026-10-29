#ifndef STUDY_CHECKED_H
#define STUDY_CHECKED_H

#include "study/source.h"
#include "study/syntax.h"
#include "study/token.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

enum { STUDY_MAX_FUNCTION_PARAMETERS = 6 };

typedef size_t StudyDeclarationId;

#define STUDY_DECLARATION_NONE SIZE_MAX

typedef enum StudyDeclarationKind {
    STUDY_DECLARATION_FUNCTION = 0,
    STUDY_DECLARATION_PARAMETER,
    STUDY_DECLARATION_LOCAL
} StudyDeclarationKind;

typedef struct StudyDeclaration {
    StudyDeclarationKind kind;
    StudySyntaxNodeId syntax_node;
    size_t name_token;
    StudySyntaxType type;
    size_t parameter_count;
    StudySyntaxType parameter_types[STUDY_MAX_FUNCTION_PARAMETERS];
} StudyDeclaration;

typedef struct StudyCheckedNode {
    bool has_type;
    StudySyntaxType type;
    StudyDeclarationId declaration;
    bool definitely_returns;
} StudyCheckedNode;

typedef struct StudyCheckedProgram {
    StudyCheckedNode *nodes;
    size_t node_count;
    StudyDeclaration *declarations;
    size_t declaration_count;
    size_t declaration_capacity;
} StudyCheckedProgram;

void study_checked_program_init(StudyCheckedProgram *program);
void study_checked_program_free(StudyCheckedProgram *program);

const char *study_declaration_kind_name(StudyDeclarationKind kind);

bool study_checked_program_dump(
    FILE *stream,
    const StudyCheckedProgram *program,
    const StudySyntaxTree *tree,
    const StudySource *source,
    const StudyTokenList *tokens
);

#endif

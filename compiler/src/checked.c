#include "study/checked.h"

#include <inttypes.h>
#include <stdlib.h>

void study_checked_program_init(StudyCheckedProgram *program) {
    if (program == NULL) {
        return;
    }

    program->nodes = NULL;
    program->node_count = 0U;
    program->declarations = NULL;
    program->declaration_count = 0U;
    program->declaration_capacity = 0U;
}

void study_checked_program_free(StudyCheckedProgram *program) {
    if (program == NULL) {
        return;
    }

    free(program->nodes);
    free(program->declarations);
    study_checked_program_init(program);
}

const char *study_declaration_kind_name(StudyDeclarationKind kind) {
    switch (kind) {
        case STUDY_DECLARATION_FUNCTION:
            return "function";
        case STUDY_DECLARATION_PARAMETER:
            return "parameter";
        case STUDY_DECLARATION_LOCAL:
            return "local";
    }

    return "unknown";
}

static bool study_checked_dump_indent(FILE *stream, size_t depth) {
    size_t index;

    for (index = 0U; index < depth; index++) {
        if (fputs("  ", stream) == EOF) {
            return false;
        }
    }

    return true;
}

static bool study_checked_dump_spelling(
    FILE *stream,
    const StudySource *source,
    const StudyTokenList *tokens,
    size_t token_index
) {
    const StudyToken *token;
    size_t length;

    if (token_index >= tokens->count) {
        return false;
    }

    token = &tokens->items[token_index];
    if (token->range.start.byte_offset > token->range.end.byte_offset ||
        token->range.end.byte_offset > source->length) {
        return false;
    }

    length = token->range.end.byte_offset - token->range.start.byte_offset;
    return fwrite(
        source->bytes + token->range.start.byte_offset,
        1U,
        length,
        stream
    ) == length;
}

static bool study_checked_dump_syntax_label(
    FILE *stream,
    const StudySyntaxNode *node,
    const StudySource *source,
    const StudyTokenList *tokens
) {
    if (fputs(study_syntax_kind_name(node->kind), stream) == EOF) {
        return false;
    }

    switch (node->kind) {
        case STUDY_SYNTAX_FUNCTION:
            if (fputc(' ', stream) == EOF ||
                !study_checked_dump_spelling(
                    stream,
                    source,
                    tokens,
                    node->as.function.name_token
                ) ||
                fprintf(
                    stream,
                    " -> %s",
                    study_syntax_type_name(node->as.function.result_type)
                ) < 0) {
                return false;
            }
            break;
        case STUDY_SYNTAX_PARAMETER:
        case STUDY_SYNTAX_VAR_STATEMENT:
            if (fputc(' ', stream) == EOF ||
                !study_checked_dump_spelling(
                    stream,
                    source,
                    tokens,
                    node->as.binding.name_token
                ) ||
                fprintf(
                    stream,
                    ": %s",
                    study_syntax_type_name(node->as.binding.type)
                ) < 0) {
                return false;
            }
            break;
        case STUDY_SYNTAX_ASSIGNMENT_STATEMENT:
        case STUDY_SYNTAX_CALL_EXPRESSION:
        case STUDY_SYNTAX_NAME_EXPRESSION:
            if (fputc(' ', stream) == EOF ||
                !study_checked_dump_spelling(
                    stream,
                    source,
                    tokens,
                    node->as.named.name_token
                )) {
                return false;
            }
            break;
        case STUDY_SYNTAX_BINARY_EXPRESSION:
        case STUDY_SYNTAX_UNARY_EXPRESSION:
            if (fprintf(
                    stream,
                    " %s",
                    study_token_kind_name(node->as.operation.operator_kind)
                ) < 0) {
                return false;
            }
            break;
        case STUDY_SYNTAX_INTEGER_EXPRESSION:
            if (fprintf(
                    stream,
                    " %" PRIu32,
                    node->as.integer.value
                ) < 0) {
                return false;
            }
            break;
        case STUDY_SYNTAX_BOOLEAN_EXPRESSION:
            if (fputs(node->as.boolean.value ? " true" : " false", stream) ==
                EOF) {
                return false;
            }
            break;
        case STUDY_SYNTAX_PROGRAM:
        case STUDY_SYNTAX_BLOCK:
        case STUDY_SYNTAX_IF_STATEMENT:
        case STUDY_SYNTAX_WHILE_STATEMENT:
        case STUDY_SYNTAX_RETURN_STATEMENT:
        case STUDY_SYNTAX_PRINT_STATEMENT:
        case STUDY_SYNTAX_GROUP_EXPRESSION:
            break;
    }

    return true;
}

static bool study_checked_dump_annotation(
    FILE *stream,
    const StudyCheckedNode *checked
) {
    bool wrote_field = false;

    if (!checked->has_type && checked->declaration == STUDY_DECLARATION_NONE &&
        !checked->definitely_returns) {
        return true;
    }

    if (fputs("  [", stream) == EOF) {
        return false;
    }

    if (checked->has_type) {
        if (fprintf(stream, "type=%s", study_syntax_type_name(checked->type)) <
            0) {
            return false;
        }
        wrote_field = true;
    }

    if (checked->declaration != STUDY_DECLARATION_NONE) {
        if ((wrote_field && fputs(", ", stream) == EOF) ||
            fprintf(stream, "decl=#%zu", checked->declaration) < 0) {
            return false;
        }
        wrote_field = true;
    }

    if (checked->definitely_returns) {
        if ((wrote_field && fputs(", ", stream) == EOF) ||
            fputs("returns=yes", stream) == EOF) {
            return false;
        }
    }

    return fputc(']', stream) != EOF;
}

static bool study_checked_dump_node(
    FILE *stream,
    const StudyCheckedProgram *program,
    const StudySyntaxTree *tree,
    const StudySource *source,
    const StudyTokenList *tokens,
    StudySyntaxNodeId node_id,
    size_t depth
) {
    const StudySyntaxNode *node;
    StudySyntaxNodeId child;

    if (node_id >= tree->count || node_id >= program->node_count ||
        depth > tree->count) {
        return false;
    }

    node = &tree->nodes[node_id];
    if (!study_checked_dump_indent(stream, depth) ||
        !study_checked_dump_syntax_label(stream, node, source, tokens) ||
        !study_checked_dump_annotation(stream, &program->nodes[node_id]) ||
        fprintf(
            stream,
            "  %zu:%zu..%zu:%zu\n",
            node->range.start.line,
            node->range.start.column,
            node->range.end.line,
            node->range.end.column
        ) < 0) {
        return false;
    }

    child = node->first_child;
    while (child != STUDY_SYNTAX_NODE_NONE) {
        if (child >= tree->count ||
            !study_checked_dump_node(
                stream,
                program,
                tree,
                source,
                tokens,
                child,
                depth + 1U
            )) {
            return false;
        }
        child = tree->nodes[child].next_sibling;
    }

    return true;
}

bool study_checked_program_dump(
    FILE *stream,
    const StudyCheckedProgram *program,
    const StudySyntaxTree *tree,
    const StudySource *source,
    const StudyTokenList *tokens
) {
    if (stream == NULL || program == NULL || tree == NULL || source == NULL ||
        tokens == NULL || program->nodes == NULL ||
        program->node_count != tree->count ||
        tree->root == STUDY_SYNTAX_NODE_NONE || tree->root >= tree->count) {
        return false;
    }

    return study_checked_dump_node(
        stream,
        program,
        tree,
        source,
        tokens,
        tree->root,
        0U
    );
}

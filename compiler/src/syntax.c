#include "study/syntax.h"

#include <inttypes.h>
#include <stdlib.h>

void study_syntax_tree_init(StudySyntaxTree *tree) {
    if (tree == NULL) {
        return;
    }

    tree->nodes = NULL;
    tree->count = 0U;
    tree->capacity = 0U;
    tree->root = STUDY_SYNTAX_NODE_NONE;
}

void study_syntax_tree_free(StudySyntaxTree *tree) {
    if (tree == NULL) {
        return;
    }

    free(tree->nodes);
    study_syntax_tree_init(tree);
}

const char *study_syntax_kind_name(StudySyntaxKind kind) {
    switch (kind) {
        case STUDY_SYNTAX_PROGRAM:
            return "program";
        case STUDY_SYNTAX_FUNCTION:
            return "function";
        case STUDY_SYNTAX_PARAMETER:
            return "parameter";
        case STUDY_SYNTAX_BLOCK:
            return "block";
        case STUDY_SYNTAX_VAR_STATEMENT:
            return "var";
        case STUDY_SYNTAX_ASSIGNMENT_STATEMENT:
            return "assign";
        case STUDY_SYNTAX_IF_STATEMENT:
            return "if";
        case STUDY_SYNTAX_WHILE_STATEMENT:
            return "while";
        case STUDY_SYNTAX_RETURN_STATEMENT:
            return "return";
        case STUDY_SYNTAX_PRINT_STATEMENT:
            return "print";
        case STUDY_SYNTAX_BINARY_EXPRESSION:
            return "binary";
        case STUDY_SYNTAX_UNARY_EXPRESSION:
            return "unary";
        case STUDY_SYNTAX_GROUP_EXPRESSION:
            return "group";
        case STUDY_SYNTAX_CALL_EXPRESSION:
            return "call";
        case STUDY_SYNTAX_NAME_EXPRESSION:
            return "name";
        case STUDY_SYNTAX_INTEGER_EXPRESSION:
            return "integer";
        case STUDY_SYNTAX_BOOLEAN_EXPRESSION:
            return "boolean";
    }

    return "unknown";
}

const char *study_syntax_type_name(StudySyntaxType type) {
    switch (type) {
        case STUDY_SYNTAX_TYPE_I32:
            return "i32";
        case STUDY_SYNTAX_TYPE_BOOL:
            return "bool";
    }

    return "unknown";
}

static bool study_dump_token_spelling(
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

static bool study_dump_indent(FILE *stream, size_t depth) {
    size_t index;

    for (index = 0U; index < depth; index++) {
        if (fputs("  ", stream) == EOF) {
            return false;
        }
    }

    return true;
}

static bool study_dump_node_label(
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
                !study_dump_token_spelling(
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
                !study_dump_token_spelling(
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
                !study_dump_token_spelling(
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

    return fprintf(
        stream,
        "  %zu:%zu..%zu:%zu\n",
        node->range.start.line,
        node->range.start.column,
        node->range.end.line,
        node->range.end.column
    ) >= 0;
}

static bool study_dump_node(
    FILE *stream,
    const StudySyntaxTree *tree,
    const StudySource *source,
    const StudyTokenList *tokens,
    StudySyntaxNodeId node_id,
    size_t depth
) {
    const StudySyntaxNode *node;
    StudySyntaxNodeId child;

    if (node_id >= tree->count || depth > tree->count) {
        return false;
    }

    node = &tree->nodes[node_id];
    if (!study_dump_indent(stream, depth) ||
        !study_dump_node_label(stream, node, source, tokens)) {
        return false;
    }

    child = node->first_child;
    while (child != STUDY_SYNTAX_NODE_NONE) {
        if (child >= tree->count ||
            !study_dump_node(
                stream,
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

bool study_syntax_tree_dump(
    FILE *stream,
    const StudySyntaxTree *tree,
    const StudySource *source,
    const StudyTokenList *tokens
) {
    if (stream == NULL || tree == NULL || source == NULL || tokens == NULL ||
        tree->root == STUDY_SYNTAX_NODE_NONE || tree->root >= tree->count) {
        return false;
    }

    return study_dump_node(
        stream,
        tree,
        source,
        tokens,
        tree->root,
        0U
    );
}

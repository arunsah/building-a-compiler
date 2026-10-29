#include "study/parser.h"

#include <stdint.h>
#include <stdlib.h>

enum {
    STUDY_INITIAL_SYNTAX_CAPACITY = 128,
    STUDY_MAX_SYNTAX_NESTING = 256
};

typedef struct StudyParser {
    const StudySource *source;
    const StudyTokenList *tokens;
    StudySyntaxTree tree;
    size_t current;
    size_t nesting;
    StudyDiagnostic *diagnostic;
} StudyParser;

static bool study_parse_expression(
    StudyParser *parser,
    StudySyntaxNodeId *output
);
static bool study_parse_block(StudyParser *parser, StudySyntaxNodeId *output);

static bool study_syntax_tree_is_empty(const StudySyntaxTree *tree) {
    return tree != NULL && tree->nodes == NULL && tree->count == 0U &&
           tree->capacity == 0U && tree->root == STUDY_SYNTAX_NODE_NONE;
}

static const StudyToken *study_parser_current_token(const StudyParser *parser) {
    return &parser->tokens->items[parser->current];
}

static StudyTokenKind study_parser_current_kind(const StudyParser *parser) {
    return study_parser_current_token(parser)->kind;
}

static bool study_parser_check(
    const StudyParser *parser,
    StudyTokenKind kind
) {
    return study_parser_current_kind(parser) == kind;
}

static size_t study_parser_advance(StudyParser *parser) {
    size_t previous = parser->current;

    if (!study_parser_check(parser, STUDY_TOKEN_EOF)) {
        parser->current += 1U;
    }

    return previous;
}

static void study_parser_error_at(
    StudyParser *parser,
    const StudyToken *token,
    const char *message
) {
    study_diagnostic_set(
        parser->diagnostic,
        STUDY_DIAGNOSTIC_SOURCE,
        parser->source->path,
        "%s",
        message
    );
    study_diagnostic_set_location(
        parser->diagnostic,
        token->range.start.byte_offset,
        token->range.start.line,
        token->range.start.column
    );
}

static void study_parser_expected(
    StudyParser *parser,
    const char *expected
) {
    const StudyToken *token = study_parser_current_token(parser);

    study_diagnostic_set(
        parser->diagnostic,
        STUDY_DIAGNOSTIC_SOURCE,
        parser->source->path,
        "expected %s, found %s",
        expected,
        study_token_kind_name(token->kind)
    );
    study_diagnostic_set_location(
        parser->diagnostic,
        token->range.start.byte_offset,
        token->range.start.line,
        token->range.start.column
    );
}

static bool study_parser_consume(
    StudyParser *parser,
    StudyTokenKind kind,
    const char *expected,
    size_t *token_index
) {
    if (!study_parser_check(parser, kind)) {
        study_parser_expected(parser, expected);
        return false;
    }

    *token_index = study_parser_advance(parser);
    return true;
}

static bool study_parser_enter_nesting(StudyParser *parser) {
    if (parser->nesting >= STUDY_MAX_SYNTAX_NESTING) {
        study_parser_error_at(
            parser,
            study_parser_current_token(parser),
            "syntax nesting exceeds 256 levels"
        );
        return false;
    }

    parser->nesting += 1U;
    return true;
}

static void study_parser_leave_nesting(StudyParser *parser) {
    if (parser->nesting != 0U) {
        parser->nesting -= 1U;
    }
}

static bool study_parser_reserve_node(StudyParser *parser) {
    size_t next_capacity;
    StudySyntaxNode *grown;

    if (parser->tree.count < parser->tree.capacity) {
        return true;
    }

    if (parser->tree.capacity == 0U) {
        next_capacity = STUDY_INITIAL_SYNTAX_CAPACITY;
    } else {
        if (parser->tree.capacity > SIZE_MAX / 2U) {
            study_parser_error_at(
                parser,
                study_parser_current_token(parser),
                "too many syntax nodes to store"
            );
            return false;
        }
        next_capacity = parser->tree.capacity * 2U;
    }

    if (next_capacity > SIZE_MAX / sizeof(*parser->tree.nodes)) {
        study_parser_error_at(
            parser,
            study_parser_current_token(parser),
            "too many syntax nodes to store"
        );
        return false;
    }

    grown = realloc(
        parser->tree.nodes,
        next_capacity * sizeof(*parser->tree.nodes)
    );
    if (grown == NULL) {
        study_parser_error_at(
            parser,
            study_parser_current_token(parser),
            "not enough memory to store the syntax tree"
        );
        return false;
    }

    parser->tree.nodes = grown;
    parser->tree.capacity = next_capacity;
    return true;
}

static bool study_parser_add_node(
    StudyParser *parser,
    StudySyntaxKind kind,
    StudySourceRange range,
    StudySyntaxNodeId *output
) {
    StudySyntaxNode *node;

    if (!study_parser_reserve_node(parser)) {
        return false;
    }

    *output = parser->tree.count;
    node = &parser->tree.nodes[parser->tree.count];
    *node = (StudySyntaxNode){0};
    node->kind = kind;
    node->range = range;
    node->first_child = STUDY_SYNTAX_NODE_NONE;
    node->last_child = STUDY_SYNTAX_NODE_NONE;
    node->next_sibling = STUDY_SYNTAX_NODE_NONE;
    parser->tree.count += 1U;
    return true;
}

static bool study_parser_append_child(
    StudyParser *parser,
    StudySyntaxNodeId parent_id,
    StudySyntaxNodeId child_id
) {
    StudySyntaxNode *parent;

    if (parent_id >= parser->tree.count || child_id >= parser->tree.count) {
        study_diagnostic_set(
            parser->diagnostic,
            STUDY_DIAGNOSTIC_INTERNAL,
            parser->source->path,
            "parser attempted to connect an invalid syntax node"
        );
        return false;
    }

    parent = &parser->tree.nodes[parent_id];
    if (parent->first_child == STUDY_SYNTAX_NODE_NONE) {
        parent->first_child = child_id;
    } else {
        parser->tree.nodes[parent->last_child].next_sibling = child_id;
    }
    parent->last_child = child_id;
    return true;
}

static StudySourceRange study_parser_token_range(
    const StudyParser *parser,
    size_t token_index
) {
    return parser->tokens->items[token_index].range;
}

static StudySourceRange study_parser_range_from(
    StudySourcePosition start,
    StudySourcePosition end
) {
    StudySourceRange range;

    range.start = start;
    range.end = end;
    return range;
}

static bool study_parse_type(
    StudyParser *parser,
    StudySyntaxType *type,
    size_t *token_index
) {
    if (study_parser_check(parser, STUDY_TOKEN_I32)) {
        *token_index = study_parser_advance(parser);
        *type = STUDY_SYNTAX_TYPE_I32;
        return true;
    }

    if (study_parser_check(parser, STUDY_TOKEN_BOOL)) {
        *token_index = study_parser_advance(parser);
        *type = STUDY_SYNTAX_TYPE_BOOL;
        return true;
    }

    study_parser_expected(parser, "type 'i32' or 'bool'");
    return false;
}

static bool study_parse_parameter(
    StudyParser *parser,
    StudySyntaxNodeId *output
) {
    size_t name_token;
    size_t colon_token;
    size_t type_token;
    StudySyntaxType type;
    StudySourceRange range;

    if (!study_parser_consume(
            parser,
            STUDY_TOKEN_IDENTIFIER,
            "parameter name",
            &name_token
        ) ||
        !study_parser_consume(
            parser,
            STUDY_TOKEN_COLON,
            "':' after parameter name",
            &colon_token
        ) ||
        !study_parse_type(parser, &type, &type_token)) {
        return false;
    }

    (void)colon_token;
    range = study_parser_range_from(
        study_parser_token_range(parser, name_token).start,
        study_parser_token_range(parser, type_token).end
    );
    if (!study_parser_add_node(
            parser,
            STUDY_SYNTAX_PARAMETER,
            range,
            output
        )) {
        return false;
    }

    parser->tree.nodes[*output].as.binding.name_token = name_token;
    parser->tree.nodes[*output].as.binding.type = type;
    return true;
}

static bool study_parse_function(
    StudyParser *parser,
    StudySyntaxNodeId *output
) {
    size_t fn_token;
    size_t name_token;
    size_t punctuation_token;
    size_t type_token;
    StudySyntaxType result_type;
    StudySyntaxNodeId function_id;
    StudySyntaxNodeId child;
    StudySourceRange initial_range;

    if (!study_parser_consume(
            parser,
            STUDY_TOKEN_FN,
            "'fn' to begin a function",
            &fn_token
        ) ||
        !study_parser_consume(
            parser,
            STUDY_TOKEN_IDENTIFIER,
            "function name",
            &name_token
        ) ||
        !study_parser_consume(
            parser,
            STUDY_TOKEN_LEFT_PAREN,
            "'(' after function name",
            &punctuation_token
        )) {
        return false;
    }

    initial_range = study_parser_token_range(parser, fn_token);
    if (!study_parser_add_node(
            parser,
            STUDY_SYNTAX_FUNCTION,
            initial_range,
            &function_id
        )) {
        return false;
    }
    parser->tree.nodes[function_id].as.function.name_token = name_token;

    if (!study_parser_check(parser, STUDY_TOKEN_RIGHT_PAREN)) {
        do {
            if (!study_parse_parameter(parser, &child) ||
                !study_parser_append_child(parser, function_id, child)) {
                return false;
            }

            if (!study_parser_check(parser, STUDY_TOKEN_COMMA)) {
                break;
            }
            (void)study_parser_advance(parser);
        } while (true);
    }

    if (!study_parser_consume(
            parser,
            STUDY_TOKEN_RIGHT_PAREN,
            "')' after parameters",
            &punctuation_token
        ) ||
        !study_parser_consume(
            parser,
            STUDY_TOKEN_ARROW,
            "'->' before function result type",
            &punctuation_token
        ) ||
        !study_parse_type(parser, &result_type, &type_token) ||
        !study_parse_block(parser, &child) ||
        !study_parser_append_child(parser, function_id, child)) {
        return false;
    }

    (void)type_token;
    parser->tree.nodes[function_id].as.function.result_type = result_type;
    parser->tree.nodes[function_id].range.end = parser->tree.nodes[child].range.end;
    *output = function_id;
    return true;
}

static bool study_parse_primary(
    StudyParser *parser,
    StudySyntaxNodeId *output
) {
    const StudyToken *token = study_parser_current_token(parser);
    size_t token_index;
    StudySyntaxNodeId node_id;
    StudySyntaxNodeId child;
    StudySourceRange range;

    if (token->kind == STUDY_TOKEN_INTEGER) {
        token_index = study_parser_advance(parser);
        if (!study_parser_add_node(
                parser,
                STUDY_SYNTAX_INTEGER_EXPRESSION,
                token->range,
                output
            )) {
            return false;
        }
        parser->tree.nodes[*output].as.integer.value =
            parser->tokens->items[token_index].integer_value;
        return true;
    }

    if (token->kind == STUDY_TOKEN_TRUE || token->kind == STUDY_TOKEN_FALSE) {
        token_index = study_parser_advance(parser);
        if (!study_parser_add_node(
                parser,
                STUDY_SYNTAX_BOOLEAN_EXPRESSION,
                token->range,
                output
            )) {
            return false;
        }
        parser->tree.nodes[*output].as.boolean.value =
            parser->tokens->items[token_index].kind == STUDY_TOKEN_TRUE;
        return true;
    }

    if (token->kind == STUDY_TOKEN_IDENTIFIER) {
        size_t name_token = study_parser_advance(parser);

        if (!study_parser_check(parser, STUDY_TOKEN_LEFT_PAREN)) {
            if (!study_parser_add_node(
                    parser,
                    STUDY_SYNTAX_NAME_EXPRESSION,
                    token->range,
                    output
                )) {
                return false;
            }
            parser->tree.nodes[*output].as.named.name_token = name_token;
            return true;
        }

        (void)study_parser_advance(parser);
        if (!study_parser_add_node(
                parser,
                STUDY_SYNTAX_CALL_EXPRESSION,
                token->range,
                &node_id
            ) ||
            !study_parser_enter_nesting(parser)) {
            return false;
        }
        parser->tree.nodes[node_id].as.named.name_token = name_token;

        if (!study_parser_check(parser, STUDY_TOKEN_RIGHT_PAREN)) {
            do {
                if (!study_parse_expression(parser, &child) ||
                    !study_parser_append_child(parser, node_id, child)) {
                    return false;
                }

                if (!study_parser_check(parser, STUDY_TOKEN_COMMA)) {
                    break;
                }
                (void)study_parser_advance(parser);
            } while (true);
        }

        if (!study_parser_consume(
                parser,
                STUDY_TOKEN_RIGHT_PAREN,
                "')' after call arguments",
                &token_index
            )) {
            return false;
        }
        study_parser_leave_nesting(parser);
        parser->tree.nodes[node_id].range.end =
            study_parser_token_range(parser, token_index).end;
        *output = node_id;
        return true;
    }

    if (token->kind == STUDY_TOKEN_LEFT_PAREN) {
        size_t left_token = study_parser_advance(parser);
        size_t right_token;

        if (!study_parser_enter_nesting(parser) ||
            !study_parse_expression(parser, &child) ||
            !study_parser_consume(
                parser,
                STUDY_TOKEN_RIGHT_PAREN,
                "')' after grouped expression",
                &right_token
            )) {
            return false;
        }
        study_parser_leave_nesting(parser);

        range = study_parser_range_from(
            study_parser_token_range(parser, left_token).start,
            study_parser_token_range(parser, right_token).end
        );
        if (!study_parser_add_node(
                parser,
                STUDY_SYNTAX_GROUP_EXPRESSION,
                range,
                &node_id
            ) ||
            !study_parser_append_child(parser, node_id, child)) {
            return false;
        }
        *output = node_id;
        return true;
    }

    study_parser_expected(parser, "expression");
    return false;
}

static bool study_parse_unary(
    StudyParser *parser,
    StudySyntaxNodeId *output
) {
    StudyTokenKind kind = study_parser_current_kind(parser);

    if (kind == STUDY_TOKEN_MINUS || kind == STUDY_TOKEN_BANG) {
        size_t operator_token = study_parser_advance(parser);
        StudySyntaxNodeId operand;
        StudySyntaxNodeId node_id;
        StudySourceRange range;

        if (!study_parser_enter_nesting(parser) ||
            !study_parse_unary(parser, &operand)) {
            return false;
        }
        study_parser_leave_nesting(parser);

        range = study_parser_range_from(
            study_parser_token_range(parser, operator_token).start,
            parser->tree.nodes[operand].range.end
        );
        if (!study_parser_add_node(
                parser,
                STUDY_SYNTAX_UNARY_EXPRESSION,
                range,
                &node_id
            ) ||
            !study_parser_append_child(parser, node_id, operand)) {
            return false;
        }
        parser->tree.nodes[node_id].as.operation.operator_kind = kind;
        *output = node_id;
        return true;
    }

    return study_parse_primary(parser, output);
}

static bool study_parser_make_binary(
    StudyParser *parser,
    StudySyntaxNodeId left,
    size_t operator_token,
    StudySyntaxNodeId right,
    StudySyntaxNodeId *output
) {
    StudySourceRange range = study_parser_range_from(
        parser->tree.nodes[left].range.start,
        parser->tree.nodes[right].range.end
    );
    StudySyntaxNodeId node_id;

    if (!study_parser_add_node(
            parser,
            STUDY_SYNTAX_BINARY_EXPRESSION,
            range,
            &node_id
        ) ||
        !study_parser_append_child(parser, node_id, left) ||
        !study_parser_append_child(parser, node_id, right)) {
        return false;
    }

    parser->tree.nodes[node_id].as.operation.operator_kind =
        parser->tokens->items[operator_token].kind;
    *output = node_id;
    return true;
}

static bool study_parse_product(
    StudyParser *parser,
    StudySyntaxNodeId *output
) {
    StudySyntaxNodeId left;

    if (!study_parse_unary(parser, &left)) {
        return false;
    }

    while (study_parser_check(parser, STUDY_TOKEN_STAR)) {
        size_t operator_token = study_parser_advance(parser);
        StudySyntaxNodeId right;

        if (!study_parse_unary(parser, &right) ||
            !study_parser_make_binary(
                parser,
                left,
                operator_token,
                right,
                &left
            )) {
            return false;
        }
    }

    *output = left;
    return true;
}

static bool study_parse_addition(
    StudyParser *parser,
    StudySyntaxNodeId *output
) {
    StudySyntaxNodeId left;

    if (!study_parse_product(parser, &left)) {
        return false;
    }

    while (study_parser_check(parser, STUDY_TOKEN_PLUS) ||
           study_parser_check(parser, STUDY_TOKEN_MINUS)) {
        size_t operator_token = study_parser_advance(parser);
        StudySyntaxNodeId right;

        if (!study_parse_product(parser, &right) ||
            !study_parser_make_binary(
                parser,
                left,
                operator_token,
                right,
                &left
            )) {
            return false;
        }
    }

    *output = left;
    return true;
}

static bool study_parse_comparison(
    StudyParser *parser,
    StudySyntaxNodeId *output
) {
    StudySyntaxNodeId left;

    if (!study_parse_addition(parser, &left)) {
        return false;
    }

    while (study_parser_check(parser, STUDY_TOKEN_LESS) ||
           study_parser_check(parser, STUDY_TOKEN_LESS_EQUAL) ||
           study_parser_check(parser, STUDY_TOKEN_GREATER) ||
           study_parser_check(parser, STUDY_TOKEN_GREATER_EQUAL)) {
        size_t operator_token = study_parser_advance(parser);
        StudySyntaxNodeId right;

        if (!study_parse_addition(parser, &right) ||
            !study_parser_make_binary(
                parser,
                left,
                operator_token,
                right,
                &left
            )) {
            return false;
        }
    }

    *output = left;
    return true;
}

static bool study_parse_equality(
    StudyParser *parser,
    StudySyntaxNodeId *output
) {
    StudySyntaxNodeId left;

    if (!study_parse_comparison(parser, &left)) {
        return false;
    }

    while (study_parser_check(parser, STUDY_TOKEN_EQUAL_EQUAL) ||
           study_parser_check(parser, STUDY_TOKEN_BANG_EQUAL)) {
        size_t operator_token = study_parser_advance(parser);
        StudySyntaxNodeId right;

        if (!study_parse_comparison(parser, &right) ||
            !study_parser_make_binary(
                parser,
                left,
                operator_token,
                right,
                &left
            )) {
            return false;
        }
    }

    *output = left;
    return true;
}

static bool study_parse_expression(
    StudyParser *parser,
    StudySyntaxNodeId *output
) {
    return study_parse_equality(parser, output);
}

static bool study_parse_var_statement(
    StudyParser *parser,
    StudySyntaxNodeId *output
) {
    size_t start_token = study_parser_advance(parser);
    size_t name_token;
    size_t punctuation_token;
    size_t type_token;
    size_t end_token;
    StudySyntaxType type;
    StudySyntaxNodeId value;
    StudySyntaxNodeId node_id;
    StudySourceRange range;

    if (!study_parser_consume(
            parser,
            STUDY_TOKEN_IDENTIFIER,
            "variable name after 'var'",
            &name_token
        ) ||
        !study_parser_consume(
            parser,
            STUDY_TOKEN_COLON,
            "':' after variable name",
            &punctuation_token
        ) ||
        !study_parse_type(parser, &type, &type_token) ||
        !study_parser_consume(
            parser,
            STUDY_TOKEN_EQUAL,
            "'=' before variable initializer",
            &punctuation_token
        ) ||
        !study_parse_expression(parser, &value) ||
        !study_parser_consume(
            parser,
            STUDY_TOKEN_SEMICOLON,
            "';' after variable declaration",
            &end_token
        )) {
        return false;
    }

    (void)punctuation_token;
    (void)type_token;
    range = study_parser_range_from(
        study_parser_token_range(parser, start_token).start,
        study_parser_token_range(parser, end_token).end
    );
    if (!study_parser_add_node(
            parser,
            STUDY_SYNTAX_VAR_STATEMENT,
            range,
            &node_id
        ) ||
        !study_parser_append_child(parser, node_id, value)) {
        return false;
    }
    parser->tree.nodes[node_id].as.binding.name_token = name_token;
    parser->tree.nodes[node_id].as.binding.type = type;
    *output = node_id;
    return true;
}

static bool study_parse_assignment_statement(
    StudyParser *parser,
    StudySyntaxNodeId *output
) {
    size_t name_token = study_parser_advance(parser);
    size_t punctuation_token;
    size_t end_token;
    StudySyntaxNodeId value;
    StudySyntaxNodeId node_id;
    StudySourceRange range;

    if (!study_parser_consume(
            parser,
            STUDY_TOKEN_EQUAL,
            "'=' after assignment name",
            &punctuation_token
        ) ||
        !study_parse_expression(parser, &value) ||
        !study_parser_consume(
            parser,
            STUDY_TOKEN_SEMICOLON,
            "';' after assignment",
            &end_token
        )) {
        return false;
    }

    (void)punctuation_token;
    range = study_parser_range_from(
        study_parser_token_range(parser, name_token).start,
        study_parser_token_range(parser, end_token).end
    );
    if (!study_parser_add_node(
            parser,
            STUDY_SYNTAX_ASSIGNMENT_STATEMENT,
            range,
            &node_id
        ) ||
        !study_parser_append_child(parser, node_id, value)) {
        return false;
    }
    parser->tree.nodes[node_id].as.named.name_token = name_token;
    *output = node_id;
    return true;
}

static bool study_parse_if_statement(
    StudyParser *parser,
    StudySyntaxNodeId *output
) {
    size_t start_token = study_parser_advance(parser);
    StudySyntaxNodeId condition;
    StudySyntaxNodeId then_block;
    StudySyntaxNodeId else_block = STUDY_SYNTAX_NODE_NONE;
    StudySyntaxNodeId node_id;
    StudySourcePosition end;
    StudySourceRange range;

    if (!study_parse_expression(parser, &condition) ||
        !study_parse_block(parser, &then_block)) {
        return false;
    }

    end = parser->tree.nodes[then_block].range.end;
    if (study_parser_check(parser, STUDY_TOKEN_ELSE)) {
        (void)study_parser_advance(parser);
        if (!study_parse_block(parser, &else_block)) {
            return false;
        }
        end = parser->tree.nodes[else_block].range.end;
    }

    range = study_parser_range_from(
        study_parser_token_range(parser, start_token).start,
        end
    );
    if (!study_parser_add_node(
            parser,
            STUDY_SYNTAX_IF_STATEMENT,
            range,
            &node_id
        ) ||
        !study_parser_append_child(parser, node_id, condition) ||
        !study_parser_append_child(parser, node_id, then_block) ||
        (else_block != STUDY_SYNTAX_NODE_NONE &&
         !study_parser_append_child(parser, node_id, else_block))) {
        return false;
    }

    *output = node_id;
    return true;
}

static bool study_parse_while_statement(
    StudyParser *parser,
    StudySyntaxNodeId *output
) {
    size_t start_token = study_parser_advance(parser);
    StudySyntaxNodeId condition;
    StudySyntaxNodeId body;
    StudySyntaxNodeId node_id;
    StudySourceRange range;

    if (!study_parse_expression(parser, &condition) ||
        !study_parse_block(parser, &body)) {
        return false;
    }

    range = study_parser_range_from(
        study_parser_token_range(parser, start_token).start,
        parser->tree.nodes[body].range.end
    );
    if (!study_parser_add_node(
            parser,
            STUDY_SYNTAX_WHILE_STATEMENT,
            range,
            &node_id
        ) ||
        !study_parser_append_child(parser, node_id, condition) ||
        !study_parser_append_child(parser, node_id, body)) {
        return false;
    }

    *output = node_id;
    return true;
}

static bool study_parse_return_statement(
    StudyParser *parser,
    StudySyntaxNodeId *output
) {
    size_t start_token = study_parser_advance(parser);
    size_t end_token;
    StudySyntaxNodeId value;
    StudySyntaxNodeId node_id;
    StudySourceRange range;

    if (!study_parse_expression(parser, &value) ||
        !study_parser_consume(
            parser,
            STUDY_TOKEN_SEMICOLON,
            "';' after return value",
            &end_token
        )) {
        return false;
    }

    range = study_parser_range_from(
        study_parser_token_range(parser, start_token).start,
        study_parser_token_range(parser, end_token).end
    );
    if (!study_parser_add_node(
            parser,
            STUDY_SYNTAX_RETURN_STATEMENT,
            range,
            &node_id
        ) ||
        !study_parser_append_child(parser, node_id, value)) {
        return false;
    }

    *output = node_id;
    return true;
}

static bool study_parse_print_statement(
    StudyParser *parser,
    StudySyntaxNodeId *output
) {
    size_t start_token = study_parser_advance(parser);
    size_t punctuation_token;
    size_t end_token;
    StudySyntaxNodeId value;
    StudySyntaxNodeId node_id;
    StudySourceRange range;

    if (!study_parser_consume(
            parser,
            STUDY_TOKEN_LEFT_PAREN,
            "'(' after 'print'",
            &punctuation_token
        ) ||
        !study_parse_expression(parser, &value) ||
        !study_parser_consume(
            parser,
            STUDY_TOKEN_RIGHT_PAREN,
            "')' after printed expression",
            &punctuation_token
        ) ||
        !study_parser_consume(
            parser,
            STUDY_TOKEN_SEMICOLON,
            "';' after print statement",
            &end_token
        )) {
        return false;
    }

    (void)punctuation_token;
    range = study_parser_range_from(
        study_parser_token_range(parser, start_token).start,
        study_parser_token_range(parser, end_token).end
    );
    if (!study_parser_add_node(
            parser,
            STUDY_SYNTAX_PRINT_STATEMENT,
            range,
            &node_id
        ) ||
        !study_parser_append_child(parser, node_id, value)) {
        return false;
    }

    *output = node_id;
    return true;
}

static bool study_parse_statement(
    StudyParser *parser,
    StudySyntaxNodeId *output
) {
    switch (study_parser_current_kind(parser)) {
        case STUDY_TOKEN_LEFT_BRACE:
            return study_parse_block(parser, output);
        case STUDY_TOKEN_VAR:
            return study_parse_var_statement(parser, output);
        case STUDY_TOKEN_IDENTIFIER:
            return study_parse_assignment_statement(parser, output);
        case STUDY_TOKEN_IF:
            return study_parse_if_statement(parser, output);
        case STUDY_TOKEN_WHILE:
            return study_parse_while_statement(parser, output);
        case STUDY_TOKEN_RETURN:
            return study_parse_return_statement(parser, output);
        case STUDY_TOKEN_PRINT:
            return study_parse_print_statement(parser, output);
        default:
            study_parser_expected(parser, "statement");
            return false;
    }
}

static bool study_parse_block(StudyParser *parser, StudySyntaxNodeId *output) {
    size_t left_token;
    size_t right_token;
    StudySyntaxNodeId block_id;
    StudySyntaxNodeId statement;
    StudySourceRange range;

    if (!study_parser_consume(
            parser,
            STUDY_TOKEN_LEFT_BRACE,
            "'{' to begin a block",
            &left_token
        ) ||
        !study_parser_enter_nesting(parser)) {
        return false;
    }

    range = study_parser_token_range(parser, left_token);
    if (!study_parser_add_node(
            parser,
            STUDY_SYNTAX_BLOCK,
            range,
            &block_id
        )) {
        return false;
    }

    while (!study_parser_check(parser, STUDY_TOKEN_RIGHT_BRACE) &&
           !study_parser_check(parser, STUDY_TOKEN_EOF)) {
        if (!study_parse_statement(parser, &statement) ||
            !study_parser_append_child(parser, block_id, statement)) {
            return false;
        }
    }

    if (!study_parser_consume(
            parser,
            STUDY_TOKEN_RIGHT_BRACE,
            "'}' to end the block",
            &right_token
        )) {
        return false;
    }

    study_parser_leave_nesting(parser);
    parser->tree.nodes[block_id].range.end =
        study_parser_token_range(parser, right_token).end;
    *output = block_id;
    return true;
}

static bool study_parse_program(StudyParser *parser) {
    StudySyntaxNodeId program_id;
    StudySyntaxNodeId function_id;
    StudySourceRange range = study_parser_current_token(parser)->range;

    if (!study_parser_add_node(
            parser,
            STUDY_SYNTAX_PROGRAM,
            range,
            &program_id
        )) {
        return false;
    }
    parser->tree.root = program_id;

    while (!study_parser_check(parser, STUDY_TOKEN_EOF)) {
        if (!study_parse_function(parser, &function_id) ||
            !study_parser_append_child(parser, program_id, function_id)) {
            return false;
        }
    }

    parser->tree.nodes[program_id].range.end =
        study_parser_current_token(parser)->range.end;
    return true;
}

bool study_parse(
    const StudySource *source,
    const StudyTokenList *tokens,
    StudySyntaxTree *output,
    StudyDiagnostic *diagnostic
) {
    StudyParser parser;

    study_diagnostic_clear(diagnostic);

    if (output == NULL || !study_syntax_tree_is_empty(output)) {
        study_diagnostic_set(
            diagnostic,
            STUDY_DIAGNOSTIC_INTERNAL,
            source != NULL ? source->path : NULL,
            "syntax tree output must be initialized and empty"
        );
        return false;
    }

    if (source == NULL || source->path == NULL || source->bytes == NULL ||
        tokens == NULL || tokens->items == NULL || tokens->count == 0U ||
        tokens->items[tokens->count - 1U].kind != STUDY_TOKEN_EOF) {
        study_diagnostic_set(
            diagnostic,
            STUDY_DIAGNOSTIC_INTERNAL,
            source != NULL ? source->path : NULL,
            "parser input is invalid"
        );
        return false;
    }

    parser.source = source;
    parser.tokens = tokens;
    study_syntax_tree_init(&parser.tree);
    parser.current = 0U;
    parser.nesting = 0U;
    parser.diagnostic = diagnostic;

    if (!study_parse_program(&parser)) {
        study_syntax_tree_free(&parser.tree);
        return false;
    }

    *output = parser.tree;
    return true;
}

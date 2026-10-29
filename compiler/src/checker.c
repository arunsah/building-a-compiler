#include "study/checker.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum {
    STUDY_INITIAL_DECLARATION_CAPACITY = 32,
    STUDY_INITIAL_BINDING_CAPACITY = 32,
    STUDY_DIAGNOSTIC_NAME_LIMIT = 80
};

typedef struct StudyScopeBinding {
    size_t name_token;
    StudyDeclarationId declaration;
} StudyScopeBinding;

typedef struct StudyChecker {
    const StudySource *source;
    const StudyTokenList *tokens;
    const StudySyntaxTree *tree;
    StudyCheckedProgram program;
    StudyScopeBinding *bindings;
    size_t binding_count;
    size_t binding_capacity;
    size_t current_scope_start;
    StudyDeclarationId current_function;
    StudyDiagnostic *diagnostic;
} StudyChecker;

static bool study_checker_check_expression(
    StudyChecker *checker,
    StudySyntaxNodeId node_id,
    StudySyntaxType *type
);

static bool study_checked_program_is_empty(const StudyCheckedProgram *program) {
    return program != NULL && program->nodes == NULL &&
           program->node_count == 0U && program->declarations == NULL &&
           program->declaration_count == 0U &&
           program->declaration_capacity == 0U;
}

static const StudySyntaxNode *study_checker_node(
    const StudyChecker *checker,
    StudySyntaxNodeId node_id
) {
    return &checker->tree->nodes[node_id];
}

static StudyCheckedNode *study_checker_checked_node(
    StudyChecker *checker,
    StudySyntaxNodeId node_id
) {
    return &checker->program.nodes[node_id];
}

static void study_checker_set_location(
    StudyChecker *checker,
    StudySourcePosition position
) {
    study_diagnostic_set_location(
        checker->diagnostic,
        position.byte_offset,
        position.line,
        position.column
    );
}

static void study_checker_error_at_node(
    StudyChecker *checker,
    StudySyntaxNodeId node_id,
    const char *message
) {
    const StudySyntaxNode *node = study_checker_node(checker, node_id);

    study_diagnostic_set(
        checker->diagnostic,
        STUDY_DIAGNOSTIC_SOURCE,
        checker->source->path,
        "%s",
        message
    );
    study_checker_set_location(checker, node->range.start);
}

static size_t study_checker_token_length(
    const StudyChecker *checker,
    size_t token_index
) {
    const StudyToken *token = &checker->tokens->items[token_index];

    return token->range.end.byte_offset - token->range.start.byte_offset;
}

static const unsigned char *study_checker_token_bytes(
    const StudyChecker *checker,
    size_t token_index
) {
    return checker->source->bytes +
           checker->tokens->items[token_index].range.start.byte_offset;
}

static int study_checker_name_precision(
    const StudyChecker *checker,
    size_t token_index
) {
    size_t length = study_checker_token_length(checker, token_index);

    if (length > (size_t)STUDY_DIAGNOSTIC_NAME_LIMIT) {
        length = (size_t)STUDY_DIAGNOSTIC_NAME_LIMIT;
    }
    if (length > (size_t)INT_MAX) {
        length = (size_t)INT_MAX;
    }

    return (int)length;
}

static void study_checker_error_name(
    StudyChecker *checker,
    size_t name_token,
    const char *description
) {
    const StudyToken *token = &checker->tokens->items[name_token];

    study_diagnostic_set(
        checker->diagnostic,
        STUDY_DIAGNOSTIC_SOURCE,
        checker->source->path,
        "%s '%.*s'",
        description,
        study_checker_name_precision(checker, name_token),
        (const char *)study_checker_token_bytes(checker, name_token)
    );
    study_checker_set_location(checker, token->range.start);
}

static void study_checker_error_type(
    StudyChecker *checker,
    StudySyntaxNodeId node_id,
    const char *context,
    StudySyntaxType expected,
    StudySyntaxType actual
) {
    const StudySyntaxNode *node = study_checker_node(checker, node_id);

    study_diagnostic_set(
        checker->diagnostic,
        STUDY_DIAGNOSTIC_SOURCE,
        checker->source->path,
        "%s requires %s, found %s",
        context,
        study_syntax_type_name(expected),
        study_syntax_type_name(actual)
    );
    study_checker_set_location(checker, node->range.start);
}

static bool study_checker_names_equal(
    const StudyChecker *checker,
    size_t left_token,
    size_t right_token
) {
    size_t left_length = study_checker_token_length(checker, left_token);
    size_t right_length = study_checker_token_length(checker, right_token);

    return left_length == right_length &&
           memcmp(
               study_checker_token_bytes(checker, left_token),
               study_checker_token_bytes(checker, right_token),
               left_length
           ) == 0;
}

static bool study_checker_name_is(
    const StudyChecker *checker,
    size_t token_index,
    const char *text
) {
    size_t length = study_checker_token_length(checker, token_index);
    size_t text_length = strlen(text);

    return length == text_length &&
           memcmp(
               study_checker_token_bytes(checker, token_index),
               text,
               length
           ) == 0;
}

static bool study_checker_reserve_declaration(StudyChecker *checker) {
    size_t next_capacity;
    StudyDeclaration *grown;

    if (checker->program.declaration_count <
        checker->program.declaration_capacity) {
        return true;
    }

    if (checker->program.declaration_capacity == 0U) {
        next_capacity = STUDY_INITIAL_DECLARATION_CAPACITY;
    } else {
        if (checker->program.declaration_capacity > SIZE_MAX / 2U) {
            study_checker_error_at_node(
                checker,
                checker->tree->root,
                "too many declarations to store"
            );
            return false;
        }
        next_capacity = checker->program.declaration_capacity * 2U;
    }

    if (next_capacity > SIZE_MAX / sizeof(*checker->program.declarations)) {
        study_checker_error_at_node(
            checker,
            checker->tree->root,
            "too many declarations to store"
        );
        return false;
    }

    grown = realloc(
        checker->program.declarations,
        next_capacity * sizeof(*checker->program.declarations)
    );
    if (grown == NULL) {
        study_checker_error_at_node(
            checker,
            checker->tree->root,
            "not enough memory to store declarations"
        );
        return false;
    }

    checker->program.declarations = grown;
    checker->program.declaration_capacity = next_capacity;
    return true;
}

static bool study_checker_add_declaration(
    StudyChecker *checker,
    const StudyDeclaration *declaration,
    StudyDeclarationId *output
) {
    if (!study_checker_reserve_declaration(checker)) {
        return false;
    }

    *output = checker->program.declaration_count;
    checker->program.declarations[checker->program.declaration_count] =
        *declaration;
    checker->program.declaration_count += 1U;
    return true;
}

static StudyDeclarationId study_checker_find_function(
    const StudyChecker *checker,
    size_t name_token
) {
    StudyDeclarationId declaration;

    for (declaration = 0U;
         declaration < checker->program.declaration_count;
         declaration++) {
        const StudyDeclaration *candidate =
            &checker->program.declarations[declaration];

        if (candidate->kind == STUDY_DECLARATION_FUNCTION &&
            study_checker_names_equal(
                checker,
                candidate->name_token,
                name_token
            )) {
            return declaration;
        }
    }

    return STUDY_DECLARATION_NONE;
}

static bool study_checker_reserve_binding(StudyChecker *checker) {
    size_t next_capacity;
    StudyScopeBinding *grown;

    if (checker->binding_count < checker->binding_capacity) {
        return true;
    }

    if (checker->binding_capacity == 0U) {
        next_capacity = STUDY_INITIAL_BINDING_CAPACITY;
    } else {
        if (checker->binding_capacity > SIZE_MAX / 2U) {
            study_checker_error_at_node(
                checker,
                checker->tree->root,
                "too many names in scope"
            );
            return false;
        }
        next_capacity = checker->binding_capacity * 2U;
    }

    if (next_capacity > SIZE_MAX / sizeof(*checker->bindings)) {
        study_checker_error_at_node(
            checker,
            checker->tree->root,
            "too many names in scope"
        );
        return false;
    }

    grown = realloc(
        checker->bindings,
        next_capacity * sizeof(*checker->bindings)
    );
    if (grown == NULL) {
        study_checker_error_at_node(
            checker,
            checker->tree->root,
            "not enough memory to store names in scope"
        );
        return false;
    }

    checker->bindings = grown;
    checker->binding_capacity = next_capacity;
    return true;
}

static StudyDeclarationId study_checker_find_current_scope(
    const StudyChecker *checker,
    size_t name_token
) {
    size_t index;

    for (index = checker->binding_count;
         index > checker->current_scope_start;
         index--) {
        const StudyScopeBinding *binding = &checker->bindings[index - 1U];

        if (study_checker_names_equal(
                checker,
                binding->name_token,
                name_token
            )) {
            return binding->declaration;
        }
    }

    return STUDY_DECLARATION_NONE;
}

static StudyDeclarationId study_checker_resolve_local(
    const StudyChecker *checker,
    size_t name_token
) {
    size_t index;

    for (index = checker->binding_count; index > 0U; index--) {
        const StudyScopeBinding *binding = &checker->bindings[index - 1U];

        if (study_checker_names_equal(
                checker,
                binding->name_token,
                name_token
            )) {
            return binding->declaration;
        }
    }

    return STUDY_DECLARATION_NONE;
}

static bool study_checker_bind(
    StudyChecker *checker,
    size_t name_token,
    StudyDeclarationId declaration
) {
    if (!study_checker_reserve_binding(checker)) {
        return false;
    }

    checker->bindings[checker->binding_count].name_token = name_token;
    checker->bindings[checker->binding_count].declaration = declaration;
    checker->binding_count += 1U;
    return true;
}

static bool study_checker_collect_functions(StudyChecker *checker) {
    StudySyntaxNodeId function_id =
        study_checker_node(checker, checker->tree->root)->first_child;
    StudyDeclarationId main_declaration = STUDY_DECLARATION_NONE;

    while (function_id != STUDY_SYNTAX_NODE_NONE) {
        const StudySyntaxNode *function = study_checker_node(checker, function_id);
        StudySyntaxNodeId child;
        StudyDeclaration declaration = {0};
        StudyDeclarationId declaration_id;

        if (function->kind != STUDY_SYNTAX_FUNCTION) {
            study_checker_error_at_node(
                checker,
                function_id,
                "program contains a non-function declaration"
            );
            return false;
        }

        if (study_checker_find_function(
                checker,
                function->as.function.name_token
            ) != STUDY_DECLARATION_NONE) {
            study_checker_error_name(
                checker,
                function->as.function.name_token,
                "duplicate function"
            );
            return false;
        }

        declaration.kind = STUDY_DECLARATION_FUNCTION;
        declaration.syntax_node = function_id;
        declaration.name_token = function->as.function.name_token;
        declaration.type = function->as.function.result_type;
        child = function->first_child;
        while (child != STUDY_SYNTAX_NODE_NONE &&
               study_checker_node(checker, child)->kind ==
                   STUDY_SYNTAX_PARAMETER) {
            const StudySyntaxNode *parameter = study_checker_node(checker, child);

            if (declaration.parameter_count >=
                (size_t)STUDY_MAX_FUNCTION_PARAMETERS) {
                const StudyToken *name = &checker->tokens->items[
                    function->as.function.name_token
                ];

                study_diagnostic_set(
                    checker->diagnostic,
                    STUDY_DIAGNOSTIC_SOURCE,
                    checker->source->path,
                    "function '%.*s' has more than six parameters",
                    study_checker_name_precision(
                        checker,
                        function->as.function.name_token
                    ),
                    (const char *)study_checker_token_bytes(
                        checker,
                        function->as.function.name_token
                    )
                );
                study_checker_set_location(checker, name->range.start);
                return false;
            }

            declaration.parameter_types[declaration.parameter_count] =
                parameter->as.binding.type;
            declaration.parameter_count += 1U;
            child = parameter->next_sibling;
        }

        if (!study_checker_add_declaration(
                checker,
                &declaration,
                &declaration_id
            )) {
            return false;
        }
        study_checker_checked_node(checker, function_id)->declaration =
            declaration_id;

        if (study_checker_name_is(
                checker,
                function->as.function.name_token,
                "main"
            )) {
            main_declaration = declaration_id;
        }

        function_id = function->next_sibling;
    }

    if (main_declaration == STUDY_DECLARATION_NONE) {
        study_checker_error_at_node(
            checker,
            checker->tree->root,
            "program must declare fn main() -> i32"
        );
        return false;
    }

    if (checker->program.declarations[main_declaration].parameter_count != 0U ||
        checker->program.declarations[main_declaration].type !=
            STUDY_SYNTAX_TYPE_I32) {
        const StudyToken *name = &checker->tokens->items[
            checker->program.declarations[main_declaration].name_token
        ];

        study_diagnostic_set(
            checker->diagnostic,
            STUDY_DIAGNOSTIC_SOURCE,
            checker->source->path,
            "main must have signature fn main() -> i32"
        );
        study_checker_set_location(checker, name->range.start);
        return false;
    }

    return true;
}

static bool study_checker_add_local_declaration(
    StudyChecker *checker,
    StudySyntaxNodeId node_id,
    StudyDeclarationKind kind,
    size_t name_token,
    StudySyntaxType type
) {
    StudyDeclaration declaration = {0};
    StudyDeclarationId declaration_id;

    if (study_checker_find_current_scope(checker, name_token) !=
        STUDY_DECLARATION_NONE) {
        study_checker_error_name(
            checker,
            name_token,
            "duplicate declaration of"
        );
        return false;
    }

    declaration.kind = kind;
    declaration.syntax_node = node_id;
    declaration.name_token = name_token;
    declaration.type = type;
    if (!study_checker_add_declaration(
            checker,
            &declaration,
            &declaration_id
        ) ||
        !study_checker_bind(checker, name_token, declaration_id)) {
        return false;
    }

    study_checker_checked_node(checker, node_id)->declaration = declaration_id;
    return true;
}

static bool study_checker_require_type(
    StudyChecker *checker,
    StudySyntaxNodeId node_id,
    StudySyntaxType actual,
    StudySyntaxType expected,
    const char *context
) {
    if (actual == expected) {
        return true;
    }

    study_checker_error_type(checker, node_id, context, expected, actual);
    return false;
}

static bool study_checker_check_call(
    StudyChecker *checker,
    StudySyntaxNodeId node_id,
    StudySyntaxType *type
) {
    const StudySyntaxNode *node = study_checker_node(checker, node_id);
    StudyDeclarationId declaration_id = study_checker_find_function(
        checker,
        node->as.named.name_token
    );
    const StudyDeclaration *declaration;
    StudySyntaxNodeId argument;
    size_t argument_count = 0U;
    size_t index = 0U;

    if (declaration_id == STUDY_DECLARATION_NONE) {
        study_checker_error_name(
            checker,
            node->as.named.name_token,
            "unknown function"
        );
        return false;
    }

    declaration = &checker->program.declarations[declaration_id];
    argument = node->first_child;
    while (argument != STUDY_SYNTAX_NODE_NONE) {
        argument_count += 1U;
        argument = study_checker_node(checker, argument)->next_sibling;
    }

    if (argument_count != declaration->parameter_count) {
        const StudyToken *name =
            &checker->tokens->items[node->as.named.name_token];

        study_diagnostic_set(
            checker->diagnostic,
            STUDY_DIAGNOSTIC_SOURCE,
            checker->source->path,
            "function '%.*s' expects %zu argument%s, found %zu",
            study_checker_name_precision(checker, node->as.named.name_token),
            (const char *)study_checker_token_bytes(
                checker,
                node->as.named.name_token
            ),
            declaration->parameter_count,
            declaration->parameter_count == 1U ? "" : "s",
            argument_count
        );
        study_checker_set_location(checker, name->range.start);
        return false;
    }

    argument = node->first_child;
    while (argument != STUDY_SYNTAX_NODE_NONE) {
        StudySyntaxType argument_type;

        if (!study_checker_check_expression(checker, argument, &argument_type) ||
            !study_checker_require_type(
                checker,
                argument,
                argument_type,
                declaration->parameter_types[index],
                "function argument"
            )) {
            return false;
        }
        index += 1U;
        argument = study_checker_node(checker, argument)->next_sibling;
    }

    study_checker_checked_node(checker, node_id)->declaration = declaration_id;
    study_checker_checked_node(checker, node_id)->has_type = true;
    study_checker_checked_node(checker, node_id)->type = declaration->type;
    *type = declaration->type;
    return true;
}

static bool study_checker_check_expression(
    StudyChecker *checker,
    StudySyntaxNodeId node_id,
    StudySyntaxType *type
) {
    const StudySyntaxNode *node = study_checker_node(checker, node_id);
    StudyCheckedNode *checked = study_checker_checked_node(checker, node_id);
    StudySyntaxNodeId left;
    StudySyntaxNodeId right;
    StudySyntaxType left_type;
    StudySyntaxType right_type;

    switch (node->kind) {
        case STUDY_SYNTAX_INTEGER_EXPRESSION:
            *type = STUDY_SYNTAX_TYPE_I32;
            break;
        case STUDY_SYNTAX_BOOLEAN_EXPRESSION:
            *type = STUDY_SYNTAX_TYPE_BOOL;
            break;
        case STUDY_SYNTAX_NAME_EXPRESSION: {
            StudyDeclarationId declaration_id = study_checker_resolve_local(
                checker,
                node->as.named.name_token
            );

            if (declaration_id == STUDY_DECLARATION_NONE) {
                study_checker_error_name(
                    checker,
                    node->as.named.name_token,
                    "unknown local name"
                );
                return false;
            }

            checked->declaration = declaration_id;
            *type = checker->program.declarations[declaration_id].type;
            break;
        }
        case STUDY_SYNTAX_CALL_EXPRESSION:
            return study_checker_check_call(checker, node_id, type);
        case STUDY_SYNTAX_GROUP_EXPRESSION:
            left = node->first_child;
            if (left == STUDY_SYNTAX_NODE_NONE ||
                !study_checker_check_expression(checker, left, type)) {
                return false;
            }
            break;
        case STUDY_SYNTAX_UNARY_EXPRESSION:
            left = node->first_child;
            if (left == STUDY_SYNTAX_NODE_NONE ||
                !study_checker_check_expression(checker, left, &left_type)) {
                return false;
            }

            if (node->as.operation.operator_kind == STUDY_TOKEN_MINUS) {
                if (!study_checker_require_type(
                        checker,
                        left,
                        left_type,
                        STUDY_SYNTAX_TYPE_I32,
                        "unary '-'"
                    )) {
                    return false;
                }
                *type = STUDY_SYNTAX_TYPE_I32;
            } else {
                if (!study_checker_require_type(
                        checker,
                        left,
                        left_type,
                        STUDY_SYNTAX_TYPE_BOOL,
                        "unary '!'"
                    )) {
                    return false;
                }
                *type = STUDY_SYNTAX_TYPE_BOOL;
            }
            break;
        case STUDY_SYNTAX_BINARY_EXPRESSION:
            left = node->first_child;
            right = left != STUDY_SYNTAX_NODE_NONE
                        ? study_checker_node(checker, left)->next_sibling
                        : STUDY_SYNTAX_NODE_NONE;
            if (left == STUDY_SYNTAX_NODE_NONE ||
                right == STUDY_SYNTAX_NODE_NONE ||
                !study_checker_check_expression(checker, left, &left_type) ||
                !study_checker_check_expression(checker, right, &right_type)) {
                return false;
            }

            switch (node->as.operation.operator_kind) {
                case STUDY_TOKEN_PLUS:
                case STUDY_TOKEN_MINUS:
                case STUDY_TOKEN_STAR:
                    if (!study_checker_require_type(
                            checker,
                            left,
                            left_type,
                            STUDY_SYNTAX_TYPE_I32,
                            "arithmetic operator"
                        ) ||
                        !study_checker_require_type(
                            checker,
                            right,
                            right_type,
                            STUDY_SYNTAX_TYPE_I32,
                            "arithmetic operator"
                        )) {
                        return false;
                    }
                    *type = STUDY_SYNTAX_TYPE_I32;
                    break;
                case STUDY_TOKEN_LESS:
                case STUDY_TOKEN_LESS_EQUAL:
                case STUDY_TOKEN_GREATER:
                case STUDY_TOKEN_GREATER_EQUAL:
                    if (!study_checker_require_type(
                            checker,
                            left,
                            left_type,
                            STUDY_SYNTAX_TYPE_I32,
                            "comparison operator"
                        ) ||
                        !study_checker_require_type(
                            checker,
                            right,
                            right_type,
                            STUDY_SYNTAX_TYPE_I32,
                            "comparison operator"
                        )) {
                        return false;
                    }
                    *type = STUDY_SYNTAX_TYPE_BOOL;
                    break;
                case STUDY_TOKEN_EQUAL_EQUAL:
                case STUDY_TOKEN_BANG_EQUAL:
                    if (left_type != right_type) {
                        study_diagnostic_set(
                            checker->diagnostic,
                            STUDY_DIAGNOSTIC_SOURCE,
                            checker->source->path,
                            "equality operands have different types: %s and %s",
                            study_syntax_type_name(left_type),
                            study_syntax_type_name(right_type)
                        );
                        study_checker_set_location(
                            checker,
                            study_checker_node(checker, node_id)->range.start
                        );
                        return false;
                    }
                    *type = STUDY_SYNTAX_TYPE_BOOL;
                    break;
                default:
                    study_checker_error_at_node(
                        checker,
                        node_id,
                        "binary expression has an unsupported operator"
                    );
                    return false;
            }
            break;
        default:
            study_checker_error_at_node(
                checker,
                node_id,
                "expected an expression during type checking"
            );
            return false;
    }

    checked->has_type = true;
    checked->type = *type;
    return true;
}

static bool study_checker_check_block(
    StudyChecker *checker,
    StudySyntaxNodeId block_id,
    bool create_scope,
    bool *definitely_returns
);

static bool study_checker_check_statement(
    StudyChecker *checker,
    StudySyntaxNodeId node_id,
    bool *definitely_returns
) {
    const StudySyntaxNode *node = study_checker_node(checker, node_id);
    StudyCheckedNode *checked = study_checker_checked_node(checker, node_id);
    StudySyntaxNodeId child;
    StudySyntaxNodeId second;
    StudySyntaxNodeId third;
    StudySyntaxType type;
    bool first_returns;
    bool second_returns;

    *definitely_returns = false;

    switch (node->kind) {
        case STUDY_SYNTAX_BLOCK:
            if (!study_checker_check_block(
                    checker,
                    node_id,
                    true,
                    definitely_returns
                )) {
                return false;
            }
            break;
        case STUDY_SYNTAX_VAR_STATEMENT:
            if (study_checker_find_current_scope(
                    checker,
                    node->as.binding.name_token
                ) != STUDY_DECLARATION_NONE) {
                study_checker_error_name(
                    checker,
                    node->as.binding.name_token,
                    "duplicate declaration of"
                );
                return false;
            }

            child = node->first_child;
            if (child == STUDY_SYNTAX_NODE_NONE ||
                !study_checker_check_expression(checker, child, &type) ||
                !study_checker_require_type(
                    checker,
                    child,
                    type,
                    node->as.binding.type,
                    "variable initializer"
                ) ||
                !study_checker_add_local_declaration(
                    checker,
                    node_id,
                    STUDY_DECLARATION_LOCAL,
                    node->as.binding.name_token,
                    node->as.binding.type
                )) {
                return false;
            }
            break;
        case STUDY_SYNTAX_ASSIGNMENT_STATEMENT: {
            StudyDeclarationId declaration_id = study_checker_resolve_local(
                checker,
                node->as.named.name_token
            );

            if (declaration_id == STUDY_DECLARATION_NONE) {
                study_checker_error_name(
                    checker,
                    node->as.named.name_token,
                    "assignment to unknown local name"
                );
                return false;
            }

            child = node->first_child;
            if (child == STUDY_SYNTAX_NODE_NONE ||
                !study_checker_check_expression(checker, child, &type) ||
                !study_checker_require_type(
                    checker,
                    child,
                    type,
                    checker->program.declarations[declaration_id].type,
                    "assignment"
                )) {
                return false;
            }
            checked->declaration = declaration_id;
            break;
        }
        case STUDY_SYNTAX_IF_STATEMENT:
            child = node->first_child;
            second = child != STUDY_SYNTAX_NODE_NONE
                         ? study_checker_node(checker, child)->next_sibling
                         : STUDY_SYNTAX_NODE_NONE;
            third = second != STUDY_SYNTAX_NODE_NONE
                        ? study_checker_node(checker, second)->next_sibling
                        : STUDY_SYNTAX_NODE_NONE;
            if (child == STUDY_SYNTAX_NODE_NONE ||
                second == STUDY_SYNTAX_NODE_NONE ||
                !study_checker_check_expression(checker, child, &type) ||
                !study_checker_require_type(
                    checker,
                    child,
                    type,
                    STUDY_SYNTAX_TYPE_BOOL,
                    "if condition"
                ) ||
                !study_checker_check_block(
                    checker,
                    second,
                    true,
                    &first_returns
                )) {
                return false;
            }

            if (third != STUDY_SYNTAX_NODE_NONE) {
                if (!study_checker_check_block(
                        checker,
                        third,
                        true,
                        &second_returns
                    )) {
                    return false;
                }
                *definitely_returns = first_returns && second_returns;
            }
            break;
        case STUDY_SYNTAX_WHILE_STATEMENT:
            child = node->first_child;
            second = child != STUDY_SYNTAX_NODE_NONE
                         ? study_checker_node(checker, child)->next_sibling
                         : STUDY_SYNTAX_NODE_NONE;
            if (child == STUDY_SYNTAX_NODE_NONE ||
                second == STUDY_SYNTAX_NODE_NONE ||
                !study_checker_check_expression(checker, child, &type) ||
                !study_checker_require_type(
                    checker,
                    child,
                    type,
                    STUDY_SYNTAX_TYPE_BOOL,
                    "while condition"
                ) ||
                !study_checker_check_block(
                    checker,
                    second,
                    true,
                    &first_returns
                )) {
                return false;
            }
            break;
        case STUDY_SYNTAX_RETURN_STATEMENT:
            child = node->first_child;
            if (child == STUDY_SYNTAX_NODE_NONE ||
                !study_checker_check_expression(checker, child, &type) ||
                !study_checker_require_type(
                    checker,
                    child,
                    type,
                    checker->program
                        .declarations[checker->current_function]
                        .type,
                    "return value"
                )) {
                return false;
            }
            *definitely_returns = true;
            break;
        case STUDY_SYNTAX_PRINT_STATEMENT:
            child = node->first_child;
            if (child == STUDY_SYNTAX_NODE_NONE ||
                !study_checker_check_expression(checker, child, &type) ||
                !study_checker_require_type(
                    checker,
                    child,
                    type,
                    STUDY_SYNTAX_TYPE_I32,
                    "print"
                )) {
                return false;
            }
            break;
        default:
            study_checker_error_at_node(
                checker,
                node_id,
                "expected a statement during semantic checking"
            );
            return false;
    }

    checked->definitely_returns = *definitely_returns;
    return true;
}

static bool study_checker_check_block(
    StudyChecker *checker,
    StudySyntaxNodeId block_id,
    bool create_scope,
    bool *definitely_returns
) {
    const StudySyntaxNode *block = study_checker_node(checker, block_id);
    size_t saved_binding_count = checker->binding_count;
    size_t saved_scope_start = checker->current_scope_start;
    StudySyntaxNodeId statement = block->first_child;
    bool block_returns = false;

    if (block->kind != STUDY_SYNTAX_BLOCK) {
        study_checker_error_at_node(
            checker,
            block_id,
            "semantic checker expected a block"
        );
        return false;
    }

    if (create_scope) {
        checker->current_scope_start = checker->binding_count;
    }

    while (statement != STUDY_SYNTAX_NODE_NONE) {
        bool statement_returns;

        if (!study_checker_check_statement(
                checker,
                statement,
                &statement_returns
            )) {
            checker->binding_count = saved_binding_count;
            checker->current_scope_start = saved_scope_start;
            return false;
        }
        if (!block_returns && statement_returns) {
            block_returns = true;
        }
        statement = study_checker_node(checker, statement)->next_sibling;
    }

    checker->binding_count = saved_binding_count;
    checker->current_scope_start = saved_scope_start;
    study_checker_checked_node(checker, block_id)->definitely_returns =
        block_returns;
    *definitely_returns = block_returns;
    return true;
}

static bool study_checker_check_function(
    StudyChecker *checker,
    StudySyntaxNodeId function_id
) {
    const StudySyntaxNode *function = study_checker_node(checker, function_id);
    StudyDeclarationId function_declaration =
        study_checker_checked_node(checker, function_id)->declaration;
    StudySyntaxNodeId child = function->first_child;
    bool definitely_returns;

    checker->binding_count = 0U;
    checker->current_scope_start = 0U;
    checker->current_function = function_declaration;

    while (child != STUDY_SYNTAX_NODE_NONE &&
           study_checker_node(checker, child)->kind ==
               STUDY_SYNTAX_PARAMETER) {
        const StudySyntaxNode *parameter = study_checker_node(checker, child);

        if (!study_checker_add_local_declaration(
                checker,
                child,
                STUDY_DECLARATION_PARAMETER,
                parameter->as.binding.name_token,
                parameter->as.binding.type
            )) {
            return false;
        }
        child = parameter->next_sibling;
    }

    if (child == STUDY_SYNTAX_NODE_NONE ||
        !study_checker_check_block(
            checker,
            child,
            false,
            &definitely_returns
        )) {
        return false;
    }

    if (!definitely_returns) {
        const StudyToken *name = &checker->tokens->items[
            function->as.function.name_token
        ];

        study_diagnostic_set(
            checker->diagnostic,
            STUDY_DIAGNOSTIC_SOURCE,
            checker->source->path,
            "function '%.*s' may reach its end without returning %s",
            study_checker_name_precision(
                checker,
                function->as.function.name_token
            ),
            (const char *)study_checker_token_bytes(
                checker,
                function->as.function.name_token
            ),
            study_syntax_type_name(
                checker->program.declarations[function_declaration].type
            )
        );
        study_checker_set_location(checker, name->range.start);
        return false;
    }

    study_checker_checked_node(checker, function_id)->definitely_returns = true;
    checker->binding_count = 0U;
    checker->current_scope_start = 0U;
    return true;
}

static bool study_checker_check_functions(StudyChecker *checker) {
    StudySyntaxNodeId function_id =
        study_checker_node(checker, checker->tree->root)->first_child;

    while (function_id != STUDY_SYNTAX_NODE_NONE) {
        if (!study_checker_check_function(checker, function_id)) {
            return false;
        }
        function_id = study_checker_node(checker, function_id)->next_sibling;
    }

    return true;
}

bool study_check_program(
    const StudySource *source,
    const StudyTokenList *tokens,
    const StudySyntaxTree *tree,
    StudyCheckedProgram *output,
    StudyDiagnostic *diagnostic
) {
    StudyChecker checker;
    size_t index;

    study_diagnostic_clear(diagnostic);

    if (output == NULL || !study_checked_program_is_empty(output)) {
        study_diagnostic_set(
            diagnostic,
            STUDY_DIAGNOSTIC_INTERNAL,
            source != NULL ? source->path : NULL,
            "checked program output must be initialized and empty"
        );
        return false;
    }

    if (source == NULL || source->path == NULL || source->bytes == NULL ||
        tokens == NULL || tokens->items == NULL || tokens->count == 0U ||
        tree == NULL || tree->nodes == NULL || tree->count == 0U ||
        tree->root == STUDY_SYNTAX_NODE_NONE || tree->root >= tree->count) {
        study_diagnostic_set(
            diagnostic,
            STUDY_DIAGNOSTIC_INTERNAL,
            source != NULL ? source->path : NULL,
            "semantic checker input is invalid"
        );
        return false;
    }

    checker.source = source;
    checker.tokens = tokens;
    checker.tree = tree;
    study_checked_program_init(&checker.program);
    checker.bindings = NULL;
    checker.binding_count = 0U;
    checker.binding_capacity = 0U;
    checker.current_scope_start = 0U;
    checker.current_function = STUDY_DECLARATION_NONE;
    checker.diagnostic = diagnostic;

    checker.program.nodes = calloc(tree->count, sizeof(*checker.program.nodes));
    if (checker.program.nodes == NULL) {
        study_diagnostic_set(
            diagnostic,
            STUDY_DIAGNOSTIC_INTERNAL,
            source->path,
            "not enough memory to annotate syntax nodes"
        );
        return false;
    }
    checker.program.node_count = tree->count;
    for (index = 0U; index < checker.program.node_count; index++) {
        checker.program.nodes[index].declaration = STUDY_DECLARATION_NONE;
    }

    if (!study_checker_collect_functions(&checker) ||
        !study_checker_check_functions(&checker)) {
        free(checker.bindings);
        study_checked_program_free(&checker.program);
        return false;
    }

    free(checker.bindings);
    *output = checker.program;
    return true;
}

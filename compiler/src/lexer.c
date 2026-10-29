#include "study/lexer.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { STUDY_INITIAL_TOKEN_CAPACITY = 64 };

typedef struct StudyLexer {
    const StudySource *source;
    StudyTokenList tokens;
    StudySourcePosition position;
    StudyDiagnostic *diagnostic;
} StudyLexer;

static bool study_token_list_is_empty(const StudyTokenList *tokens) {
    return tokens != NULL && tokens->items == NULL && tokens->count == 0U &&
           tokens->capacity == 0U;
}

static bool study_is_ascii_letter(unsigned char byte) {
    return (byte >= (unsigned char)'a' && byte <= (unsigned char)'z') ||
           (byte >= (unsigned char)'A' && byte <= (unsigned char)'Z');
}

static bool study_is_ascii_digit(unsigned char byte) {
    return byte >= (unsigned char)'0' && byte <= (unsigned char)'9';
}

static bool study_is_identifier_start(unsigned char byte) {
    return study_is_ascii_letter(byte) || byte == (unsigned char)'_';
}

static bool study_is_identifier_continue(unsigned char byte) {
    return study_is_identifier_start(byte) || study_is_ascii_digit(byte);
}

static bool study_lexer_at_end(const StudyLexer *lexer) {
    return lexer->position.byte_offset >= lexer->source->length;
}

static unsigned char study_lexer_peek(const StudyLexer *lexer) {
    if (study_lexer_at_end(lexer)) {
        return 0U;
    }

    return lexer->source->bytes[lexer->position.byte_offset];
}

static unsigned char study_lexer_peek_next(const StudyLexer *lexer) {
    size_t index = lexer->position.byte_offset;

    if (index >= lexer->source->length ||
        lexer->source->length - index < 2U) {
        return 0U;
    }

    return lexer->source->bytes[index + 1U];
}

static void study_lexer_advance_byte(StudyLexer *lexer) {
    if (study_lexer_at_end(lexer)) {
        return;
    }

    lexer->position.byte_offset += 1U;
    lexer->position.column += 1U;
}

static void study_lexer_advance_line_ending(StudyLexer *lexer) {
    unsigned char byte = study_lexer_peek(lexer);

    if (byte == (unsigned char)'\r') {
        study_lexer_advance_byte(lexer);
        if (study_lexer_peek(lexer) == (unsigned char)'\n') {
            study_lexer_advance_byte(lexer);
        }
    } else if (byte == (unsigned char)'\n') {
        study_lexer_advance_byte(lexer);
    } else {
        return;
    }

    lexer->position.line += 1U;
    lexer->position.column = 1U;
}

static void study_lexer_set_error(
    StudyLexer *lexer,
    StudySourcePosition position,
    const char *message
) {
    study_diagnostic_set(
        lexer->diagnostic,
        STUDY_DIAGNOSTIC_SOURCE,
        lexer->source->path,
        "%s",
        message
    );
    study_diagnostic_set_location(
        lexer->diagnostic,
        position.byte_offset,
        position.line,
        position.column
    );
}

static void study_lexer_set_byte_error(
    StudyLexer *lexer,
    StudySourcePosition position,
    unsigned char byte
) {
    if (byte >= (unsigned char)' ' && byte <= (unsigned char)'~') {
        study_diagnostic_set(
            lexer->diagnostic,
            STUDY_DIAGNOSTIC_SOURCE,
            lexer->source->path,
            "unsupported character '%c'",
            (int)byte
        );
    } else {
        study_diagnostic_set(
            lexer->diagnostic,
            STUDY_DIAGNOSTIC_SOURCE,
            lexer->source->path,
            "unsupported byte 0x%02X",
            (unsigned int)byte
        );
    }

    study_diagnostic_set_location(
        lexer->diagnostic,
        position.byte_offset,
        position.line,
        position.column
    );
}

static bool study_lexer_reserve_token(StudyLexer *lexer) {
    size_t next_capacity;
    StudyToken *grown;

    if (lexer->tokens.count < lexer->tokens.capacity) {
        return true;
    }

    if (lexer->tokens.capacity == 0U) {
        next_capacity = STUDY_INITIAL_TOKEN_CAPACITY;
    } else {
        if (lexer->tokens.capacity > SIZE_MAX / 2U) {
            study_lexer_set_error(
                lexer,
                lexer->position,
                "too many tokens to store"
            );
            return false;
        }
        next_capacity = lexer->tokens.capacity * 2U;
    }

    if (next_capacity > SIZE_MAX / sizeof(*lexer->tokens.items)) {
        study_lexer_set_error(
            lexer,
            lexer->position,
            "too many tokens to store"
        );
        return false;
    }

    grown = realloc(
        lexer->tokens.items,
        next_capacity * sizeof(*lexer->tokens.items)
    );
    if (grown == NULL) {
        study_lexer_set_error(
            lexer,
            lexer->position,
            "not enough memory to store tokens"
        );
        return false;
    }

    lexer->tokens.items = grown;
    lexer->tokens.capacity = next_capacity;
    return true;
}

static bool study_lexer_push(
    StudyLexer *lexer,
    StudyTokenKind kind,
    StudySourcePosition start,
    uint32_t integer_value
) {
    StudyToken *token;

    if (!study_lexer_reserve_token(lexer)) {
        return false;
    }

    token = &lexer->tokens.items[lexer->tokens.count];
    token->kind = kind;
    token->range.start = start;
    token->range.end = lexer->position;
    token->integer_value = integer_value;
    lexer->tokens.count += 1U;
    return true;
}

static void study_lexer_skip_ignored(StudyLexer *lexer) {
    for (;;) {
        unsigned char byte = study_lexer_peek(lexer);

        if (byte == (unsigned char)' ' || byte == (unsigned char)'\t') {
            study_lexer_advance_byte(lexer);
        } else if (byte == (unsigned char)'\r' ||
                   byte == (unsigned char)'\n') {
            study_lexer_advance_line_ending(lexer);
        } else if (byte == (unsigned char)'/' &&
                   study_lexer_peek_next(lexer) == (unsigned char)'/') {
            study_lexer_advance_byte(lexer);
            study_lexer_advance_byte(lexer);
            while (!study_lexer_at_end(lexer) &&
                   study_lexer_peek(lexer) != (unsigned char)'\r' &&
                   study_lexer_peek(lexer) != (unsigned char)'\n') {
                study_lexer_advance_byte(lexer);
            }
        } else {
            return;
        }
    }
}

static bool study_lexer_spelling_is(
    const StudyLexer *lexer,
    StudySourcePosition start,
    const char *spelling
) {
    size_t length = lexer->position.byte_offset - start.byte_offset;
    size_t spelling_length = strlen(spelling);

    return length == spelling_length &&
           memcmp(
               lexer->source->bytes + start.byte_offset,
               spelling,
               length
           ) == 0;
}

static StudyTokenKind study_lexer_identifier_kind(
    const StudyLexer *lexer,
    StudySourcePosition start
) {
    static const struct {
        const char *spelling;
        StudyTokenKind kind;
    } keywords[] = {
        {"fn", STUDY_TOKEN_FN},
        {"var", STUDY_TOKEN_VAR},
        {"if", STUDY_TOKEN_IF},
        {"else", STUDY_TOKEN_ELSE},
        {"while", STUDY_TOKEN_WHILE},
        {"return", STUDY_TOKEN_RETURN},
        {"print", STUDY_TOKEN_PRINT},
        {"true", STUDY_TOKEN_TRUE},
        {"false", STUDY_TOKEN_FALSE},
        {"i32", STUDY_TOKEN_I32},
        {"bool", STUDY_TOKEN_BOOL}
    };
    size_t index;

    for (index = 0U; index < sizeof(keywords) / sizeof(keywords[0]); index++) {
        if (study_lexer_spelling_is(lexer, start, keywords[index].spelling)) {
            return keywords[index].kind;
        }
    }

    return STUDY_TOKEN_IDENTIFIER;
}

static bool study_lexer_scan_identifier(
    StudyLexer *lexer,
    StudySourcePosition start
) {
    StudyTokenKind kind;

    do {
        study_lexer_advance_byte(lexer);
    } while (study_is_identifier_continue(study_lexer_peek(lexer)));

    kind = study_lexer_identifier_kind(lexer, start);
    return study_lexer_push(lexer, kind, start, 0U);
}

static bool study_lexer_scan_integer(
    StudyLexer *lexer,
    StudySourcePosition start
) {
    uint32_t value = 0U;
    const uint32_t limit = (uint32_t)INT32_MAX;

    do {
        uint32_t digit = (uint32_t)(study_lexer_peek(lexer) -
                                    (unsigned char)'0');

        if (value > (limit - digit) / 10U) {
            while (study_is_ascii_digit(study_lexer_peek(lexer))) {
                study_lexer_advance_byte(lexer);
            }
            study_lexer_set_error(
                lexer,
                start,
                "integer literal exceeds 2147483647"
            );
            return false;
        }

        value = value * 10U + digit;
        study_lexer_advance_byte(lexer);
    } while (study_is_ascii_digit(study_lexer_peek(lexer)));

    return study_lexer_push(lexer, STUDY_TOKEN_INTEGER, start, value);
}

static bool study_lexer_match_next(StudyLexer *lexer, unsigned char expected) {
    if (study_lexer_peek(lexer) != expected) {
        return false;
    }

    study_lexer_advance_byte(lexer);
    return true;
}

static bool study_lexer_scan_punctuation(
    StudyLexer *lexer,
    StudySourcePosition start,
    unsigned char byte
) {
    StudyTokenKind kind;

    study_lexer_advance_byte(lexer);

    switch (byte) {
        case (unsigned char)'(':
            kind = STUDY_TOKEN_LEFT_PAREN;
            break;
        case (unsigned char)')':
            kind = STUDY_TOKEN_RIGHT_PAREN;
            break;
        case (unsigned char)'{':
            kind = STUDY_TOKEN_LEFT_BRACE;
            break;
        case (unsigned char)'}':
            kind = STUDY_TOKEN_RIGHT_BRACE;
            break;
        case (unsigned char)',':
            kind = STUDY_TOKEN_COMMA;
            break;
        case (unsigned char)':':
            kind = STUDY_TOKEN_COLON;
            break;
        case (unsigned char)';':
            kind = STUDY_TOKEN_SEMICOLON;
            break;
        case (unsigned char)'+':
            kind = STUDY_TOKEN_PLUS;
            break;
        case (unsigned char)'*':
            kind = STUDY_TOKEN_STAR;
            break;
        case (unsigned char)'-':
            kind = study_lexer_match_next(lexer, (unsigned char)'>')
                       ? STUDY_TOKEN_ARROW
                       : STUDY_TOKEN_MINUS;
            break;
        case (unsigned char)'=':
            kind = study_lexer_match_next(lexer, (unsigned char)'=')
                       ? STUDY_TOKEN_EQUAL_EQUAL
                       : STUDY_TOKEN_EQUAL;
            break;
        case (unsigned char)'!':
            kind = study_lexer_match_next(lexer, (unsigned char)'=')
                       ? STUDY_TOKEN_BANG_EQUAL
                       : STUDY_TOKEN_BANG;
            break;
        case (unsigned char)'<':
            kind = study_lexer_match_next(lexer, (unsigned char)'=')
                       ? STUDY_TOKEN_LESS_EQUAL
                       : STUDY_TOKEN_LESS;
            break;
        case (unsigned char)'>':
            kind = study_lexer_match_next(lexer, (unsigned char)'=')
                       ? STUDY_TOKEN_GREATER_EQUAL
                       : STUDY_TOKEN_GREATER;
            break;
        default:
            study_lexer_set_byte_error(lexer, start, byte);
            return false;
    }

    return study_lexer_push(lexer, kind, start, 0U);
}

static bool study_lexer_scan_all(StudyLexer *lexer) {
    while (!study_lexer_at_end(lexer)) {
        StudySourcePosition start;
        unsigned char byte;

        study_lexer_skip_ignored(lexer);
        if (study_lexer_at_end(lexer)) {
            break;
        }

        start = lexer->position;
        byte = study_lexer_peek(lexer);

        if (study_is_identifier_start(byte)) {
            if (!study_lexer_scan_identifier(lexer, start)) {
                return false;
            }
        } else if (study_is_ascii_digit(byte)) {
            if (!study_lexer_scan_integer(lexer, start)) {
                return false;
            }
        } else if (!study_lexer_scan_punctuation(lexer, start, byte)) {
            return false;
        }
    }

    return study_lexer_push(lexer, STUDY_TOKEN_EOF, lexer->position, 0U);
}

bool study_lex(
    const StudySource *source,
    StudyTokenList *output,
    StudyDiagnostic *diagnostic
) {
    StudyLexer lexer;

    study_diagnostic_clear(diagnostic);

    if (output == NULL || !study_token_list_is_empty(output)) {
        study_diagnostic_set(
            diagnostic,
            STUDY_DIAGNOSTIC_INTERNAL,
            source != NULL ? source->path : NULL,
            "token output must be initialized and empty"
        );
        return false;
    }

    if (source == NULL || source->path == NULL || source->bytes == NULL) {
        study_diagnostic_set(
            diagnostic,
            STUDY_DIAGNOSTIC_INTERNAL,
            source != NULL ? source->path : NULL,
            "source input is invalid"
        );
        return false;
    }

    lexer.source = source;
    study_token_list_init(&lexer.tokens);
    lexer.position.byte_offset = 0U;
    lexer.position.line = 1U;
    lexer.position.column = 1U;
    lexer.diagnostic = diagnostic;

    if (!study_lexer_scan_all(&lexer)) {
        study_token_list_free(&lexer.tokens);
        return false;
    }

    *output = lexer.tokens;
    return true;
}

#ifndef STUDY_LOCATION_H
#define STUDY_LOCATION_H

#include <stddef.h>

/*
 * byte_offset is zero based. line and column are one based. A column counts
 * source bytes, not display cells, which keeps locations independent of tab
 * width and terminal rendering.
 */
typedef struct StudySourcePosition {
    size_t byte_offset;
    size_t line;
    size_t column;
} StudySourcePosition;

/* The end position is exclusive. */
typedef struct StudySourceRange {
    StudySourcePosition start;
    StudySourcePosition end;
} StudySourceRange;

#endif

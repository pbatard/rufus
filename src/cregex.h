#ifndef CREGEX_H
#define CREGEX_H

#define REGEX_VM_MAX_MATCHES 64

typedef enum {
    REGEX_NODE_TYPE_EPSILON = 0,
    /* Characters */
    REGEX_NODE_TYPE_CHARACTER,
    REGEX_NODE_TYPE_ANY_CHARACTER,
    REGEX_NODE_TYPE_CHARACTER_CLASS,
    REGEX_NODE_TYPE_CHARACTER_CLASS_NEGATED,
    /* Composites */
    REGEX_NODE_TYPE_CONCATENATION,
    REGEX_NODE_TYPE_ALTERNATION,
    /* Quantifiers */
    REGEX_NODE_TYPE_QUANTIFIER,
    /* Anchors */
    REGEX_NODE_TYPE_ANCHOR_BEGIN,
    REGEX_NODE_TYPE_ANCHOR_END,
    /* Captures */
    REGEX_NODE_TYPE_CAPTURE
} cregex_node_type;

typedef struct cregex_node {
    cregex_node_type type;
    union {
        /* REGEX_NODE_TYPE_CHARACTER */
        struct {
            int ch;
        };
        /* REGEX_NODE_TYPE_CHARACTER_CLASS,
         * REGEX_NODE_TYPE_CHARACTER_CLASS_NEGATED
         */
        struct {
            const char *from, *to;
        };
        /* REGEX_NODE_TYPE_QUANTIFIER */
        struct {
            int nmin, nmax, greedy;
            struct cregex_node *quantified;
        };
        /* REGEX_NODE_TYPE_CONCATENATION,
         * REGEX_NODE_TYPE_ALTERNATION
         */
        struct {
            struct cregex_node *left, *right;
        };
        /* REGEX_NODE_TYPE_CAPTURE */
        struct {
            struct cregex_node *captured;
        };
    };
} cregex_node_t;

typedef enum {
    REGEX_PROGRAM_OPCODE_MATCH = 0,
    /* Characters */
    REGEX_PROGRAM_OPCODE_CHARACTER,
    REGEX_PROGRAM_OPCODE_ANY_CHARACTER,
    REGEX_PROGRAM_OPCODE_CHARACTER_CLASS,
    REGEX_PROGRAM_OPCODE_CHARACTER_CLASS_NEGATED,
    /* Control-flow */
    REGEX_PROGRAM_OPCODE_SPLIT,
    REGEX_PROGRAM_OPCODE_JUMP,
    /* Assertions */
    REGEX_PROGRAM_OPCODE_ASSERT_BEGIN,
    REGEX_PROGRAM_OPCODE_ASSERT_END,
    /* Saving */
    REGEX_PROGRAM_OPCODE_SAVE
} cregex_program_opcode_t;

#include <limits.h>

typedef char cregex_char_class[(UCHAR_MAX + CHAR_BIT - 1) / CHAR_BIT];

static inline int cregex_char_class_contains(const cregex_char_class klass,
                                             int ch)
{
    return klass[ch / CHAR_BIT] & (1 << ch % CHAR_BIT);
}

static inline int cregex_char_class_add(cregex_char_class klass, int ch)
{
    klass[ch / CHAR_BIT] |= 1 << (ch % CHAR_BIT);
    return ch;
}

typedef struct cregex_program_instr {
    cregex_program_opcode_t opcode;
    union {
        /* REGEX_PROGRAM_OPCODE_CHARACTER */
        struct {
            int ch;
        };
        /* REGEX_PROGRAM_OPCODE_CHARACTER_CLASS,
         * REGEX_PROGRAM_OPCODE_CHARACTER_CLASS_NEGATED
         */
        struct {
            cregex_char_class klass;
        };
        /* REGEX_PROGRAM_OPCODE_SPLIT */
        struct {
            struct cregex_program_instr *first, *second;
        };
        /* REGEX_PROGRAM_OPCODE_JUMP */
        struct {
            struct cregex_program_instr *target;
        };
        /* REGEX_PROGRAM_OPCODE_SAVE */
        struct {
            int save;
        };
    };
} cregex_program_instr_t;

typedef struct {
    int ninstructions;
    cregex_program_instr_t instructions[];
} cregex_program_t;

/* Run program on string */
int cregex_program_run(const cregex_program_t *program,
                       const char *string,
                       const char **matches,
                       int nmatches);

/* Compile a parsed pattern */
cregex_program_t *cregex_compile_node(const cregex_node_t *root);

/* Free a compiled program */
void cregex_compile_free(cregex_program_t *program);

/* Parse a pattern */
cregex_node_t *cregex_parse(const char *pattern);

/* Free a parsed pattern */
void cregex_parse_free(cregex_node_t *root);

#endif

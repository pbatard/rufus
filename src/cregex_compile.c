#include <stdbool.h>
#include <stdlib.h>

#include "cregex.h"

typedef struct {
    cregex_program_instr_t *pc;
    int ncaptures;
} regex_compile_context;

static int count_instructions(const cregex_node_t *node)
{
    switch (node->type) {
    case REGEX_NODE_TYPE_EPSILON:
        return 0;

    /* Characters */
    case REGEX_NODE_TYPE_CHARACTER:
    case REGEX_NODE_TYPE_ANY_CHARACTER:
    case REGEX_NODE_TYPE_CHARACTER_CLASS:
    case REGEX_NODE_TYPE_CHARACTER_CLASS_NEGATED:
        return 1;

    /* Composites */
    case REGEX_NODE_TYPE_CONCATENATION:
        return count_instructions(node->left) + count_instructions(node->right);
    case REGEX_NODE_TYPE_ALTERNATION:
        return 2 + count_instructions(node->left) +
               count_instructions(node->right);

    /* Quantifiers */
    case REGEX_NODE_TYPE_QUANTIFIER: {
        int num = count_instructions(node->quantified);
        if (node->nmax >= node->nmin)
            return node->nmin * num + (node->nmax - node->nmin) * (num + 1);
        return 1 + (node->nmin ? node->nmin * num : num + 1);
    }

    /* Anchors */
    case REGEX_NODE_TYPE_ANCHOR_BEGIN:
    case REGEX_NODE_TYPE_ANCHOR_END:
        return 1;

    /* Captures */
    case REGEX_NODE_TYPE_CAPTURE:
        return 2 + count_instructions(node->captured);
    }

    /* should not reach here */
    return 0;
}

static bool node_is_anchored(const cregex_node_t *node)
{
    switch (node->type) {
    case REGEX_NODE_TYPE_EPSILON:
        return false;

    /* Characters */
    case REGEX_NODE_TYPE_CHARACTER:
    case REGEX_NODE_TYPE_ANY_CHARACTER:
    case REGEX_NODE_TYPE_CHARACTER_CLASS:
    case REGEX_NODE_TYPE_CHARACTER_CLASS_NEGATED:
        return false;

    /* Composites */
    case REGEX_NODE_TYPE_CONCATENATION:
        return node_is_anchored(node->left);
    case REGEX_NODE_TYPE_ALTERNATION:
        return node_is_anchored(node->left) && node_is_anchored(node->right);

    /* Quantifiers */
    case REGEX_NODE_TYPE_QUANTIFIER:
        return node_is_anchored(node->quantified);

    /* Anchors */
    case REGEX_NODE_TYPE_ANCHOR_BEGIN:
        return true;
    case REGEX_NODE_TYPE_ANCHOR_END:
        return false;

    /* Captures */
    case REGEX_NODE_TYPE_CAPTURE:
        return node_is_anchored(node->captured);
    }

    /* should not reach here */
    return false;
}

static inline cregex_program_instr_t *emit(
    regex_compile_context *context,
    const cregex_program_instr_t *instruction)
{
    *context->pc = *instruction;
    return context->pc++;
}

static cregex_program_instr_t *compile_char_class(
    const cregex_node_t *node,
    cregex_program_instr_t *instruction)
{
    const char *sp = node->from;

    for (;;) {
        int ch = *sp++;
        switch (ch) {
        case ']':
            if (sp - 1 == node->from)
                goto CHARACTER;
            return instruction;
        case '\\':
            ch = *sp++;
            /* fall-through */
        default:
        CHARACTER:
            if (*sp == '-' && sp[1] != ']') {
                for (; ch <= sp[1]; ++ch)
                    cregex_char_class_add(instruction->klass, ch);
                sp += 2;
            } else {
                cregex_char_class_add(instruction->klass, ch);
            }
            break;
        }
    }
}

static cregex_program_instr_t *compile_context(regex_compile_context *context,
                                               const cregex_node_t *node)
{
    cregex_program_instr_t *bottom = context->pc, *split, *jump;
    int ncaptures = context->ncaptures, capture;

    switch (node->type) {
    case REGEX_NODE_TYPE_EPSILON:
        break;

    /* Characters */
    case REGEX_NODE_TYPE_CHARACTER:
        emit(context,
             &(cregex_program_instr_t){.opcode = REGEX_PROGRAM_OPCODE_CHARACTER,
                                       .ch = node->ch});
        break;
    case REGEX_NODE_TYPE_ANY_CHARACTER:
        emit(context, &(cregex_program_instr_t){
                          .opcode = REGEX_PROGRAM_OPCODE_ANY_CHARACTER});
        break;
    case REGEX_NODE_TYPE_CHARACTER_CLASS:
        compile_char_class(
            node,
            emit(context, &(cregex_program_instr_t){
                              .opcode = REGEX_PROGRAM_OPCODE_CHARACTER_CLASS}));
        break;
    case REGEX_NODE_TYPE_CHARACTER_CLASS_NEGATED:
        compile_char_class(
            node,
            emit(context,
                 &(cregex_program_instr_t){
                     .opcode = REGEX_PROGRAM_OPCODE_CHARACTER_CLASS_NEGATED}));
        break;

    /* Composites */
    case REGEX_NODE_TYPE_CONCATENATION:
        compile_context(context, node->left);
        compile_context(context, node->right);
        break;
    case REGEX_NODE_TYPE_ALTERNATION:
        split = emit(context, &(cregex_program_instr_t){
                                  .opcode = REGEX_PROGRAM_OPCODE_SPLIT});
        split->first = compile_context(context, node->left);
        jump = emit(context, &(cregex_program_instr_t){
                                 .opcode = REGEX_PROGRAM_OPCODE_JUMP});
        split->second = compile_context(context, node->right);
        jump->target = context->pc;
        break;

    /* Quantifiers */
    case REGEX_NODE_TYPE_QUANTIFIER: {
        cregex_program_instr_t *last = NULL;
        for (int i = 0; i < node->nmin; ++i) {
            context->ncaptures = ncaptures;
            last = compile_context(context, node->quantified);
        }
        if (node->nmax > node->nmin) {
            for (int i = 0; i < node->nmax - node->nmin; ++i) {
                context->ncaptures = ncaptures;
                split =
                    emit(context, &(cregex_program_instr_t){
                                      .opcode = REGEX_PROGRAM_OPCODE_SPLIT});
                split->first = compile_context(context, node->quantified);
                split->second = context->pc;
                if (!node->greedy) {
                    cregex_program_instr_t *swap = split->first;
                    split->first = split->second;
                    split->second = swap;
                }
            }
        } else if (node->nmax == -1) {
            split = emit(context, &(cregex_program_instr_t){
                                      .opcode = REGEX_PROGRAM_OPCODE_SPLIT});
            if (node->nmin == 0) {
                split->first = compile_context(context, node->quantified);
                jump = emit(context, &(cregex_program_instr_t){
                                         .opcode = REGEX_PROGRAM_OPCODE_JUMP});
                split->second = context->pc;
                jump->target = split;
            } else {
                split->first = last;
                split->second = context->pc;
            }
            if (!node->greedy) {
                cregex_program_instr_t *swap = split->first;
                split->first = split->second;
                split->second = swap;
            }
        }
        break;
    }

    /* Anchors */
    case REGEX_NODE_TYPE_ANCHOR_BEGIN:
        emit(context, &(cregex_program_instr_t){
                          .opcode = REGEX_PROGRAM_OPCODE_ASSERT_BEGIN});
        break;
    case REGEX_NODE_TYPE_ANCHOR_END:
        emit(context, &(cregex_program_instr_t){
                          .opcode = REGEX_PROGRAM_OPCODE_ASSERT_END});
        break;

    /* Captures */
    case REGEX_NODE_TYPE_CAPTURE:
        capture = context->ncaptures++ * 2;
        emit(context,
             &(cregex_program_instr_t){.opcode = REGEX_PROGRAM_OPCODE_SAVE,
                                       .save = capture});
        compile_context(context, node->captured);
        emit(context,
             &(cregex_program_instr_t){.opcode = REGEX_PROGRAM_OPCODE_SAVE,
                                       .save = capture + 1});
        break;
    }

    return bottom;
}

/* Compile a parsed pattern (using a previously allocated program with at least
 * estimate_instructions(root) instructions).
 */
#if defined __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdangling-pointer"
#endif
static cregex_program_t *compile_node_with_program(const cregex_node_t *root,
                                                   cregex_program_t *program)
{
    /* Silence a MinGW warning about dangling pointers */
    static cregex_node_t* _root;
    /* add capture node for entire match */
    root = &(cregex_node_t){.type = REGEX_NODE_TYPE_CAPTURE,
                            .captured = (cregex_node_t *) root};

    /* add .*? unless pattern starts with ^ */
    if (!node_is_anchored(root)) {
        _root = &(cregex_node_t){
            .type = REGEX_NODE_TYPE_CONCATENATION,
            .left =
                &(cregex_node_t){
                    .type = REGEX_NODE_TYPE_QUANTIFIER,
                    .nmin = 0,
                    .nmax = -1,
                    .greedy = 0,
                    .quantified = &(
                        cregex_node_t){.type = REGEX_NODE_TYPE_ANY_CHARACTER}},
            .right = (cregex_node_t *) root};
        root = _root;
     }

    /* compile */
    regex_compile_context *context =
        &(regex_compile_context){.pc = program->instructions, .ncaptures = 0};
    compile_context(context, root);

    /* emit final match instruction */
    emit(context,
         &(cregex_program_instr_t){.opcode = REGEX_PROGRAM_OPCODE_MATCH});

    /* set total number of instructions */
    program->ninstructions = (int)(context->pc - program->instructions);

    return program;
}
#if defined __GNUC__
#pragma GCC diagnostic pop
#endif

/* Upper bound of number of instructions required to compile parsed pattern. */
static int estimate_instructions(const cregex_node_t *root)
{
    return count_instructions(root)
           /* .*? is added unless pattern starts with ^,
            * save instructions are added for beginning and end of match,
            * a final match instruction is added to the end of the program
            */
           + !node_is_anchored(root) * 3 + 2 + 1;
}

cregex_program_t *cregex_compile_node(const cregex_node_t *root)
{
    size_t size = sizeof(cregex_program_t) +
                  sizeof(cregex_program_instr_t) * estimate_instructions(root);
    cregex_program_t *program;

    if (!(program = malloc(size)))
        return NULL;

    if (!compile_node_with_program(root, program)) {
        free(program);
        return NULL;
    }

    return program;
}

/* Free a compiled program */
void cregex_compile_free(cregex_program_t *program)
{
    free(program);
}

#ifndef HK_TEST_MICROPYTHON_STUB_H
#define HK_TEST_MICROPYTHON_STUB_H

#include <setjmp.h>
#include <stddef.h>
#include <stdint.h>

typedef uint32_t qstr;
typedef void *mp_obj_t;
typedef struct { qstr source_name; } mp_lexer_t;
typedef struct { uint8_t unused; } mp_parse_tree_t;
typedef struct { jmp_buf jump; void *ret_val; } nlr_buf_t;

extern nlr_buf_t *s_test_nlr;
extern int mp_plat_print;
extern int mp_type_KeyboardInterrupt;

#define MP_QSTR__lt_stdin_gt_ 1U
#define MP_PARSE_FILE_INPUT 1
#define nlr_push(buffer) (s_test_nlr = (buffer), setjmp((buffer)->jump))
#define nlr_pop() (s_test_nlr = NULL)

mp_lexer_t *mp_lexer_new_from_str_len(qstr name, const char *source,
    size_t length, uint32_t flags);
mp_parse_tree_t mp_parse(mp_lexer_t *lexer, int mode);
mp_obj_t mp_compile(mp_parse_tree_t *tree, qstr source_name, int is_repl);
mp_obj_t mp_call_function_0(mp_obj_t module);
void mp_obj_print_exception(const int *print, mp_obj_t exception);
void mp_raise_type(const int *type);
void mp_embed_init(void *heap, size_t bytes, void *stack_anchor);
void mp_embed_deinit(void);
void mp_stack_set_limit(size_t bytes);

#endif

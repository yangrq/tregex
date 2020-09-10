#ifndef TREGEX_HEADER
#define TREGEX_HEADER
#include <stdint.h>
#include <stdlib.h>
#include <ctype.h>

#ifdef _MSC_VER
#include <intrin.h>
static inline int __builtin_ctzll(unsigned long long x) {
  unsigned long ret;
  _BitScanForward64(&ret, x);
  return (int)ret;
}
#else
#include <x86intrin.h>
#endif

typedef int32_t tregex_byte_code;

#define INITIAL_STACK_SIZE    256 
#define MAX_STACK_SIZE        1024*1024 
#define MAX_BYTE_CODE_SIZE    1024
#define POOL_BLOCK_SIZE       32

#define OP_DEFINE(op) OP_DEFINE_IMPL(op)
#define OP_NUM 13
#define ALL_OP_DEFINE \
  OP_DEFINE(HALT)     \
  OP_DEFINE(PUSH)     \
  OP_DEFINE(REPEAT)   \
  OP_DEFINE(LOOP)     \
  OP_DEFINE(LOOP_SET) \
  OP_DEFINE(MATCH)    \
  OP_DEFINE(MATCH_SET)\
  OP_DEFINE(ANY)      \
  OP_DEFINE(BEGIN)    \
  OP_DEFINE(END)      \
  OP_DEFINE(SPLIT)    \
  OP_DEFINE(JMP)      \
  OP_DEFINE(ACCEPT)

#define OP_HALT_LEN               1
#define OP_PUSH_LEN               1
#define OP_REPEAT_LEN             2
#define OP_LOOP_LEN               2
#define OP_LOOP_SET_LEN           3
#define OP_MATCH_LEN              2
#define OP_MATCH_SET_LEN          3
#define OP_ANY_LEN                1
#define OP_BEGIN_LEN              1
#define OP_END_LEN                1
#define OP_SPLIT_LEN              3
#define OP_JMP_LEN                2
#define OP_ACCEPT_LEN             1
#define FETCH_OPCODE(inst)        ((inst)[0])
#define FETCH_OPARG_A(inst)       ((inst)[1]) 
#define FETCH_OPARG_B(inst)       ((inst)[2])
#define SET_OPCODE(buf, op)       ((buf)[0] = (op))
#define SET_OPARG_A(buf, a)       ((buf)[1] = (a))
#define SET_OPARG_B(buf, b)       ((buf)[2] = (b))
#define SET_OP_A(buf, op, ax)     ((buf)[0] = (op), (buf)[1] = (ax))
#define SET_OP_AB(buf, op, a, b)  ((buf)[0] = (op), (buf)[1] = (a), (buf)[2] = (b))
#define SET_OP_Z(buf, op)         ((buf)[0] = (op))
#define STEP_OP_A(p)              ((p) += 2)
#define STEP_OP_AB(p)             ((p) += 3)
#define STEP_OP_Z(p)              ((p) += 1)

#define ctzll(v)  __builtin_ctzll(v)
#define rdtsc()   __rdtsc()

#ifdef USE_LABELS_AS_VALUES
#define vmdispatch(x)     goto *disptab[x];
#define vmcase(label)     L_##label:
#define vmnext            goto *disptab[FETCH_OPCODE(&pcode[pc])]
#else
#define vmdispatch(op)    switch(op)
#define vmcase(label)     case label:
#define vmnext            goto next_loop
#endif

enum {
#undef OP_DEFINE_IMPL
#define OP_DEFINE_IMPL(op) op,
  ALL_OP_DEFINE
};

typedef struct _tregex_byte_code_list tregex_byte_code_list;
typedef struct _tregex_match_thread tregex_match_thread;
typedef struct _tregex_internal_stack_node tregex_internal_stack_node;
typedef struct _tregex_internal_stack tregex_internal_stack;
typedef struct _tregex_match_ctx tregex_match_ctx;
typedef struct _tregex_parse_ctx tregex_parse_ctx;
typedef struct _tregex_pool_ctx tregex_pool_ctx;

struct _tregex_byte_code_list {
  size_t len;
  tregex_byte_code code[1];
};

struct _tregex_match_thread {
  int pc;
  int idx;
  tregex_internal_stack *stack;
};

struct _tregex_internal_stack_node {
  tregex_internal_stack_node *parent, *left, *right;
  int idx;
  int count;
};

struct _tregex_internal_stack {
  tregex_internal_stack_node *root, *top;
};

struct _tregex_match_ctx {
  const char *str;
  int len;
  int pc;
  tregex_byte_code_list *code;
  tregex_match_thread *top;
  tregex_match_thread *stack;
  int stack_size;
};

struct _tregex_parse_ctx {
  const char *re;
  size_t len;
  size_t idx;
  tregex_byte_code *code;
  tregex_byte_code *cur;
};

struct _tregex_pool_ctx {
  uint64_t bitmap[16];
  void *raw;
};

tregex_byte_code_list *tregex_compile(const char *re);
int tregex_match(const char *re, const char *str, tregex_byte_code_list *compiled, tregex_pool_ctx *mem);
void tregex_dump(const tregex_byte_code_list *byte_code);
tregex_pool_ctx *tregex_pool_create();
void tregex_pool_clean(tregex_pool_ctx *pool);
void tregex_pool_destroy(tregex_pool_ctx *pool);
void *tregex_pool_alloc(tregex_pool_ctx *pool);
void tregex_pool_free(tregex_pool_ctx *pool, void *p);

#endif // !TREGEX_HEADER

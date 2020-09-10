#include "tregex.h"
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>

enum {
  EXPR,
  TERM,
  FACTOR,
};

tregex_pool_ctx *tregex_pool_create() {
  tregex_pool_ctx *pool = malloc(sizeof(tregex_pool_ctx));
  if (!pool) exit(-1);
  pool->raw = calloc(sizeof(pool->bitmap) * 8, POOL_BLOCK_SIZE);
  if (!pool->raw) exit(-1);
  memset(&pool->bitmap, -1, sizeof(pool->bitmap));
  return pool;
}

void tregex_pool_clean(tregex_pool_ctx *pool) {
  memset(&pool->bitmap, -1, sizeof(pool->bitmap));
}

void tregex_pool_destroy(tregex_pool_ctx *pool) {
  free(pool->raw);
  free(pool);
}

void *tregex_pool_alloc(tregex_pool_ctx *pool) {
  for (int i = 0; i < sizeof(pool->bitmap) / sizeof(pool->bitmap[0]); i++) {
    if (pool->bitmap[i]) {
      int offset = ctzll(pool->bitmap[i]);
      pool->bitmap[i] &= ~(1ull << offset);
      return (void *)((char *)pool->raw + (intptr_t)(offset + i * (sizeof(pool->bitmap[0]) * 8)) * POOL_BLOCK_SIZE);
    }
  }
  exit(2);
}

void tregex_pool_free(tregex_pool_ctx *pool, void *p) {
  char *tmp = p, *start = pool->raw;
  ptrdiff_t offset = (tmp - start) / POOL_BLOCK_SIZE;
  pool->bitmap[offset / (sizeof(pool->bitmap[0]) * 8)] |= 1ull << (offset % (sizeof(pool->bitmap[0]) * 8));
}

static tregex_internal_stack_node *tregex_internal_stack_node_alloc(tregex_pool_ctx *mem) {
  void *p = tregex_pool_alloc(mem);
  return p;
}

static tregex_internal_stack *tregex_internal_stack_alloc(tregex_pool_ctx *mem) {
  void *p = tregex_pool_alloc(mem);
  return p;
}

static tregex_internal_stack *tregex_internal_stack_create(tregex_pool_ctx *mem) {
  tregex_internal_stack *stack = tregex_internal_stack_alloc(mem);
  stack->root = tregex_internal_stack_node_alloc(mem);
  stack->root->parent = NULL;
  stack->root->right = NULL;
  stack->root->left = NULL;
  stack->root->count = 1;
  stack->root->idx = -1;
  stack->top = stack->root;
  return stack;
}

static tregex_internal_stack_node *tregex_internal_stack_push(tregex_pool_ctx *mem, tregex_internal_stack *stack, int idx) {
  tregex_internal_stack_node *top = stack->top;
  tregex_internal_stack_node *new_node = tregex_internal_stack_node_alloc(mem);
  new_node->idx = idx;
  new_node->count = 1;
  new_node->parent = top;
  new_node->left = NULL;
  new_node->right = NULL;
  if (!top->left)
    top->left = new_node;
  else {
    tregex_internal_stack_node *child = top->left;
    for (; child->right; child = child->right);
    child->right = new_node;
  }
  stack->top = new_node;
  return new_node;
}

static int tregex_internal_stack_pop(tregex_pool_ctx *mem, tregex_internal_stack *stack) {
  tregex_internal_stack_node *top = stack->top, *parent = top->parent;
  int idx = top->idx;
  if (top == stack->root)
    return -1;
  if (top->count == 1) {
    tregex_internal_stack_node *p = parent->left, *q = p->right;
    if (p == top)
      parent->left = top->right;
    else
      for (; q; p = q, q = q->right)
        if (q == top) {
          p->right = top->right;
          break;
        }
    tregex_pool_free(mem, top);
    stack->top = parent;
    return idx;
  }
  top->count--;
  parent->count++;
  stack->top = parent;
  return idx;
}

static int tregex_internal_stack_top(tregex_pool_ctx *mem, tregex_internal_stack *stack) {
  return stack->top->idx;
}

static tregex_internal_stack *tregex_internal_stack_copy(tregex_pool_ctx *mem, tregex_internal_stack *stack) {
  tregex_internal_stack *new_stack = tregex_internal_stack_alloc(mem);
  new_stack->root = stack->root;
  new_stack->top = stack->top;
  new_stack->top->count++;
  return new_stack;
}

static void tregex_internal_stack_destroy(tregex_pool_ctx *mem, tregex_internal_stack *stack) {
  while (stack->top != stack->root)
    tregex_internal_stack_pop(mem, stack);
  if (stack->root->count == 1)
    tregex_pool_free(mem, stack->root);
  else
    stack->root->count--;
  tregex_pool_free(mem, stack);
}

static int is_match_single_char(tregex_byte_code *begin, tregex_byte_code *end) {
  return FETCH_OPCODE(begin) == MATCH && (end - begin == OP_MATCH_LEN);
}

static int is_match_single_charset(tregex_byte_code *begin, tregex_byte_code *end) {
  return FETCH_OPCODE(begin) == MATCH_SET && (end - begin == OP_MATCH_SET_LEN);
}

static int tregex_parse(tregex_parse_ctx *ctx, int syntax_level) {
  int L1, L2;
  switch (syntax_level) {
  case EXPR: {
    tregex_byte_code *split_pos = ctx->cur;
    while (ctx->idx < ctx->len && ctx->re[ctx->idx] != '|' && ctx->re[ctx->idx] != ')')
      if (!tregex_parse(ctx, TERM)) return 0;
    if (ctx->idx < ctx->len && ctx->re[ctx->idx] == '|') {
      memmove(split_pos + OP_SPLIT_LEN, split_pos, sizeof(*split_pos) * (ctx->cur - split_pos));
      STEP_OP_AB(ctx->cur);
      tregex_byte_code *jmp = ctx->cur;
      STEP_OP_A(ctx->cur);
      SET_OP_AB(split_pos, SPLIT, OP_SPLIT_LEN, (char)(ctx->cur - split_pos));
      ctx->idx++;
      if (!tregex_parse(ctx, EXPR)) return 0;
      SET_OP_A(jmp, JMP, (char)(ctx->cur - jmp));
    }
  }
  case TERM: {
    tregex_byte_code *insert_pos = ctx->cur;
  loop:;
    while (ctx->idx < ctx->len &&
      ctx->re[ctx->idx] != '?' && ctx->re[ctx->idx] != '*' &&
      ctx->re[ctx->idx] != '+' && ctx->re[ctx->idx] != '|' &&
      ctx->re[ctx->idx] != ')') {
      insert_pos = ctx->cur;
      if (!tregex_parse(ctx, FACTOR)) return 0;
    }
    if (ctx->idx < ctx->len)
      switch (ctx->re[ctx->idx]) {
      case '?':
        ctx->idx++;
        memmove(insert_pos + OP_SPLIT_LEN, insert_pos, sizeof(*insert_pos) * (ctx->cur - insert_pos));
        STEP_OP_AB(ctx->cur);
        L1 = OP_SPLIT_LEN;
        L2 = (int)(ctx->cur - insert_pos);
        SET_OP_AB(insert_pos, SPLIT, L1, L2);
        goto loop;
      case '*':
        ctx->idx++;
        memmove(insert_pos + OP_PUSH_LEN + OP_SPLIT_LEN, insert_pos, sizeof(*insert_pos) * (ctx->cur - insert_pos));
        STEP_OP_Z(ctx->cur);
        STEP_OP_AB(ctx->cur);
        SET_OP_Z(insert_pos, PUSH);
        L1 = OP_SPLIT_LEN;
        L2 = OP_PUSH_LEN + (int)(ctx->cur - insert_pos);
        SET_OP_AB(insert_pos + OP_PUSH_LEN, SPLIT, L1, L2);
        L1 = OP_PUSH_LEN + (int)(insert_pos - ctx->cur);
        SET_OP_A(ctx->cur, REPEAT, L1);
        STEP_OP_A(ctx->cur);
        goto loop;
      case '+':
        ctx->idx++;
        if (is_match_single_char(insert_pos, ctx->cur)) {
          SET_OPCODE(insert_pos, LOOP);
          goto loop;
        }
        if (is_match_single_charset(insert_pos, ctx->cur)) {
          SET_OPCODE(insert_pos, LOOP_SET);
          goto loop;
        }
        memmove(insert_pos + OP_PUSH_LEN, insert_pos, sizeof(*insert_pos) * (ctx->cur - insert_pos));
        STEP_OP_Z(ctx->cur);
        SET_OP_Z(insert_pos, PUSH);
        L1 = OP_PUSH_LEN + (int)(insert_pos - ctx->cur);
        SET_OP_A(ctx->cur, REPEAT, L1);
        STEP_OP_A(ctx->cur);
        goto loop;
      }
  }
  case FACTOR:
    if (ctx->idx >= ctx->len) return 1;
    switch (ctx->re[ctx->idx]) {
    case '.':
      ctx->idx++;
      SET_OP_Z(ctx->cur, ANY);
      STEP_OP_Z(ctx->cur);
      return 1;
    case '^':
      ctx->idx++;
      SET_OP_Z(ctx->cur, BEGIN);
      STEP_OP_Z(ctx->cur);
      return 1;
    case '$':
      ctx->idx++;
      SET_OP_Z(ctx->cur, END);
      STEP_OP_Z(ctx->cur);
      return 1;
    case '\\':
      if (ctx->idx + 1 >= ctx->len)
        return 0;
      SET_OP_A(ctx->cur, MATCH, ctx->re[ctx->idx + 1]);
      STEP_OP_A(ctx->cur);
      ctx->idx += 2;
      return 1;
    case '[':
      if (ctx->idx + 4 >= ctx->len)
        return 0;
      if (ctx->re[ctx->idx + 2] != '-')
        return 0;
      if (ctx->re[ctx->idx + 4] != ']')
        return 0;
      SET_OP_AB(ctx->cur, MATCH_SET,
        (tregex_byte_code)ctx->re[ctx->idx + 1],
        (tregex_byte_code)ctx->re[ctx->idx + 3]);
      STEP_OP_AB(ctx->cur);
      ctx->idx += 5;
      return 1;
    case '(':
      ctx->idx++;
      tregex_parse(ctx, EXPR);
      if (ctx->idx >= ctx->len || ctx->re[ctx->idx] != ')')
        return 0;
      ctx->idx++;
      return 1;
    case '|': case ')':
      return 1;
    default:
      SET_OP_A(ctx->cur, MATCH, ctx->re[ctx->idx++]);
      STEP_OP_A(ctx->cur);
      return 1;
    }
  }
  return 1;
}

tregex_byte_code_list *tregex_compile(const char *re) {
  tregex_byte_code_list *bcl = calloc(1, sizeof(tregex_byte_code_list) + sizeof(tregex_byte_code) * (MAX_BYTE_CODE_SIZE - 1));
  if (!bcl) return NULL;

  tregex_parse_ctx parse_ctx = { 0 };
  parse_ctx.re = re;
  parse_ctx.len = strlen(re);
  parse_ctx.code = bcl->code;
  parse_ctx.cur = bcl->code;

  if (tregex_parse(&parse_ctx, EXPR) && parse_ctx.idx == parse_ctx.len) {
    SET_OP_Z(parse_ctx.cur, ACCEPT);
    STEP_OP_Z(parse_ctx.cur);
    bcl->len = parse_ctx.cur - bcl->code;
  }
  else {
    SET_OP_Z(bcl->code, HALT);
    bcl->len = 1;
  }

  return bcl;
}

static int tregex_extend_stack(tregex_match_ctx *ctx) {
  int stack_used = ctx->stack_size, new_size = ctx->stack_size + ctx->stack_size / 2;
  if (new_size > MAX_STACK_SIZE)
    return 0;
  void *p = realloc(ctx->stack, new_size * sizeof(*ctx->stack));
  if (!p) exit(-1);
  ctx->stack = (tregex_match_thread *)p;
  ctx->top = ctx->stack + stack_used;
  ctx->stack_size = new_size;
  memset(ctx->top, 0, ((size_t)new_size - stack_used) * sizeof(*ctx->stack));
  return new_size;
}

static int tregex_execute(tregex_pool_ctx *mem, tregex_match_ctx *ctx) {
#ifdef USE_LABELS_AS_VALUES
  static void *disptab[OP_NUM] = {
#undef OP_DEFINE_IMPL
#define OP_DEFINE_IMPL(op) &&L_##op,
    ALL_OP_DEFINE
  };
#endif 
  tregex_byte_code *pcode = ctx->code->code;
  tregex_internal_stack *stack0 = tregex_internal_stack_create(mem);
  *ctx->top++ = (tregex_match_thread){ 0,0, stack0 };

fail_loop:;
  while (ctx->top > ctx->stack) {
    --ctx->top;
    int pc = ctx->top->pc;
    int idx = ctx->top->idx;
    tregex_internal_stack *istack = ctx->top->stack;
#ifndef USE_LABELS_AS_VALUES
    next_loop:;
#endif
    vmdispatch(FETCH_OPCODE(&pcode[pc])) {
      vmcase(HALT) {
        return -1;
      }
      vmcase(PUSH) {
        tregex_internal_stack_push(mem, istack, idx);
        STEP_OP_Z(pc);
        vmnext;
      }
      vmcase(REPEAT) {
        if (tregex_internal_stack_top(mem, istack) == idx) {
          tregex_internal_stack_pop(mem, istack);
          STEP_OP_A(pc);
          vmnext;
        }
        if (ctx->top - ctx->stack >= (ptrdiff_t)ctx->stack_size - 1 && !tregex_extend_stack(ctx))
          exit(-1);
        *ctx->top++ = (tregex_match_thread){ pc + OP_REPEAT_LEN, idx, tregex_internal_stack_copy(mem, istack) };
        pc += FETCH_OPARG_A(&pcode[pc]);
        vmnext;
      }
      vmcase(LOOP) {
        char tmp = FETCH_OPARG_A(&pcode[pc]);
        int initial_idx = idx;
        while (idx < ctx->len && ctx->str[idx] == tmp)
          idx++;
        if (idx == initial_idx) {
          tregex_internal_stack_destroy(mem, istack);
          goto fail_loop;
        }
        STEP_OP_A(pc);
        vmnext;
      }
      vmcase(LOOP_SET) {
        char left = FETCH_OPARG_A(&pcode[pc]);
        char right = FETCH_OPARG_B(&pcode[pc]);
        int initial_idx = idx;
        while (idx < ctx->len &&
          ctx->str[idx] >= left &&
          ctx->str[idx] <= right)
          idx++;
        if (idx == initial_idx) {
          tregex_internal_stack_destroy(mem, istack);
          goto fail_loop;
        }
        STEP_OP_AB(pc);
        vmnext;
      }
      vmcase(MATCH) {
        if (idx < ctx->len && ctx->str[idx] == (char)FETCH_OPARG_A(&pcode[pc])) {
          idx++;
          STEP_OP_A(pc);
          vmnext;
        }
        tregex_internal_stack_destroy(mem, istack);
        goto fail_loop;
      }
      vmcase(MATCH_SET) {
        if (idx < ctx->len &&
          ctx->str[idx] >= (char)FETCH_OPARG_A(&pcode[pc]) &&
          ctx->str[idx] <= (char)FETCH_OPARG_B(&pcode[pc])) {
          idx++;
          STEP_OP_AB(pc);
          vmnext;
        }
        tregex_internal_stack_destroy(mem, istack);
        goto fail_loop;
      }
      vmcase(ANY) {
        if (idx < ctx->len) {
          idx++;
          STEP_OP_Z(pc);
          vmnext;
        }
        tregex_internal_stack_destroy(mem, istack);
        goto fail_loop;
      }
      vmcase(BEGIN) {
        if (idx == 0) {
          STEP_OP_Z(pc);
          vmnext;
        }
        tregex_internal_stack_destroy(mem, istack);
        goto fail_loop;
      }
      vmcase(END) {
        if (idx == ctx->len) {
          STEP_OP_Z(pc);
          vmnext;
        }
        tregex_internal_stack_destroy(mem, istack);
        goto fail_loop;
      }
      vmcase(SPLIT) {
        if (ctx->top - ctx->stack >= (ptrdiff_t)ctx->stack_size - 1 && !tregex_extend_stack(ctx))
          exit(-1);
        *ctx->top++ = (tregex_match_thread){ pc + FETCH_OPARG_B(&pcode[pc]),idx, tregex_internal_stack_copy(mem, istack) };
        pc += FETCH_OPARG_A(&pcode[pc]);
        vmnext;
      }
      vmcase(JMP) {
        pc += FETCH_OPARG_A(&pcode[pc]);
        vmnext;
      }
      vmcase(ACCEPT) {
        tregex_internal_stack_destroy(mem, istack);
        return idx;
      }
    }
  }

  return -1;
}

int tregex_match(const char *re, const char *str, tregex_byte_code_list *compiled, tregex_pool_ctx *mem) {
  tregex_match_ctx ctx = { 0 };
  tregex_byte_code_list *bcl = compiled ? compiled : tregex_compile(re);
  tregex_pool_ctx *pool = mem ? mem : tregex_pool_create();

  if (!bcl) return 0;

  ctx.str = str;
  ctx.len = (int)strlen(str);
  ctx.pc = 0;
  ctx.code = bcl;
  ctx.stack = calloc(1, INITIAL_STACK_SIZE * sizeof(*ctx.stack));
  if (!ctx.stack) exit(-1);
  ctx.top = ctx.stack;
  ctx.stack_size = INITIAL_STACK_SIZE;

  int match_end = tregex_execute(pool, &ctx);
  free(ctx.stack);
  if (!compiled)
    free(bcl);
  if (!mem)
    tregex_pool_destroy(pool);

  return match_end;
}

void tregex_dump(const tregex_byte_code_list *byte_code) {
  const tregex_byte_code *p = byte_code->code, *q = p;
  static const char *byte_code_name[] = {
#undef OP_DEFINE_IMPL
#define OP_DEFINE_IMPL(op) #op,
  ALL_OP_DEFINE
  };
  for (size_t i = 0; i < byte_code->len;) {
    p = &byte_code->code[i];
    int op = FETCH_OPCODE(p);
    printf("%d\t ", (int)(p - q));
    if (op > ACCEPT) continue;
    printf("%s ", byte_code_name[op]);
    switch (op) {
    case LOOP:
    case MATCH:
      printf("\t\t%c(\\%d)\n",
        (char)FETCH_OPARG_A(p), (int)(char)FETCH_OPARG_A(p));
      STEP_OP_A(i);
      continue;
    case LOOP_SET:
    case MATCH_SET:
      printf("\t%c(\\%d), %c(\\%d)\n",
        (char)FETCH_OPARG_A(p), (int)(char)FETCH_OPARG_A(p),
        (char)FETCH_OPARG_B(p), (int)(char)FETCH_OPARG_B(p));
      STEP_OP_AB(i);
      continue;
    case HALT:
    case PUSH:
    case ANY:
    case BEGIN:
    case END:
    case ACCEPT:
      printf("\n");
      STEP_OP_Z(i);
      continue;
    case SPLIT:
      printf("\t\t%d, %d\n",
        (char)FETCH_OPARG_A(p) + (int)(p - q), (char)FETCH_OPARG_B(p) + (int)(p - q));
      STEP_OP_AB(i);
      continue;
    case REPEAT:
      printf("\t%d\n",
        (char)FETCH_OPARG_A(p) + (int)(p - q));
      STEP_OP_A(i);
      continue;
    case JMP:
      printf("\t\t%d\n",
        (char)FETCH_OPARG_A(p) + (int)(p - q));
      STEP_OP_A(i);
      continue;
    }
  }
  printf("\n%d instructions were dumped\n", (int)(p - q) + 1);
}
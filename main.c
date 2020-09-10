#include "tregex.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main() {
  tregex_pool_ctx *pool = tregex_pool_create();
  for (;;) {
    tregex_pool_clean(pool);
    char re[1024], str[1024];
    unsigned long long time_start, time_end;

    printf("RE> ");
    (void)fgets(re, 1024, stdin);
    re[strlen(re) - 1] = 0;

    printf("STR> ");
    (void)fgets(str, 1024, stdin);
    str[strlen(str) - 1] = 0;

    time_start = rdtsc();
    tregex_byte_code_list *compiled = tregex_compile(re);
    time_end = rdtsc();
    tregex_dump(compiled);
    printf("compiliation costs %llu ticks\n", time_end - time_start);

    time_start = rdtsc();
    int match_end_pos = tregex_match(NULL, str, compiled, pool);
    time_end = rdtsc();
    printf("match %s, costs %llu ticks\n", match_end_pos == -1 ? "failed" : "succeed", time_end - time_start);

    free(compiled);
  }
}
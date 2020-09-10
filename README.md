# Tregex

an experimental and simple regular expression library in C.

## Sample

#### simplest

```c
tregex_match("^([a-z]|[A-Z]|[0-9]|_)+$", "hello_world", NULL, NULL);
```

#### complex

```c
char re[] = "^([a-z]|[A-Z]|[0-9]|_)+$";
char str[] = "hello_world";

tregex_pool_ctx *pool = tregex_pool_create();
tregex_byte_code_list *compiled = tregex_compile(re);
if (tregex_match(NULL, str, compiled, pool) != -1)
    printf("success");

free(compiled);
tregex_pool_destroy(pool);
```

## License

[MIT](LICENSE)


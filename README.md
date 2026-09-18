# nyoncc

A C-to-AArch64 cross-compiler I vibe-coded to learn how compiling works. Made for fun.

**Vibe code disclaimer**: ~85% LLM, ~15% me. I had no idea how compilers worked before this. I asked an LLM to build one, it built one, I broke it, it fixed it, repeat until it runs `fib(10)`. This is not a serious project — it's a learning toy.

## What it does

Compiles a tiny C subset to AArch64 Linux assembly, then assembles/links/runs under qemu.

## Supported C subset

- Types: `int`, `char`, `void`, pointers
- Variables: local and global
- Functions: with parameters and return values
- Control flow: `if/else`, `while`, `for`, `return`, `break`, `continue`
- Operators: `+`, `-`, `*`, `/`, `%`, `==`, `!=`, `<`, `>`, `<=`, `>=`, `&&`, `||`, `!`, `~`, `&`
- Arrays: basic bracket access (partial)
- String literals

## Build & run

```bash
gcc pcc.c -o pcc
./pcc input.c output.s
aarch64-linux-gnu-as -o input.o output.s
aarch64-linux-gnu-ld -o input input.o
qemu-aarch64 ./input
```

## Notes

- Recursive functions have stack issues (fib returns wrong values)
- Arrays are partially broken
- This is a learning experiment, not production software

## License

MIT

# nyoncc

A C-to-AArch64 cross-compiler I vibe-coded to learn how compiling works. Made for fun (and pain)

**disclaimer**: ~85% LLM, ~15% my code. I made this for fun while waiting to rejailbroken my phone and working on deltalator again

## What it does

Compiles a tiny C subset to AArch64 Linux assembly,that all

## Build & run

```bash
gcc pcc.c -o pcc
./pcc input.c output.s
aarch64-linux-gnu-as -o input.o output.s
aarch64-linux-gnu-ld -o input input.o
qemu-aarch64 ./input
```

## Notes

- This is a learning experiment, not production software
- kawkaw is the best
- also krasei
- deltarune tomarrow ig
- dont litteray push everything to github again
- dont let claude witre an readme again

## License

MIT

#!/bin/bash
# pcc - Simple C to AArch64 Linux Cross-Compiler
# Build and use instructions

set -e

# Build the compiler
gcc pcc.c -o pcc

echo "pcc built successfully!"
echo ""
echo "Usage: ./pcc input.c [output.s]"
echo ""
echo "Example:"
echo "  ./pcc test.c test.s"
echo "  aarch64-linux-gnu-as -o test.o test.s"
echo "  aarch64-linux-gnu-ld -o test test.o"
echo ""
echo "Or with libc:"
echo "  aarch64-linux-gnu-as -o test.o test.s"
echo "  aarch64-linux-gnu-gcc -o test test.o"

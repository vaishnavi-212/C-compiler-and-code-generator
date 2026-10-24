#!/bin/sh
# Build the compiler on Linux / macOS (needs g++ and make; flex + bison only to regenerate the scanner/parser)
cd "$(dirname "$0")/compiler" || exit 1
if command -v flex >/dev/null && command -v bison >/dev/null; then make; else echo "flex/bison not found: using the generated files in compiler/build"; make prebuilt; fi

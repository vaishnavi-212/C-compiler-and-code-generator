# Build the compiler  ->  ./minic
#   make            regenerate scanner/parser with flex + bison, then compile
#   make prebuilt   compile from the generated files in build/ (no flex/bison needed)
CXX      ?= g++
BISON    ?= bison
FLEX     ?= flex
CXXFLAGS ?= -std=gnu++17 -O2 -Wall -Wno-unused-function -Wno-sign-compare -Wno-misleading-indentation
LDFLAGS  ?= -pthread
SRC      = src/hand_lexer.cpp src/hand_parser.cpp src/grammar.cpp src/semantic.cpp src/irgen.cpp src/analysis.cpp \
           src/vm.cpp src/compile.cpp src/threads_win32.cpp src/main.cpp
BIN      = minic

all: $(BIN)

build/parser.cpp build/parser.hpp: src/parser.y
	@mkdir -p build
	$(BISON) -d -v -o build/parser.cpp src/parser.y

build/lexer.cpp: src/lexer.l build/parser.hpp
	$(FLEX) -o build/lexer.cpp src/lexer.l

$(BIN): build/parser.cpp build/lexer.cpp $(SRC) src/*.h
	$(CXX) $(CXXFLAGS) -Isrc -Ibuild -o $@ build/parser.cpp build/lexer.cpp $(SRC) $(LDFLAGS)

prebuilt:
	$(CXX) $(CXXFLAGS) -Isrc -Ibuild -o $(BIN) build/parser.cpp build/lexer.cpp $(SRC) $(LDFLAGS)

clean:
	rm -f $(BIN)

.PHONY: all prebuilt clean

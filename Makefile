# -Wall -Wextra -pedantic pegam erros cedo; -std=c11 garante portabilidade;
# -O2 é a otimização pedida no steering e -g mantém os símbolos de depuração
# (valgrind/gdb mostram linha e função)
CC = gcc
CFLAGS = -Wall -Wextra -pedantic -std=c11 -O2 -g

# Pega todos os .c da pasta, sem listar um por um (menos exemplo_fifo.c, que
# é o plugin e vira uma biblioteca dinâmica à parte)
SRC = $(filter-out exemplo_fifo.c,$(wildcard *.c))
HDR = $(wildcard *.h)
TARGET = projetoSO

# Bibliotecas de cada sistema. Todas já vêm com o sistema (e o compilador):
# nada precisa ser baixado.
#  - Linux/WSL: -ldl para os plugins (dlopen; na glibc >= 2.34 já está na
#    libc, mas versões antigas precisam). A janela usa só sockets da libc.
#  - Windows (MSYS2/MinGW): gdi32 para a janela (user32 já vem por padrão);
#    os plugins ficam desativados e não há -ldl.
#  - macOS: os frameworks Cocoa e CoreGraphics e o runtime do Objective-C.
# Compilar para Windows a partir do Linux: make CC=x86_64-w64-mingw32-gcc
UNAME_S := $(shell uname -s 2>/dev/null)
ifeq ($(OS),Windows_NT)
SISTEMA = windows
else ifneq (,$(findstring mingw,$(CC)))
SISTEMA = windows
else ifeq ($(UNAME_S),Darwin)
SISTEMA = macos
else
SISTEMA = linux
endif

ifeq ($(SISTEMA),windows)
LDLIBS = -lgdi32
else ifeq ($(SISTEMA),macos)
LDLIBS = -ldl -framework Cocoa -framework CoreGraphics -lobjc
else
LDLIBS = -ldl
endif

all: $(TARGET)

$(TARGET): $(SRC) $(HDR)
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LDLIBS)

# Plugin de exemplo (escalonador numa biblioteca dinâmica)
plugins: exemplo_fifo.so

exemplo_fifo.so: exemplo_fifo.c sched.h
	$(CC) $(CFLAGS) -fPIC -shared -o $@ $<

# Bateria de testes (pastas tests/ e exemplos/)
test: $(TARGET) plugins
	sh tests/run_tests.sh

clean:
	rm -rf $(TARGET) $(TARGET).exe $(TARGET).dSYM *.so *_gantt.svg

.PHONY: all clean plugins test

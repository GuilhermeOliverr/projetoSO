# -Wall -Wextra -pedantic pegam erros cedo; -std=c11 garante portabilidade;
# -O2 é a otimização pedida no steering e -g mantém os símbolos de depuração
# (valgrind/gdb mostram linha e função)
CC = gcc
CFLAGS = -Wall -Wextra -pedantic -std=c11 -O2 -g -Iinclude

# Pega todos os .c de src/ (e subpastas), sem listar um por um
SRC = $(wildcard src/*.c src/*/*.c)
HDR = $(wildcard include/*.h)
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
plugins: plugins/exemplo_fifo.so

plugins/exemplo_fifo.so: plugins/exemplo_fifo.c include/sched.h include/task.h
	$(CC) $(CFLAGS) -fPIC -shared -o $@ $<

# Bateria de testes: arquivos válidos/inválidos e casos do capítulo 2
test: $(TARGET) plugins
	sh tests/run_tests.sh

clean:
	rm -rf $(TARGET) $(TARGET).exe $(TARGET).dSYM plugins/*.so *_gantt.svg

.PHONY: all clean plugins test

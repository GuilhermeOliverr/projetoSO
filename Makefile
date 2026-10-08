# -Wall -Wextra pegam erros cedo; -std=c11 garante portabilidade
CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -g

# Pega todos os .c da pasta, sem listar um por um
SRC = $(wildcard *.c)
HDR = $(wildcard *.h)
TARGET = projetoSO

# dlopen (plugins de escalonador): na glibc >= 2.34 já está na libc, mas
# versões antigas precisam de -ldl. No Windows (MSYS2) os plugins ficam
# desativados e não há -ldl.
ifeq ($(OS),Windows_NT)
LDLIBS =
else
LDLIBS = -ldl
endif

all: $(TARGET)

$(TARGET): $(SRC) $(HDR)
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LDLIBS)

# Plugin de exemplo (escalonador numa biblioteca dinâmica)
plugins: plugins/exemplo_fifo.so

plugins/exemplo_fifo.so: plugins/exemplo_fifo.c sched.h task.h
	$(CC) $(CFLAGS) -fPIC -shared -o $@ $<

# Bateria de testes: arquivos válidos/inválidos e casos do capítulo 2
test: $(TARGET) plugins
	sh tests/run_tests.sh

clean:
	rm -rf $(TARGET) $(TARGET).exe $(TARGET).dSYM plugins/*.so *_gantt.svg

.PHONY: all clean plugins test

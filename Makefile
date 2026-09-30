# -Wall -Wextra pegam erros cedo; -std=c11 garante portabilidade
CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -g

# Pega todos os .c de src/ e das subpastas, sem listar um por um
SRC = $(wildcard src/*.c src/*/*.c)
TARGET = projetoSO

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $(SRC)

clean:
	rm -f $(TARGET) $(TARGET).exe

.PHONY: all clean

# -Wall -Wextra pegam erros cedo; -std=c11 garante portabilidade
CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -g

# Pega todos os .c da pasta, sem listar um por um
SRC = $(wildcard *.c)
TARGET = projetoSO

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $(SRC)

clean:
	rm -rf $(TARGET) $(TARGET).exe $(TARGET).dSYM

.PHONY: all clean

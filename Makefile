# Compiler and flags
CC = gcc
CFLAGS = -pthread -Wall -Wextra -Werror
TARGET = membership

# Source files
SRC = membership.c
OBJ = $(SRC:.c=.o)

# Default target: Compile the project
all: $(TARGET)

# Compile the executable
$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET)

# Clean target: Remove generated files
clean:
	rm -f $(TARGET)


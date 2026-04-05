CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -pthread
SRC_DIR = src
TARGET = server

SOURCES = \
	$(SRC_DIR)/main.c \
	$(SRC_DIR)/protocol.c \
	$(SRC_DIR)/auth_client.c \
	$(SRC_DIR)/http_handler.c \
	$(SRC_DIR)/logger.c \
	$(SRC_DIR)/sensor_manager.c

OBJECTS = $(SOURCES:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) -o $@ $(OBJECTS)

$(SRC_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) $(OBJECTS)

.PHONY: all clean

CC := gcc
CFLAGS := -Wall
TARGET := writer
SRC := writer.c
OBJ := $(SRC:.c=.o)

# Check if CROSS_COMPILE is set
ifdef CROSS_COMPILE
    CC := $(CROSS_COMPILE)gcc
endif

all:
	$(CC) $(CFLAGS) -c $(SRC) -o $(OBJ)
	$(CC) $(CFLAGS) -o  $(TARGET) $(OBJ)

clean:
	rm -f $(TARGET) $(OBJ)

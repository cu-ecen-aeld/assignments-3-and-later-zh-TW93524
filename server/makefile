# if cross add pre
CC = $(CROSS_COMPILE)gcc
CFLAGS = -Wall -Werror


TARGET = aesdsocket
OBJS = aesdsocket.o

# default target
all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

#clean target
clean:
	rm -f $(TARGET) $(OBJS)

.PHONY: all clean

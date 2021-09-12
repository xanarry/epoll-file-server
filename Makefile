CC = gcc
SRC = $(wildcard *.c)
OBJS = $(patsubst %.c, %.o, $(SRC))
OBJS := $(filter-out client.o, $(OBJS))
CFLAG = -Wall

all: server client

server: $(OBJS)
	$(CC) $(CFLAG) $(OBJS) -o server
	rm *.o

client:
	$(CC) $(CFLAG) client.c -o client

%.o:%.c
	$(CC) -c $< $(CFLAG) -o $@


.PHONY: clean
clean:
	rm server client
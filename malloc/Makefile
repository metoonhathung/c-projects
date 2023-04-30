OUTPUT = mydriver
OBJS = mydriver.o mymalloc.o mm.o memlib.o
CFLAGS = -g -Wall -Wvla -fsanitize=address

%.o: %.c
	gcc $(CFLAGS) -c -o $@ $<
$(OUTPUT): $(OBJS)
	gcc $(CFLAGS) -o $@ $^
all: $(OUTPUT)

clean:
	rm -f *~ *.o $(OUTPUT)

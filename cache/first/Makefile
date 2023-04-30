OUTPUT = first
CFLAGS = -Wall -Werror -fsanitize=address -std=c11

all: $(OUTPUT)

%: %.c
	gcc $(CFLAGS) -o $@ $^ -lm

clean:
	rm -rf $(OUTPUT) *.tmp

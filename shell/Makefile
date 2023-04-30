OUTPUT = shell
CFLAGS = -g -Wall -Wvla -fsanitize=address

all: $(OUTPUT)

%: %.c
	gcc $(CFLAGS) -o $@ $^

clean:
	rm -rf $(OUTPUT) *.tmp

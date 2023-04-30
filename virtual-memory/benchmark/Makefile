
all : test
test: ../my_vm.h
	gcc test.c -L../ -lmy_vm -m32 -o test -lm
	gcc multi_test.c -L../ -lmy_vm -m32 -o mtest -lpthread -lm

clean:
	rm -rf test mtest

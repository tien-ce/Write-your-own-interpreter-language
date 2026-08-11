exec = ti.out
sources = $(wildcard src/*.c)
objects = $(sources:.c=.o)
flag = -g

$(exec): $(objects)
	gcc $(objects) $(flag) -o $(exec)

%.o: %.c
	gcc -c $(flag) $< -o $@

install:
	make
	cp ./ti.out /usr/local/bin/ti

clean:
	rm -rf $(exec) *.o src/*.o

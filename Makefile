exec = ti.out
sources = $(wildcard src/*.c)
objects = $(sources:.c=.o)
flag = -g
headers = $(wildcard src/include/*.h)

$(exec): $(objects)
	gcc $(objects) $(flag) -o $(exec)
%.o: %.c $(headers)
	gcc -c $(flag) $< -o $@

install:
	make
	cp ./ti.out /usr/local/bin/tien

clean:
	rm -rf $(exec) *.o src/*.o

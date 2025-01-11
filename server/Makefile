all : lolvn

lolvn : lolvn.c
	gcc -O4 -g -o $@ $^ -I.. -I../android -lpthread

clean :
	rm -rf *.o *~ lolvn

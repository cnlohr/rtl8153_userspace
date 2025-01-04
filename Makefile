all : test

CFLAGS:=-g
LDFLAGS:=-lusb-1.0

test : test.c netinterface.c rtl8153.c
	gcc -o $@ $^ $(CFLAGS) $(LDFLAGS)

clean :
	rm -rf *.o *~ test


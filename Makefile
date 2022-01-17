PKGS=glfw3 gl
CFLAGS=-Wall -Wextra -ggdb -I./include/ `pkg-config --cflags $(PKGS)`
LIBS=`pkg-config --libs $(PKGS)` -lm

main: main.c
	$(CC) $(CFLAGS) -o main main.c $(LIBS)


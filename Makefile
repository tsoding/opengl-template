PKGS=glfw3 gl
CFLAGS=-Wall -Wextra -ggdb -I./include/

main: main.c
	$(CC) $(CFLAGS) `pkg-config --cflags $(PKGS)` -o main main.c `pkg-config --libs $(PKGS)` -lm


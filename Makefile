GL_PKGS=glfw3 glew
CFLAGS=-Wall -Wextra

main: main.c
	$(CC) $(CFLAGS) `pkg-config --cflags $(GL_PKGS)` -o main main.c `pkg-config --libs $(GL_PKGS)`


#!/bin/sh

set -xe

CC=cc
CFLAGS="-Wall -Wextra -std=c11 -pedantic -ggdb -I./include/"
GLFW_FLAGS=`pkg-config glfw3 --cflags --libs-only-L`
LIBS="-lglfw -lGL -lm"

$CC $CFLAGS $GLFW_FLAGS -o main main.c $LIBS

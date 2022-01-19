#!/bin/sh

set -xe

CC=cc
CFLAGS="-Wall -Wextra -ggdb -I./include/"
LIBS="-lglfw -lGL -lm"

$CC $CFLAGS -o main main.c $LIBS

CC := gcc
C_FOLDER := ./src/
OBJ_FOLDER := ./out/
SRC_FILES := $(wildcard $(C_FOLDER)*.c)
OBJS := $(OBJ_FOLDER)server.o

app : $(OBJS)
	$(CC) $^ -o $@

$(OBJ_FOLDER)%.o : */%.c
	mkdir -p out/
	$(CC) $(CFLAGS) -c $< -o $@

run : app
	./app

clean :
	- rm app server.log
	- rm -rf ./out

.PHONY: run clean

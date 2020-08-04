all: program1 program2

program1: server.c
	gcc -Wall -o server server.c -pthread -lm

program2: client.c
	gcc -Wall -o client client.c -pthread -lm

clean: 
	  $(RM) server
	  $(RM) client
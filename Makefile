CC = g++

CFLAGS = -g -Wall -lpthread

all: client server

setup:
	mkdir client_folder server_folder

client: netutil.cpp netutil.h client.cpp client.h client_side.cpp
	$(CC) $(CFLAGS) -o $@ netutil.cpp client.cpp client_side.cpp

server: netutil.cpp netutil.h server.cpp server.h server_side.cpp
	$(CC) $(CFLAGS) -o $@ netutil.cpp server.cpp server_side.cpp

move:
	mv server server_folder/
	mv client client_folder/

clean:
	rm -rf client_folder server_folder

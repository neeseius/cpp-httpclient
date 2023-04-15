CC=g++
CFLAGS=-g -std=c++20 -Wall -Wextra -pedantic -lcrypto -lssl

client: main.cpp http/client.cpp http/connection.cpp
	$(CC) $(CFLAGS) $^ -o $@

# complete build
all: server client table kitchen

# make rule per il server
server: ./server/server.o
	gcc -Wall ./server/server.o -o ./server/server

# make rule per il client
client: client.o
	gcc -Wall client.o -o cli

# make rule per il kitchen device
kitchen: kitchen.o
	gcc -Wall kitchen.o -o kd

# make rule per il table device
table: table.o
	gcc -Wall table.o -o td

clean:
	rm *o ./server/*.o ./server/server cli td kd
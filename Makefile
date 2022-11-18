clean:
	rm server;
	rm client;



all:
	gcc -o client Client.c ./funzioniAusiliarie/** -pthread;
	gcc -o server Server.c ./funzioniAusiliarie/** -pthread;
	
all_print:
	gcc -o client Client.c ./funzioniAusiliarie/** -pthread -DPRINT
	gcc -o server Server.c ./funzioniAusiliarie/** -pthread -DPRINT 



client: Client.c ./funzioniAusiliarie/**
	gcc -o client Client.c ./funzioniAusiliarie/** -pthread
	
server: Server.c ./funzioniAusiliarie/**
	gcc -o server Server.c ./funzioniAusiliarie/** -pthread



client_print:
	gcc -o client Client.c ./funzioniAusiliarie/** -pthread -DPRINT

server_print: Server.c ./funzioniAusiliarie/**
	gcc -o server Server.c ./funzioniAusiliarie/** -pthread -DPRINT 

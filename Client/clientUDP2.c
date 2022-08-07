/* clientUDP.c - code for example client program that uses UDP */
/* compile with: gcc clientUDP1.c -DPRINT */


/* STRUTTURA DEL PROGRAMMA
main:
	_inizio_
	attendi che venga inserita dall'utente un comando
	crea un thread che gestisce la richiesta
	ritorna ad _inizio_	
thread:
	_inizio_
	controlla la richiesta
	invia un messaggio al server
	si assicura che la richiesta abbia terminato
	(indipendentemente dall'esito)
	termina il thread
*/


#include <sys/types.h> 
#include <sys/socket.h> 
#include <arpa/inet.h>
#include <unistd.h> 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "libreria.h"

#define	SERV_PORT	5193 
#define	MAXLINE		1024
#define	sizeOfRic	1024

/* GLOBAL VARIABLES shared between threads */
char *	indirizzoServ;			// indirizzo del server
/* _______________________________________ */

void * gestisciRichiesta(void* richiestaDaGestire);	// dichiarazione

int main(int argc, char *argv[ ])
{
  if(argc != 2){		// controlla numero degli argomenti
  	fprintf(stderr, "utilizzo: daytime_clientUDP <indirizzo IP server>\n");
  	exit(1);
  }
  
  indirizzoServ = argv[1];	// assegno l'argomento passato come indirizzo del server
  				// essendo una variabile globale, è accessibile a tutti i server
  pthread_t thread_id;		// dichiaro un intero che rappresenta l'ID del thread
  char	richiesta[sizeOfRic];	// dichiaro buffer testuale dove l'utente inserisce la richiesta
  int	retValue;		// dichiaro variabile che ospita il val di ritorno della pthread_create
  				// per poi fare controlli
  
  while(1){
  	//printf("inserisci una richiesta: ");		//chiedi all'utente una nuova richiesta
  	scanf("%s", richiesta);				//attendi che l'utente inserisca una richiesta
  	fflush(stdout);
  	fflush(stdin);
  	#ifdef PRINT
  	printf("richiesta inserita: %s \n", richiesta);	//notifica l'utente della richiesta
  	#endif
  	retValue = pthread_create(&thread_id, NULL, gestisciRichiesta, (void*)richiesta);
  	// *** ricordati di fare i controlli di thread create ***
  }
  exit(0);
}



void * gestisciRichiesta(void* richiestaDaGestire)
  {
  	char * ric = (char *)richiestaDaGestire;	// cast per gestire la richiesta
  	#ifdef PRINT
	printf("new thread - got %s \n", ric);		// stampa la richiesta
	#endif
	
	if(controllaRichiesta(ric) == 0){		// controlla che la richiesta effettuata dal client sia legittima
		printf("richiesta \"%s\" NON permessa \n", ric);
		pthread_exit(NULL);			// termina il thread
	}
	printf("invio richiesta %s \n", ric);
		
	
	int   sockfd, n, retValue;
	char  recvline[MAXLINE + 1];
	struct sockaddr_in	servaddr;
  
  	sockfd = socket(AF_INET, SOCK_DGRAM, 0);	// creo il socket
	if ((sockfd) < 0) {				// controllo errori
		perror("errore in socket");
		exit(1);
	}
	
	memset((void *)&servaddr, 0, sizeof(servaddr));	// azzera servaddr
	servaddr.sin_family = AF_INET;			// assegna il tipo di indirizzo
	servaddr.sin_port = htons(SERV_PORT);		// assegna la porta del server
	/* assegna l'indirizzo del server prendendolo dalla riga di comando.
	L'indirizzo è una stringa da convertire in intero secondo network byte order. */
	if (inet_pton(AF_INET, indirizzoServ, &servaddr.sin_addr) <= 0) {
		/* inet_pton (p=presentation) vale anche per indirizzi IPv6 */
		fprintf(stderr, "errore in inet_pton per %s", indirizzoServ);
		exit(1);
	}
	
	/* Invia al server il pacchetto di richiesta */
	if(scarta() == 0){
		retValue = sendto(sockfd, ric, sizeOfRic, 0, (struct sockaddr *) &servaddr, sizeof(servaddr));
		//ssize_t sendto(int sockfd, const void *buf, size_t len, int flags, const struct sockaddr *dest_addr, socklen_t addrlen);
		if (retValue < 0) {		//controlli sulla send
			perror("errore in sendto");
			exit(1);
		}
		
		/* Legge dal socket il pacchetto di risposta */
		n = recvfrom(sockfd, recvline, MAXLINE, 0 , NULL, NULL);
		if (n < 0) {
			perror("errore in recvfrom");
			exit(1);
		}
		if (n > 0) {
			recvline[n] = 0;	// aggiunge carattere di terminazione
			if (fputs(recvline, stdout) == EOF) {	// stampa recvline sullo stdout
				fprintf(stderr, "errore in fputs");
				exit(1);
			}
		}
	  }
	  else{
	  	//printf("pacchetto scartato \n"); //il client non lo sa, print usato per i test
	  }
	  pthread_exit(NULL);			// termina il thread
  }

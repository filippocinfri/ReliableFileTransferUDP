/* clientUDP.c - code for example client program that uses UDP */
/* compile with: gcc clientUDP1.c -DPRINT */


/* STRUTTURA DEL PROGRAMMA
main:
	while(1) {
	attendi che venga inserita dall'utente un comando
	crea un thread che gestisce la richiesta
	}
	
thread richiesta:
	divide la richiesta in pacchetti di 32B (per averne tanti fittiziamente)
	fa la selective repeat chiamando thread pacchetto

thread pacchetto:
	legge il pacchetto che ha ricevuto dal thread richiesta
	simula eventuale perdita
	invia il pacchetto
	attiva un timer
	se il timer è scaduto, e il thread non è stato terminato, ricomincia dall'inizio
	
main			->	richiesta(gestisci richiesta)

gestore richiesta	->	richiesta(gestore connessione)

gestore connessione	->	porta(gestore invii o gestore ack in base alla richiesta)


*/


#include <sys/types.h> 
#include <sys/socket.h> 
#include <arpa/inet.h>
#include <unistd.h> 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>
#include "libreria.h"


#define	SERV_PORT	5193 
#define	MAXLINE		1024
#define	sizeOfRic	1024
#define timeOutSec	12.2	// secondi per la scadenza del timer
#define windowSize	4	// dimensione della finestra del selective repeat


/* global variables shared between threads */
char *	indirizzoServ;				// indirizzo del server
pthread_mutex_t mutexScarta, mutexControlla;	// mutex per consent. accesso unico a scarta e controlla
struct arg_struct {				// argomento da passare al thread gestore pacchetto
  struct sockaddr_in addr;
  char buff[MAXLINE];
  int socketDescriptor;
};


void * gestisciRichiesta(void* richiestaDaGestire);	// dichiarazione
void * gestisciPacchetto(void* arg);			// dichiarazione


int main(int argc, char *argv[ ])
{
  if(argc != 2){		// controlla numero degli argomenti
  	fprintf(stderr, "utilizzo: daytime_clientUDP <indirizzo IP server>\n");
  	exit(1);
  }
  
  pthread_mutex_init(&mutexScarta, NULL);	// inizializzo il mutex per controllare accesso alla funzione scarta
  //*** ricorda i controlli sulla init ***
  pthread_mutex_init(&mutexControlla, NULL);	// inizializzo il mutex per controllare accesso alla funzione scarta
  //*** ricorda i controlli sulla init ***

  
  indirizzoServ = argv[1];	// assegno l'argomento passato come indirizzo del server
  				// essendo una variabile globale, è accessibile a tutti i server
  pthread_t thread_id;		// dichiaro un intero che rappresenta l'ID del thread
  char	richiesta[MAXLINE];	// dichiaro buffer testuale dove l'utente inserisce la richiesta
  char*	puntatoreRichiesta;	// ......
  int	retValue;		// dichiaro variabile che ospita il val di ritorno della pthread_create
  				// per poi fare controlli
  
  while(1){
  	scanf("%s", richiesta);			//attendi una richiesta dall'utente
  	
  	puntatoreRichiesta = (char *)malloc(MAXLINE);
  	if (puntatoreRichiesta == NULL) {	//errore nella malloc
  		fprintf(stderr, "main: errore nella malloc\n");
  		exit(1)
  	}
  	
  	memcpy(puntatoreRichiesta, richiesta, MAXLINE); //salvo la richiesta
  	
  	#ifdef PRINT
  	printf("PRINT: richiesta: \"%s\" inserita in:  %p \n", puntatoreRichiesta, puntatoreRichiesta);	//notifica l'utente della richiesta
  	#endif
  	
  	retValue = pthread_create(&thread_id, NULL, gestisciRichiesta, (void*)puntatoreRichiesta);
  	// *** ricordati di fare i controlli di thread create ***
  }
  exit(0);
}



void * gestisciRichiesta(void* richiestaDaGestire)
  {
  	char * ric = (char *)richiestaDaGestire;	// cast per gestire la richiesta
  	#ifdef PRINT
	printf("PRINT: thread richiesta - gestisco la richiesta: \"%s\" \n", ric);		// stampa la richiesta
	#endif
	
	pthread_mutex_lock(&mutexControlla);
	//***controlli lock
	int risultatoRichiesta = controllaRichiesta(ric);
	pthread_mutex_unlock(&mutexControlla);
	
	if(risultatoRichiesta == 0){		// controlla che la richiesta effettuata dal client sia legittima
		fprintf(stderr, "inserire: list / get_<file name> / put_<file name> \n");
		goto fine_richiesta;			// termina il thread
	}
	
	int   sockfd, n, retValue;
	char  recvline[MAXLINE + 1];
	struct sockaddr_in servaddr;
  
  	sockfd = socket(AF_INET, SOCK_DGRAM, 0);	// creo il socket
	if ((sockfd) < 0) {				// controllo errori
		perror("errore in socket");
		exit(1);
	}
	
	memset((void *)&servaddr, 0, sizeof(servaddr));	// azzera servaddr
	servaddr.sin_family = AF_INET;			// assegna il tipo di indirizzo
	servaddr.sin_port = htons(SERV_PORT);		// assegna la porta del server
	
	/* assegna l'indirizzo del server, preso dalla riga di comando.
	L'indirizzo è una stringa da convertire in intero secondo network byte order. */
	if (inet_pton(AF_INET, indirizzoServ, &servaddr.sin_addr) <= 0) {
		/* inet_pton (p=presentation) vale anche per indirizzi IPv6 */
		fprintf(stderr, "errore in inet_pton per %s", indirizzoServ);
		exit(1);
	}
	
	
	
	#ifdef PRINT
	printf("PRINT: attivo gestore connessione per richiesta: %s\n", ric);
	#endif
	
	//----------------GESTORE CONNESSIONE-----------------------//
	//invia comando
	pthread_t thread_id;		// mantengo gli ID del thread per cancellarlo
	
	/*raccolgo le informazioni per la gestione del pacchetto in una struttura */
	struct arg_struct args;
	args.addr = servaddr;
	args.socketDescriptor = sockfd;
	// ricorda di fare la malloc
	strncpy(args.buff,ric,MAXLINE);
	// ricorda di fare la malloc
	
	pthread_create(&thread_id, NULL, gestisciPacchetto, (void*)(&args));
	//attendi ack della richiesta (comune a tutti e 3 i comandi)
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
		#ifdef PRINT
		printf("PRINT: thread richiesta: \"%s\" risposta: \"%s\" \n", ric, recvline);
		#endif
		// *** ricordati controlli cancel***
		if(strncmp(recvline, "ack", 3) == 0){
			#ifdef PRINT
			printf("PRINT: ack ricevuto \n");
			#endif
			// invio conferma ack
			pthread_create(&thread_id, NULL, gestisciPacchetto, (void*)(&args));
		}
		//else?
	}
	pthread_cancel(thread_id);
	#ifdef PRINT
	printf("PRINT: thread cancellato \n");
	#endif
	
	
	
	if (risultatoRichiesta < 3) { //list e get_file
		// mettiti in ascolto
		// invia gli acks
		
		//gli ultimi 2 comandi tramite gestore ricezioni
		
		
		
		
		//pacchetti attesi
		int j;
		for(j=0; j<2; j++){
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
				#ifdef PRINT
				printf("PRINT: thread richiesta: \"%s\" risposta: \"%s\" \n", ric, recvline);
				#endif
				pthread_cancel(thread_id);
				// *** ricordati controlli cancel***
				#ifdef PRINT
				printf("PRINT: thread cancellato \n");
				#endif
			}
		}
		
		
		
		
		
	}
	else { //put_file
		
		//leggi dove scrivere
		// attiva gestore invii
	}
	
	goto fine_richiesta;
	/*
	int numPacch = 0;
	
	int indiceSend, inizioWindow;
	int vettoreAck[numPacch];
	int i;
	for(i=0; i<numPacch; i++){
		vettoreAck[i] = 0;
	}
	
	//gestore ack
	
	inizioWindow = 0;
	indiceSend = 0;
	while(inizioWindow < numPacch){
		#ifdef PRINT
		printf("PRINT: gest. rich. inizioWind: %d indiceSend %d \n", inizioWindow, indiceSend);
		#endif
		if(indiceSend < min(inizioWindow+windowSize, numPacch)){
			//attiva un gestore pacchetto
			indiceSend++;
		}
		if(vettoreAck[inizioWindow] == 1) {
			inizioWindow++;
		}		
	}
	*/
	pthread_create(&thread_id, NULL, gestisciPacchetto, (void*)(&args));
  	// *** ricordati di fare i controlli di thread create ***
  	printf("fine thread create \n"); //test
	
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
		#ifdef PRINT
		printf("PRINT: thread richiesta: \"%s\" risposta: \"%s\" \n", ric, recvline);
		#endif
		pthread_cancel(thread_id);
		// *** ricordati controlli cancel***
		#ifdef PRINT
		printf("PRINT: thread cancellato \n");
		#endif
	}
	
	fine_richiesta:
	free(richiestaDaGestire);		// libero lo spazio che prima avevo allocato per la richiesta
	pthread_exit(NULL);			// termina il thread
  }
  

void * gestisciPacchetto(void * arg)
{
  printf("thread gestore pacchetto \n");
  
  struct arg_struct* args = (struct arg_struct*)arg;
  struct sockaddr_in servaddr = args->addr;
  int	sockfd = args->socketDescriptor;
  char	ric[MAXLINE];
  int	retValue;
  strncpy(ric,args->buff,MAXLINE);
  
  invio:
  /* Invia al server il pacchetto di richiesta */
  pthread_mutex_lock(&mutexScarta);
  int decisione = scarta();
  pthread_mutex_unlock(&mutexScarta);
  if(decisione == 0) {	// se il pacchetto non va scartato restituisce 0
  	retValue = sendto(sockfd, ric, MAXLINE, 0, (struct sockaddr *) &servaddr, sizeof(servaddr));
  	if (retValue < 0) {		// controlli sulla send
  		perror("errore in sendto");
  		exit(1);
  	}
  }
  else {
  	#ifdef PRINT
  	printf("PRINT: pacchetto scartato \n"); // il client non usa questa informazione
  	#endif
  }
  
  sleep(timeOutSec);
  printf("thread gestore pacchetto: timer scaduto\n");
  goto invio;				// se il timer è scaduto, il thread riprova ad inviare
  					// l'unico modo di uscire è che il thread richiesta cancelli
  					// questo thread che gestisce il singolo pacchetto
}

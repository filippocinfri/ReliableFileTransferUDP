/* clientUDP.c - code for example client program that uses UDP */
/* compile with: gcc clientUDP1.c -DPRINT */


/* STRUTTURA DEL PROGRAMMA
main:
	_inizio_
	attendi che venga inserita dall'utente un comando
	crea un thread che gestisce la richiesta
	ritorna ad _inizio_	
thread richiesta:
	*****

thread pacchetto:
/*	legge il pacchetto che ha ricevuto dal thread richiesta
	simula eventuale perdita
	invia il pacchetto
	attiva un timer
	se il timer è scaduto, e il thread non è stato terminato, ricomincia dall'inizio
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
#define timeOutSec	5.2	// secondi per la scadenza del timer
#define windowSize	4	// dimensione della finestra del selective repeat


/* GLOBAL VARIABLES shared between threads */
char *	indirizzoServ;		// indirizzo del server

struct arg_struct {		// argomento da passare al thread gestore pacchetto
  struct sockaddr_in addr;
  char buff[MAXLINE];
  int socketDescriptor;
};

pthread_mutex_t mutexScarta, mutexControlla;
/* _______________________________________ */


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
  	//printf("inserisci una richiesta: ");		//chiedi all'utente una nuova richiesta
  	scanf("%s", richiesta);				//attendi che l'utente inserisca una richiesta
  	puntatoreRichiesta = (char *)malloc(MAXLINE);
  	memcpy(puntatoreRichiesta, richiesta, MAXLINE);
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
	printf("PRINT: thread richiesta - gestisco \"%s\" \n", ric);		// stampa la richiesta
	#endif
	
	pthread_mutex_lock(&mutexControlla);
	//***controlli lock
	if(controllaRichiesta(ric) == 0){		// controlla che la richiesta effettuata dal client sia legittima
		fprintf(stderr, "inserire: list / get_<file name> / put_<file name> \n");
		pthread_mutex_unlock(&mutexControlla);
		goto fine_richiesta;			// termina il thread
	}
	pthread_mutex_unlock(&mutexControlla);
	printf("invio richiesta \"%s\" al server \n", ric);
		
	
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
	
	/*raccolgo le informazioni per la gestione del pacchetto in una struttura */
	struct arg_struct args;
	args.addr = servaddr;
	args.socketDescriptor = sockfd;
	strncpy(args.buff,ric,MAXLINE);
	// ricorda di fare la malloc
	
	printf("creo thread che gestisce timer pacchetto\n"); //test
	pthread_t thread_id;		// mantengo l'ID del thread per cancellarlo
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
		pthread_cancel(thread_id);
		printf("thread cancellato \n");
		// *** ricordati controlli cancel***
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

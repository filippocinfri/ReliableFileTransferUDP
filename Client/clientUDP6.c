/* STRUTTURA DEL PROGRAMMA

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


#define	SERV_PORT		5193
#define SHORTPAY		128
#define LONGPAY			1024
#define HEADER_DIM		7
#define	PACK_LEN		LONGPAY+HEADER_DIM
#define SHORT_PACK_LEN	SHORTPAY+HEADER_DIM
#define timeOutSec		9.2	// secondi per il timeout del timer
#define windowSize		4		// dimensione della finestra del selective repeat


/* global variables shared between threads */
char *	indirizzoServ;				// indirizzo del server
pthread_mutex_t mutexScarta, mutexControlla;	// mutex per consent. accesso unico a scarta e controlla
struct arg_struct {				// struttura da passare come argomento tra i thread
  struct	sockaddr_in addr;
  char *	buff;				//pacchetto
  int		socketDescriptor;
};


// dichiarazioni dei gestori
void * gestisciRichiesta(void* richiestaDaGestire);	
void * gestisciPacchetto(void* arg);


int main(int argc, char *argv[ ])
{
  if(argc != 2){		// controlla numero degli argomenti
  	fprintf(stderr, "utilizzo: daytime_clientUDP <indirizzo IP server>\n");
  	exit(1);
  }
  
  // inizializzo i mutex per controllare accessi alle funzioni ausiliari
  if (pthread_mutex_init(&mutexScarta, NULL) != 0) {
		fprintf(stderr, "main: errore nella mutex_init\n");
  		exit(1);
  }
  if (pthread_mutex_init(&mutexControlla, NULL) != 0) {
		fprintf(stderr, "main: errore nella mutex_init\n");
  		exit(1);
  }
  
  
  indirizzoServ = argv[1];	// var. globale, indirizzo del server accessibile a tutti i server
  pthread_t thread_id;		// ID del thread
  char	richiesta[SHORTPAY]; //dichiaro buffer dove viene inserita la richiesta
  char*	puntatoreRichiesta;	// ......
  int	retValue;		// dichiaro variabile che ospita il val di ritorno della pthread_create
  						// per poi fare controlli
  
  // attiva gestori per ogni richiesta in input
  while(1){
	// inserire una richiesta
  	scanf("%s", richiesta);


	// salvo la richiesta
  	puntatoreRichiesta = (char *)calloc(SHORTPAY, 1);
	if (puntatoreRichiesta == NULL) {	//errore nella calloc
  		fprintf(stderr, "main: errore nella malloc\n");
  		exit(1);
  	}
  	memcpy(puntatoreRichiesta, richiesta, SHORTPAY);
  	#ifdef PRINT
  	printf("PRINT: richiesta: \"%s\" inserita in:  %p \n", puntatoreRichiesta, puntatoreRichiesta);	//notifica l'utente della richiesta
  	#endif
  	
	// chiamo il gestore della richiesta
  	retValue = pthread_create(&thread_id, NULL, gestisciRichiesta, (void*)puntatoreRichiesta);
  	if(retValue != 0) {
		fprintf(stderr, "main: errore nella pthread_create\n");
  		exit(1);
  	}
  }
}



void * gestisciRichiesta(void* richiestaDaGestire)
  {
  	char * ric = (char *)richiestaDaGestire;
	#ifdef PRINT
	printf("PRINT: thread richiesta - gestisco la richiesta: \"%s\" \n", ric);		// stampa la richiesta
	#endif
	
	
	// Controlla la correttezza della richiesta inserita
	if (pthread_mutex_lock(&mutexControlla)!=0) {	//per evitare accessi sovrapposti
		fprintf(stderr, "gestore richiesta: errore nella mutex_lock\n");
  		exit(1);
	}
	int risultatoRichiesta = controllaRichiesta(ric);
	if (pthread_mutex_unlock(&mutexControlla)!=0) {
		fprintf(stderr, "gestore richiesta: errore nella mutex_unlock\n");
  		exit(1);
	}
	if(risultatoRichiesta == 0){
		fprintf(stderr, "inserire: list / get_<file name> / put_<file name> \n");
		goto fine_richiesta;	// il thread deve terminare
	}

	// Creo il pacchetto
	unsigned short lunghezzaPayload = strlen(ric)-4;
	printf("%hu %d \n", lunghezzaPayload, lunghezzaPayload);
	char * head = malloc(SHORT_PACK_LEN);
	memset((void *)head, 0, SHORT_PACK_LEN);		// head
	memset((void *)head, setBit(head[0], 6), 1);	// head
	head[1] = lunghezzaPayload;
	strcat(head+HEADER_DIM, ric);
	
	//#ifdef PRINT
	//printf("PRINT: gestisci richiesta - payload packet: %s \n", head+HEADER_DIM);
	//printf("PRINT: gestisci richiesta - header: %c \n", head[0]);
	//#endif


	int   sockfd, n, retValue;
	char  recvlineShort[SHORT_PACK_LEN];
	char  recvlineLong[PACK_LEN];
	struct sockaddr_in servaddr;


	// creo il socket
  	sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	if ((sockfd) < 0) {			// controllo errori
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
	

	//----------------GESTORE CONNESSIONE-----------------------//
	pthread_t thread_id;	// mantengo ID del thread per poterlo cancellare
	/*raccolgo le informazioni per la gestione del pacchetto in una struttura */
	struct arg_struct args;
	args.addr = servaddr;
	args.socketDescriptor = sockfd;
	//strncpy(args.buff,head,SHORT_PACK_LEN); //¿serve la malloc??
	args.buff = head;

	//#ifdef PRINT
	//printf("PRINT: gestisci richiesta - payload packet: %s \n", args.buff+HEADER_DIM);
	//printf("PRINT: gestisci richiesta - header: %c \n", args.buff[0]);
	//#endif

	// invio richiesta
	if(pthread_create(&thread_id, NULL, gestisciPacchetto, (void*)(&args)) != 0) {
		fprintf(stderr, "main: errore nella pthread_create\n");
  		exit(1);
  	}
	// attendi ack
	n = recvfrom(sockfd, recvlineShort, SHORT_PACK_LEN, 0, NULL, NULL);
	if (n < 0) {
		perror("errore in recvfrom");
		exit(1);
	}
	if (n > 0) {
		char head = recvlineShort[0];
		if(readBit(head, 7) == 0 & readBit(head, 6) == 1){
			#ifdef PRINT
			printf("PRINT: thread richiesta: \"%s\" risposta: get \n", ric);
			#endif
			pthread_cancel(thread_id);
			// *** ricordati controlli cancel***
			#ifdef PRINT
			printf("PRINT: thread cancellato \n");
			#endif
		}
		else{
			//ho ricevuto una risposta != ack, situazione inattesa
			#ifdef PRINT
			printf("PRINT: thread richiesta: \"%s\" risposta: NON ack \n", ric);
			#endif
		}
	}
	
	// libero lo spazio del messaggio precedente
	free(head);

	// Creo il pacchetto di ack
	lunghezzaPayload = 0;
	printf("%hu %d \n", lunghezzaPayload, lunghezzaPayload);
	char * ackPacket = malloc(HEADER_DIM);
	memset((void *)ackPacket, 0, HEADER_DIM);		// head
	memset((void *)ackPacket, setBit(ackPacket[0], 6), 1);	// head
	memset((void *)ackPacket, setBit(ackPacket[0], 7), 1);	// head
	
	//#ifdef PRINT
	//printf("PRINT: gestisci richiesta - payload packet: %s \n", head+HEADER_DIM);
	//printf("PRINT: gestisci richiesta - header: %c \n", head[0]);
	//#endif

	args.buff = ackPacket;
	
	// invio ack
	if(pthread_create(&thread_id, NULL, gestisciPacchetto, (void*)(&args)) != 0) {
		fprintf(stderr, "main: errore nella pthread_create\n");
  		exit(1);
  	}
	sleep(10); //poi va modificato, 	attenderà messaggi successivi
	pthread_cancel(thread_id);

	goto fine_richiesta;


    printf("Ricevuto ack da IP: %s e porta: %i\n",inet_ntoa(servaddr.sin_addr),ntohs(servaddr.sin_port));
    

	/*
	// in base al comando
	if (risultatoRichiesta < 3) { //list e get_file
		// mettiti in ascolto
		// invia gli acks
		
		while(1){
			n = recvfrom(sockfd, recvlineShort, PACK_LEN, 0, NULL, NULL);
			if (n < 0) {
				perror("errore in recvfrom");
				exit(1);
			}
			if (n > 0) {
			recvline[n] = 0;	// aggiunge carattere di terminazione
			if(strncmp(recvline, "stop", 4) == 0) {
				#ifdef PRINT
				printf("PRINT: exit \n");
				#endif
				break;
			}
			else{
				//ho ricevuto un ulteriore pacchetto
				#ifdef PRINT
				printf("PRINT: thread richiesta: \"%s\" risposta: \"%s\" \n", ric, recvline);
				#endif
			}
		}
	}	
		
		
		
		
		
	}
	else { //put_file
		
		//leggi dove scrivere
		// attiva gestore invii
	}
	*/
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
	
	fine_richiesta:
	close(sockfd);				// chiudiamo il descrittore della socket
	free(richiestaDaGestire);	// libero lo spazio che prima avevo allocato per la richiesta
	pthread_exit(NULL);			// termina il thread
  }
  

void * gestisciPacchetto(void * arg)
{
  printf("thread gestore pacchetto \n");
  
  struct arg_struct* args = (struct arg_struct*)arg;
  struct sockaddr_in servaddr = args->addr;
  int	sockfd = args->socketDescriptor;
  char*	pack;
  int	retValue;
  
  pack = args->buff;
	
	//#ifdef PRINT
	//printf("PRINT: gestore pack - payload packet: %s \n", pack+HEADER_DIM);
	//printf("PRINT: gestore pack - header: %c \n", pack[0]);
	//#endif
  
  invio:
  /* Invia al server il pacchetto di richiesta */
  pthread_mutex_lock(&mutexScarta);
  int decisione = scarta();
  pthread_mutex_unlock(&mutexScarta);
  if(decisione == 0) {	// se il pacchetto non va scartato restituisce 0
  	retValue = sendto(sockfd, pack, SHORT_PACK_LEN,  0, (struct sockaddr *) &servaddr, sizeof(servaddr));
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
  goto invio;				// se il timer � scaduto, il thread riprova ad inviare
  					// l'unico modo di uscire � che il thread richiesta cancelli
  					// questo thread che gestisce il singolo pacchetto
}

/*
void * gestoreConnessione(void * arg)
{
  printf("thread gestore connessione attivo \n");
  pthread_t thread_id_GestPack;	// mantengo ID del gest. pacchetto per poterlo cancellare
  struct arg_struct* args = (struct arg_struct*)arg;
  struct sockaddr_in servaddr = args->addr;
  int	sockfd = args->socketDescriptor;
  char	ric[PACK_LEN];
  char  recvlineShort[SHORT_PACK_LEN];
  int	retValue;
  strncpy(ric,args->buff, PACK_LEN);
  
  invio:
  
  if(pthread_create(&thread_id_GestPack, NULL, gestisciPacchetto, (void*)(args)) != 0) {
	fprintf(stderr, "main: errore nella pthread_create\n");
  	exit(1);
  }
  //attendi ack della richiesta
  // Legge dal socket il pacchetto di risposta
  int n = recvfrom(sockfd, recvlineShort, PACK_LEN, 0, NULL, NULL);
  pthread_cancel(thread_id_GestPack); // chiudo il gestore del pacchetto
  if (n < 0) {
	perror("errore in recvfrom Gest Conn");
	exit(1);
  }
  if (n > 0) {
	recvlineShort[n] = 0;	// aggiunge carattere di terminazione
	#ifdef PRINT
	printf("PRINT: thread richiesta: \"%s\" risposta: \"%s\" \n", ric, recvline);
	#endif
	if(strncmp(recvlineShort, "ack", 3) == 0){
		#ifdef PRINT
		printf("PRINT: ack ricevuto \n");
		#endif
	}
	else {
		#ifdef PRINT
		printf("PRINT: richiesta fallita \n");
		#endif
	}
  }
  pthread_exit(0);
}
*/
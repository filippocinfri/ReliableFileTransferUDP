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
#define SHORT_PAY		128
#define LONG_PAY		1024
#define HEADER_DIM		7
#define	PACK_LEN		HEADER_DIM + LONG_PAY
#define SHORT_PACK_LEN	HEADER_DIM + SHORT_PAY
#define timeOutSec		9.2		// secondi per il timeout del timer
#define WAIT_FACTOR		4		// massima attesa = WAIT_FACTOR * timeOutSec
#define WINDOW_SIZE		5		// dimensione della finestra


// global variables shared between threads
char *	indirizzoServ;			// indirizzo del server
pthread_mutex_t mutexScarta, mutexControlla, mutexHeaderPrint, mutexBitManipulation;	// mutex per consent. accesso unico a scarta e controlla
struct arg_struct {				// struttura da passare come argomento tra i thread
  struct	sockaddr_in addr;
  char *	buff;				//puntatore a pacchetto
  int		socketDescriptor;
};

struct arg_attesa {				// struttura da passare come argomento tra i thread
  char *	richiesta;			// puntatore alla richiesta
  pthread_t	threadChiamante;	// quello da chiudere, per aprirne un altro
};


// dichiarazioni dei gestori
void * gestisciRichiesta(void* richiestaDaGestire);	
void * gestisciPacchetto(void* arg);
void * gestisciAttesa(struct arg_attesa * arg); 
void * listRequest(void * arg);
void * getRequest(void * arg);
void * putRequest(void * arg);  


int main(int argc, char *argv[ ]) {
  // controlla numero degli argomenti
  if(argc != 2){
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
  if (pthread_mutex_init(&mutexHeaderPrint, NULL) != 0) {
		fprintf(stderr, "main: errore nella mutex_init\n");
  		exit(1);
  }
  if (pthread_mutex_init(&mutexBitManipulation, NULL) != 0) {
		fprintf(stderr, "main: errore nella mutex_init\n");
  		exit(1);
  }
  
  indirizzoServ = argv[1];	// var. globale, indirizzo del server accessibile a tutti i server
  pthread_t thread_id;		// ID del thread
  char*	puntatoreRichiesta;	// ......

  // attiva gestori per ogni richiesta in input
  while(1){
	// salvo spazio per la richiesta
  	puntatoreRichiesta = (char *)calloc(1,SHORT_PAY);
	if (puntatoreRichiesta == NULL) {	//errore nella calloc
  		fprintf(stderr, "main: errore nella malloc\n");
  		exit(1);
  	}

	// attendi una richiesta
  	scanf("%s", puntatoreRichiesta);	// assumo che sia < 128 caratteri
	#ifdef PRINT
	printf("PRINT: richiesta: \" %s \" inserita in:  %p \n", puntatoreRichiesta, puntatoreRichiesta);
	#endif

	// attivo il gestore della richiesta
  	if(pthread_create(&thread_id, NULL, gestisciRichiesta, (void*)puntatoreRichiesta) != 0) {
		fprintf(stderr, "main: errore nella pthread_create\n");
  		exit(1);
  	}
  }
}



void * gestisciRichiesta(void* richiestaDaGestire) {
	char * richiesta = (char *)richiestaDaGestire;

	// Controlla la correttezza della richiesta inserita
	if (pthread_mutex_lock(&mutexControlla)!=0) {	//per evitare accessi sovrapposti
		fprintf(stderr, "gestore richiesta: errore nella mutex_lock\n");
  		exit(1);
	}
	int risultatoRichiesta = controllaRichiesta(richiesta);
	if (pthread_mutex_unlock(&mutexControlla)!=0) {
		fprintf(stderr, "gestore richiesta: errore nella mutex_unlock\n");
  		exit(1);
	}
	if(risultatoRichiesta == 0){
		fprintf(stderr, "inserire: list / get_<file name> / put_<file name> \n");
		goto fine;	// il thread deve terminare
	}

	// Creo il pacchetto
	char * requestPacket = calloc(SHORT_PACK_LEN,1);
	// list lascio header vuoto
	if (pthread_mutex_lock(&mutexBitManipulation)!=0) {	//per evitare accessi sovrapposti
		fprintf(stderr, "gestore richiesta: errore nella mutex_lock\n");
  		exit(1);
	}
	if(risultatoRichiesta == 2){			// get
		setBit(requestPacket[0], 6);		// richiesta get 01
	}
	if(risultatoRichiesta == 3){			// put
		setBit(requestPacket[0], 7);		// richiesta put 10
	}
	if (pthread_mutex_unlock(&mutexBitManipulation)!=0) {
		fprintf(stderr, "gestore richiesta: errore nella mutex_unlock\n");
  		exit(1);
	}
	(*(unsigned short*)(&requestPacket[1])) = strlen(richiesta)-4;	// inserisco lunghezza payload
	strcat(requestPacket+HEADER_DIM, richiesta+4);					// appendo la richiesta dopo l'header

	#ifdef PRINT
	//stampo l'header
	if (pthread_mutex_lock(&mutexHeaderPrint)!=0) {	//per evitare accessi sovrapposti
		fprintf(stderr, "gestore richiesta: errore nella mutex_lock\n");
  		exit(1);
	}
	headerPrint(requestPacket);
	if (pthread_mutex_unlock(&mutexHeaderPrint)!=0) {
		fprintf(stderr, "gestore richiesta: errore nella mutex_unlock\n");
  		exit(1);
	}
	#endif

	int   sockfd, n;
	char  recvline[HEADER_DIM];
	struct sockaddr_in servaddr;

	// creo il socket
  	sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	if ((sockfd) < 0) {	// controllo eventuali errori
		perror("errore in socket");
		exit(1);
	}
	memset((void *)&servaddr, 0, sizeof(servaddr));	// azzera servaddr
	servaddr.sin_family = AF_INET;					// assegna il tipo di indirizzo
	servaddr.sin_port = htons(SERV_PORT);			// assegna la porta del server
	// assegna l'indirizzo del server, preso dalla riga di comando.
	// L'indirizzo è una stringa da convertire in intero secondo network byte order.
	if (inet_pton(AF_INET, indirizzoServ, &servaddr.sin_addr) <= 0) {
		/* inet_pton (p=presentation) vale anche per indirizzi IPv6 */
		fprintf(stderr, "errore in inet_pton per %s", indirizzoServ);
		exit(1);
	}
	

	//---------------- GESTORE HANDSHAKE -----------------------
	pthread_t thread_id, thread_id_attesa;  // mantengo ID del thread creato per poterlo cancellare
	//raccolgo le informazioni per la gestione del pacchetto in una struttura
	struct arg_struct args;
	socklen_t lenadd = sizeof(struct sockaddr_in);
	args.addr = servaddr;
	args.socketDescriptor = sockfd;
	args.buff = requestPacket;
	int result;
	
	// invio richiesta
	if(pthread_create(&thread_id, NULL, gestisciPacchetto, (void*)(&args)) != 0) {
		fprintf(stderr, "main: errore nella pthread_create\n");
		exit(1);
    }

	// inizio attesa
	struct arg_attesa argomenti;
	argomenti.richiesta = richiesta;
	argomenti.threadChiamante = pthread_self();
	if(pthread_create(&thread_id_attesa, NULL, gestisciAttesa, &argomenti) != 0) {
		fprintf(stderr, "main: errore nella pthread_create\n");
		exit(1);
    }
	
	// attendi ack //Ricordati che ack è solo pacchetto
	n = recvfrom(sockfd, recvline, HEADER_DIM, 0,  (struct sockaddr*)&args.addr, &lenadd);
	if (n < 0) {
		perror("errore in recvfrom");
		exit(1);
	}
	if (n > 0) {
		// qualcosa ho ricevuto, sia ack che nack, devo smettere di inviare il packet
		if(pthread_cancel(thread_id) != 0) {
			fprintf(stderr, "main: errore nella pthread_cancel\n");
			exit(1);
		}
		// controllo se ho ricevuto ack, ack => primo byte risposta == primo byte header
		if (pthread_mutex_lock(&mutexBitManipulation)!=0) {	//per evitare accessi sovrapposti
			fprintf(stderr, "gestore richiesta: errore nella mutex_lock\n");
  			exit(1);
		}
		result = readBit(recvline[0], 7) == readBit(requestPacket[0], 7) & readBit(recvline[0], 6) == readBit(requestPacket[0], 6);
		if (pthread_mutex_unlock(&mutexBitManipulation)!=0) {	//per evitare accessi sovrapposti
			fprintf(stderr, "gestore richiesta: errore nella mutex_lock\n");
  			exit(1);
		}
		if(result == 0){ // ho ricevuto una risposta nack
			#ifdef PRINT
			printf("PRINT: thread richiesta: \"%s\" risposta: NON ack \n", richiesta);
			#endif
			goto fine_richiesta;
		}
	}
	#ifdef PRINT
	printf("Ricevuto ack da IP: %s e porta: %i\n",inet_ntoa(args.addr.sin_addr),ntohs(args.addr.sin_port));
	#endif
	
	// creo il pacchetto di ack
	char * ackPacket = calloc(HEADER_DIM,1);
	if (pthread_mutex_lock(&mutexBitManipulation)!=0) {	//per evitare accessi sovrapposti
		fprintf(stderr, "gestore richiesta: errore nella mutex_lock\n");
  		exit(1);
	}
	setBit(ackPacket[0], 6);
	setBit(ackPacket[0], 7);		// 11000000
	if (pthread_mutex_unlock(&mutexBitManipulation)!=0) {	//per evitare accessi sovrapposti
		fprintf(stderr, "gestore richiesta: errore nella mutex_unlock\n");
  		exit(1);
	}
	// invio il pacchetto
  	if (sendto(sockfd, ackPacket, HEADER_DIM,  0, (struct sockaddr *) &servaddr, sizeof(servaddr)) < 0) {		// controlli sulla send
  		fprintf(stderr, "gestore richiesta: errore sendto\n");
  		exit(1);
  	}

	// a seconda della richiesta il corrispondente thread prende il controllo
	if (risultatoRichiesta == 1) { // list
		if(pthread_create(&thread_id, NULL, listRequest, (void*)(&args)) != 0) {
			fprintf(stderr, "gestore richiesta: errore nella pthread_create\n");
			exit(1);
		}
	}
	else {
		if (risultatoRichiesta == 2) { // get
			if(pthread_create(&thread_id, NULL, getRequest, (void*)(&args)) != 0) {
				fprintf(stderr, "main: errore nella pthread_create\n");
				exit(1);
			}
		}
		else {
			if(pthread_create(&thread_id, NULL, putRequest, (void*)(&args)) != 0) {
				fprintf(stderr, "main: errore nella pthread_create\n");
				exit(1);
			}
		}
	}

	fine_richiesta:
	free(requestPacket);		// libero lo spazio del messaggio di richiesta
	fine:
	free(richiestaDaGestire);	// libero lo spazio allocato dal main per la richiesta
	pthread_exit(NULL);			// termina il thread
  }


void * gestisciAttesa(struct arg_attesa * arg) {
	char * richiesta = (char *)malloc(SHORT_PACK_LEN);
	richiesta = arg->richiesta;
	pthread_t thread_id_chiamante = arg->threadChiamante;
	pthread_t dummy;
	
	printf("thread gestisci attesa, dormo \n");
	sleep(WAIT_FACTOR * timeOutSec);
	printf("thread gestisci attesa, svegliato \n");

	if(pthread_cancel(thread_id_chiamante) != 0) {
		fprintf(stderr, "gestisciAttesa: errore nella pthread_cancel\n");
		exit(1);
	}
	if(pthread_create(dummy, NULL, listRequest, (void*)richiesta) != 0) {
		fprintf(stderr, "gestisciAttesa: errore nella pthread_create\n");
		exit(1);
	}
	pthread_exit(NULL);
}


void * getRequest(void * arg) {
	int n;
	int inizio = 0;
	unsigned long primoNumAtteso = 0;
	printf("primo atteso %lu \n", primoNumAtteso);
	unsigned long sequenceNumber, indice;
	char * recvdPack = calloc(PACK_LEN, 1);

	
	// SELECTIVE REPEAT RECV
	// creo il buffer in cui metto i pacchetti, e quello in cui scrivo quali spazi sono liberi
	char * buffer = calloc(WINDOW_SIZE, PACK_LEN);
	char * bufferFill = calloc(WINDOW_SIZE, 1);
	// creo il pacchetto con cui ackare
	char * ackPacket = calloc(HEADER_DIM, 1);
	if (pthread_mutex_lock(&mutexBitManipulation)!=0) {	//per evitare accessi sovrapposti
		fprintf(stderr, "gestore richiesta: errore nella mutex_lock\n");
  		exit(1);
	}
	setBit(ackPacket[0], 6);
	setBit(ackPacket[0], 7);	// 11
	if (pthread_mutex_unlock(&mutexBitManipulation)!=0) {	//per evitare accessi sovrapposti
		fprintf(stderr, "gestore richiesta: errore nella mutex_lock\n");
  		exit(1);
	}

	while(1){
		#ifdef PRINT
		printf("PRINT: gest. rich. inizio: %d \n", inizio);
		#endif
		
		// ricevo un pacchetto
		n = recvfrom(sockfd, recvdPack, PACK_LEN, 0, (struct sockaddr*)&args.addr, &lenadd);
		if (n < 0) {
			perror("errore in recvfrom");
			exit(1);
		}
		if (n > 0) { // se ho ricevuto qualcosa
			sequenceNumber = (*(unsigned long*)(&recvdPack[3]));
			indice = sequenceNumber % WINDOW_SIZE;
			// seqNum non può essere qualcosa fuori dal range perché client/servrr hanno stessa window size
			if (sequenceNumber > inizio){ // se è qualcosa che non ho ancora riscontrato
				if(bufferFill[indice] != "1"){ // se non mi era già arrivato
					buffer[indice] =  recvdPack[HEADER_DIM]; // metto nel buffer alla posizione indice
					bufferFill[indice] = "1"; // aggiorno bufferFill
				}
			}
			// imposto il pacchetto di ack
			(*(unsigned long*)(&ackPacket[3])) = sequenceNumber;
			// invia ack del pack ricevuto tramite gestisci pacchetto 
			if (sendto(sockfd, ackPacket, HEADER_DIM,  0, (struct sockaddr *) &servaddr, sizeof(servaddr)) < 0) {		// controlli sulla send
				perror("errore in sendto");
				exit(1);
			}
		}
		ciclo: // per ogni packet che posso copiare
		if(bufferFill[inizio%WINDOW_SIZE] == "1"){
			printf("%c \n", recvdPack[HEADER_DIM]); // copio il payload nel file
			if(readBit(recvdPack[0], 5) == 1){ // verifico se è l'ultimo pack
				goto ultimo_pack_recvd;// esci e vai a chiusura di connessione
			}
			else{
				bufferFill[inizio%WINDOW_SIZE] = "0";// aggiorno bufferFill
				inizio += 1;// aggiorno inizio
				goto ciclo;
			}
		}
	}
	ultimo_pack_recvd:
	printf("letto ultimo packet \n");
	free(ackPacket); // libero le aree di memoria occupate
	free(buffer);
	free(bufferFill);
	*/
	printf("fine get \n");
}


void * listRequest(void * arg) {
	struct arg_struct	args = *(struct arg_struct *)arg;
	struct	sockaddr_in	servaddr = args.addr;
	int 		sockfd = args.socketDescriptor;
	char *		packet = args.buff;
	socklen_t 	lenadd = sizeof(struct sockaddr_in);
	char		recvlineLong[PACK_LEN];

	int n;
	pthread_t thread_id[100];

	// Creo il pacchetto di ack
	char * ackPacket = malloc(HEADER_DIM);
	memset((void *)ackPacket, 0, HEADER_DIM);				// ack packet head
	memset((void *)ackPacket, setBit(ackPacket[0], 6), 1);	// voglio scrivere 11000000
	memset((void *)ackPacket, setBit(ackPacket[0], 7), 1);	// voglio scrivere 11000000

	args.buff = ackPacket;

	do{
		// invio ack
		if(pthread_create(&thread_id, NULL, gestisciPacchetto, (void*)(&args)) != 0) {
			fprintf(stderr, "main: errore nella pthread_create\n");
			exit(1);
		}
		n = recvfrom(sockfd, recvlineLong, PACK_LEN, 0,  (struct sockaddr*)&args.addr, &lenadd);
		if (n < 0) {
			perror("errore in recvfrom");
			exit(1);
		}
		if (n > 0) {
			// controllo se ho ricevuto ack, ack => primo byte risposta == primo byte header
			if(readBit(recvlineLong[0], 7) == 0 & readBit(recvlineLong[0], 6) == 0){
				pthread_cancel(thread_id);
				// *** ricordati controlli cancel***
			}
			else{
				//ho ricevuto una risposta != ack, situazione inattesa
			}
		}
	} while(readBit(recvlineLong[0], 5) == 1);

	free(ackPacket); // alla fine
}


void * gestisciPacchetto(void * arg) {
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

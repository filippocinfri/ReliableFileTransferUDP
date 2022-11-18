#include <arpa/inet.h>
#include <sys/types.h> 
#include <sys/socket.h> 
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h> // per misurare il tempo trascorso

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "libreria.h"

#define	SERV_PORT		5193

#define HEADER_DIM		7
#define SHORT_PAY		128
#define LONG_PAY		2048
#define	PACKET_LEN		HEADER_DIM + LONG_PAY
#define SHORT_PACK_LEN	HEADER_DIM + SHORT_PAY

#define WAIT_FACTOR		4		// massima attesa = WAIT_FACTOR * timeOutSec
#define WINDOWSIZE		5		// dimensione della finestra

// Variabili globali condivise tra thread  
char dir_file[] = "./Files_Client/"; // Directory della cartella contenente tutti i Files presenti sul client

// mutex per consent. accesso unico a scarta e controlla
pthread_mutex_t mutexScarta, mutexControlla, mutexHeaderPrint, mutexBitManipulation, mutexBuffFill, mutexAck, mutexRcv_base;	// mutex per consent. accesso unico

struct timeval tv1, tv2;        // variabili per misurare il trascorrere del tempo

char* indirizzoServ;			// indirizzo del server

// Variabili per il timer adattivo
int time_milli;	//ABCD
int timeOutSec = 3200;
int TIMEOUT_ACK = 2000;
int TIMEOUT_CLOSE = 3000;
int bonus = 20;
int malus = -30;

struct arg_struct {           	// Struttura per passare gli argomenti ai thread principali per la gestione della richiesta del client
	struct    sockaddr_in addr; 
	char      *buff;            // Buffer dove viene salvata la richiesta del client 
	int	    socketDescriptor;
	pthread_t	thread_id_attesa;
};

struct arg_attesa {				// struttura da passare come argomento tra i thread
	char* richiesta;			// puntatore alla richiesta
	pthread_t	threadChiamante;// quello da chiudere, per aprirne un altro
};

struct arg_write_print {	// struttura da passare come argomento al thread: writeOnFile
	int			fileDescriptor;
	pthread_t	threadChiamante;
	char*		payloads_vector[WINDOWSIZE];
	char*		payloads_fill_vector;
	unsigned* 	rcv_base;
};

struct arg_read_write {		// Struttura da passare come argomento al thread: writeN e readN
	int		fileDescriptor;
	void*	buff;
	size_t	nBytes;
};

struct arg_ackRecv {        // Struttura per passare gli argomenti al thread: ackReceiver
  unsigned short* cong_win;
  pthread_t*  	threadIds;
  int	        socketDescriptor;
};

// dichiarazioni delle funzioni 
void* gestisciRichiesta(void* richiestaDaGestire);

void* listRequest(struct arg_struct* args);
void* getRequest(struct arg_struct* args);
void* putRequest(struct arg_struct* args);

void* printFilesName(struct arg_write_print* arg);
void* writeOnFile(struct arg_write_print* arg);
void* writeN(struct arg_read_write* arg);
void* readN(struct arg_read_write* args);
	
void* gestisciPacchetto(struct arg_struct* arg);
void* gestisciAck(struct arg_struct* args);
void* gestisciAttesa(struct arg_attesa* arg); 
void* ackReceiver(struct arg_ackRecv* arg_ack);

void* timer(pthread_t* thread_id);
void* timer_close(pthread_t* thread_id);
void* time_milli_print();


int main(int argc, char *argv[ ]) {
	// controlla numero degli argomenti
	if(argc != 2){
		perror("Main: utilizzo: daytime_clientUDP <indirizzo IP server>");
		exit(1);
	}

	// inizializzo i mutex per controllare accessi alle funzioni ausiliari
	if (pthread_mutex_init(&mutexScarta, NULL) != 0) {
        perror("main: errore nella mutexScarta");
        exit(1);
    }
    if (pthread_mutex_init(&mutexControlla, NULL) != 0) {
        perror("main: errore nella mutexControlla");
        exit(1);
    }
    if (pthread_mutex_init(&mutexHeaderPrint, NULL) != 0) {
        perror("main: errore nella mutexHeaderPrint");
        exit(1);
    }
    if (pthread_mutex_init(&mutexBitManipulation, NULL) != 0) {
        perror("main: errore nella mutexBitManipulation");
        exit(1);
    }
    if (pthread_mutex_init(&mutexBuffFill, NULL) != 0) {
        perror("main: errore nella mutexBuffFill");
        exit(1);
    }
    if (pthread_mutex_init(&mutexAck, NULL) != 0) {
        perror("main: errore nella mutexAck");
        exit(1);
    }
	if (pthread_mutex_init(&mutexRcv_base, NULL) != 0) {
        perror("main: errore nella mutexRcv_base");
        exit(1);
    }

	indirizzoServ = argv[1];	// var. globale, indirizzo del server accessibile a tutti i server
	pthread_t thread_id;		// ID del thread
	char*	puntatoreRichiesta;	// ......

	time_milli = 0; //ABCD
	// attivo il gthread che lo stampa // ABCD
	if(pthread_create(&thread_id, NULL, time_milli_print, NULL) != 0) {
		perror("main: errore nella pthread_create");
		exit(1);
	}

	// attiva gestori per ogni richiesta in input
	while(1){
		// salvo spazio per la richiesta
		puntatoreRichiesta = (char *)calloc(1,SHORT_PAY);
		if (puntatoreRichiesta == NULL) {	//errore nella calloc
			perror("main: errore nella malloc");
			exit(1);
		}

		// attendi una richiesta
		scanf("%s", puntatoreRichiesta);	// assumo che sia < 128 caratteri
		#ifdef TIME
			gettimeofday(&tv1, NULL);           // memorizzo in che istante ho ricevuto la richiesta
		#endif
		
		#ifdef PRINT
			printf("Main: richiesta: \" %s \" inserita in:  %p \n", puntatoreRichiesta, puntatoreRichiesta);
		#endif

		// attivo il gestore della richiesta
		if(pthread_create(&thread_id, NULL, gestisciRichiesta, (void*)puntatoreRichiesta) != 0) {
			perror("main: errore nella pthread_create");
			exit(1);
		}
	}
}

// Controllo richiesta se valida, connessione 
void* gestisciRichiesta(void* richiestaDaGestire) {
	char* richiesta = (char*)richiestaDaGestire;

	#ifdef PRINT
		printf("gestisciRichiesta: \"%s\" \n", richiesta);
	#endif

	// Controlla la correttezza della richiesta inserita
	if (pthread_mutex_lock(&mutexControlla)!=0) {	// Per evitare accessi sovrapposti
		perror("gestore richiesta: errore nella mutex_lock");
		exit(1);
	}
	int risultatoRichiesta = controllaRichiesta(richiesta);
	if (pthread_mutex_unlock(&mutexControlla)!=0) {
		perror("gestore richiesta: errore nella mutex_unlock");
		exit(1);
	}
	if(risultatoRichiesta == 0){
		perror("gestore richiesta: inserire: list / get_<file name> / put_<file name>\n");
		free(richiestaDaGestire);
		goto fine;	// il thread deve terminare per rimettersi in ascolto
	}

	// Creo il pacchetto
	char * requestPacket = calloc(SHORT_PACK_LEN,1);
	// list lascio header vuoto
	if (pthread_mutex_lock(&mutexBitManipulation)!=0) {	//per evitare accessi sovrapposti
		perror("gestore richiesta: errore nella mutex_lock");
		exit(1);
	}
	if(risultatoRichiesta == 2){			// get
		memset(requestPacket, setBit(requestPacket[0], 6), 1);		// richiesta get 01
	}
	if(risultatoRichiesta == 3){			// put [10]
		//Controlli se il file di put esiste, se non esiste non invio la richiesta al server 
		int file;
		char* fileName = richiesta+4; // Prendo il nome del file richiesto, leggo il payload
		char current_file[sizeof(dir_file)+sizeof(fileName)];
		strcpy(current_file,dir_file);
		strcat(current_file, fileName);

		if ((file = open(current_file, O_RDONLY)) < 0){ 
			// FILE NON PRESENTE 
			perror("getsione richiesta: File non presente");
			close(file);
			free(requestPacket);
			free(richiestaDaGestire);
			pthread_exit(0);
		}
		close(file);
		
		memset(requestPacket, setBit(requestPacket[0], 7), 1);		// richiesta put 10
	}
	if (pthread_mutex_unlock(&mutexBitManipulation)!=0) {
		perror("gestore richiesta: errore nella mutex_unlock");
		exit(1);
	}
	(*(unsigned short*)(&requestPacket[1])) = strlen(richiesta)-4;	// inserisco lunghezza payload
	strcat(requestPacket+HEADER_DIM, richiesta+4);					// appendo la richiesta dopo l'header

	#ifdef PRINT
		//stampo l'header
		printf("Pacchetto di richiesta: \n");
		if (pthread_mutex_lock(&mutexHeaderPrint)!=0) {	//per evitare accessi sovrapposti
			perror("gestore richiesta: errore nella mutex_lock");
			exit(1);
		}
		headerPrint(requestPacket);
		if (pthread_mutex_unlock(&mutexHeaderPrint)!=0) {
			perror("gestore richiesta: errore nella mutex_unlock");
			exit(1);
		}
		printf("%s\n",&requestPacket[HEADER_DIM]);
	#endif

	int   sockfd, n;
	char  recvline[HEADER_DIM];
	struct sockaddr_in servaddr;

	// creo il socket
	sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	if ((sockfd) < 0) {	// controllo eventuali errori
		perror("gestore richiesta: errore in socket");
		pthread_exit(0);
	}
	memset((void *)&servaddr, 0, sizeof(servaddr));	// azzera servaddr
	servaddr.sin_family = AF_INET;					// assegna il tipo di indirizzo
	servaddr.sin_port = htons(SERV_PORT);			// assegna la porta del server
	// assegna l'indirizzo del server, preso dalla riga di comando.
	// L'indirizzo è una stringa da convertire in intero secondo network byte order.
	if (inet_pton(AF_INET, indirizzoServ, &servaddr.sin_addr) <= 0) {
		perror("gestore richiesta: errore in inet_pton");
		exit(1);
	}
	
	//---------------- GESTORE HANDSHAKE -----------------------
	pthread_t thread_id, thread_id_attesa;  // mantengo ID del thread creato per poterlo cancellare
	//raccolgo le informazioni per la gestione del pacchetto in una struttura
	struct arg_struct * args = (struct arg_struct *)malloc(sizeof(struct arg_struct));
	socklen_t lenadd = sizeof(struct sockaddr_in);
	args->addr = servaddr;
	args->socketDescriptor = sockfd;
	args->buff = requestPacket;
	int result;

	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

	if(pthread_create(&thread_id, &attr, (void*)gestisciPacchetto, args) != 0) {
		perror("putRequest: errore nella pthread_create");
		exit(1);
	}

	pthread_attr_destroy(&attr);

	// inizio attesa della risposta del server 
	struct arg_attesa argomenti;
	argomenti.richiesta = richiesta;
	argomenti.threadChiamante = pthread_self();	// Se scado il timer Attesa, chiuso e riapro gestisciRIchiesta

	if(pthread_create(&thread_id_attesa, NULL, (void *)gestisciAttesa, &argomenti) != 0) {
		perror("gestore richiesta: errore nella pthread_create");
		exit(1);
	}
	args->thread_id_attesa = thread_id_attesa;
	
	// Attesa di un ack, solo HEADER 
	n = recvfrom(sockfd, recvline, HEADER_DIM, 0,  (struct sockaddr*)&args->addr, &lenadd);
	if (n < 0) {
		perror("gestore richiesta: errore in recvfrom");
		exit(1);
	}
	if (n > 0) {
		time_milli = time_milli + bonus;
		// Ho ricevuto qualcosa, sia ack che nack, devo smettere di inviare il packet
		#ifdef PRINT
			printf("gestore richiesta: chiudo gestore pacchetto, la richiesta è stata inviata\n");
		#endif
		if(pthread_cancel(thread_id) != 0) { // Cancello il thread di gestisciPacchetto
			perror("gestore richiesta: errore nella pthread_cancel di gestisciPacchetto");
			exit(1);
		}
		// controllo se ho ricevuto ack, se si => il primo byte di risposta == al primo byte header
		if (pthread_mutex_lock(&mutexBitManipulation)!=0) {	//per evitare accessi sovrapposti
			perror("gestore richiesta: errore nella mutex_lock");
			exit(1);
		}
		result = (readBit(recvline[0], 7) == readBit(requestPacket[0], 7)) & (readBit(recvline[0], 6) == readBit(requestPacket[0], 6));
		if (pthread_mutex_unlock(&mutexBitManipulation)!=0) {	//per evitare accessi sovrapposti
			perror("gestore richiesta: errore nella mutex_unlock");
			exit(1);
		}
		if(result == 0){ // ho ricevuto un nack
			#ifdef PRINT
				printf("gestore richiesta: thread richiesta: \"%s\" risposta: nack \n", richiesta);
			#endif
			printf("Ho ricevuto un nack, richiesta non servita\n");
			// Chiudo gestisci attesa
			if(pthread_cancel(thread_id_attesa) != 0) {
				perror("gestore richiesta: errore nella pthread_cancel di gestisciAttesa");
				exit(1);
			}
			close(sockfd);
			free(requestPacket);
			free(richiestaDaGestire);
			goto fine;
		}
	}
	#ifdef PRINT
		printf("gestore richiesta: Ricevuto ack da IP: %s e porta: %i\n",inet_ntoa(args->addr.sin_addr),ntohs(args->addr.sin_port));
	#endif
	
	// Creo il pacchetto di ack per concludere l'handshake 
	char * ackPacket = calloc(HEADER_DIM,1);
	if (pthread_mutex_lock(&mutexBitManipulation)!=0) {	// Per evitare accessi sovrapposti
		perror("gestore richiesta: errore nella mutex_lock");
		exit(1);
	}
	memset(ackPacket, setBit(ackPacket[0], 6), 1);
	memset(ackPacket, setBit(ackPacket[0], 7), 1);		// 11000000 ultimo ack handshake
	if (pthread_mutex_unlock(&mutexBitManipulation)!=0) {	// Per evitare accessi sovrapposti
		perror("gestore richiesta: errore nella mutex_unlock");
		exit(1);
	}
	// invio il pacchetto
	if (sendto(sockfd, ackPacket, HEADER_DIM,  0, (struct sockaddr*)&args->addr, sizeof(servaddr)) < 0) {
		perror("gestore richiesta: errore sendto");
		exit(1);
	}
	time_milli = time_milli + malus;

	// A seconda della richiesta, il corrispondente thread prende il controllo
	if (risultatoRichiesta == 1) { // list
		if(pthread_create(&thread_id, NULL, (void*)listRequest, (void*)args) != 0) {
			perror("gestore richiesta: errore nella pthread_create");
			exit(1);
		}
	}
	else {
		if (risultatoRichiesta == 2) { // get
			if(pthread_create(&thread_id, NULL, (void*)getRequest, (void*)args) != 0) {
				perror("gestore richiesta: errore nella pthread_create");
				exit(1);
			}
		}
		else {	// put
			if(pthread_cancel(thread_id_attesa) != 0) { // Cancello l'handshake poichè è il client ad iniziare ad inviare pacchetti
				perror("gestore richiesta: errore nella pthread_cancel");
				exit(1);
			}
			#ifdef PRINT
				printf("gestore richiesta: chiuso il thread ATTESA, handshake chiuso\n");
			#endif
			if(pthread_create(&thread_id, NULL, (void*)putRequest, (void*)args) != 0) {
				perror("gestore richiesta: errore nella pthread_create");
				exit(1);
			}
		}
	}
	
	fine:
	#ifdef PRINT
		printf("gestisciRichiesta: termino, richiesta già associata o scartata\n");
	#endif
	pthread_exit(NULL);	// termina il thread
}

void* listRequest(struct arg_struct* args) {
	#ifdef PRINT
		printf("List request \n");
	#endif

	int	sockfd = args->socketDescriptor;				// Descrittore della socket per la recvFrom
	pthread_t thread_id_attesa = args->thread_id_attesa;
	char ricorda_cancellare_attesa = 's';
	
	pthread_t thread_id_timer; // Id del timer di chiusura
	unsigned sequenceNumber;   // Numero di pacchetto ricevuto
	int ultimo = 0; 		   // 1 -> ho ricevuto l'ultimo pacchetto
	unsigned last_packet = 0;  // Sequence Number dell'ultimo pacchetto
	unsigned* rcv_base = (unsigned*)calloc(sizeof(unsigned),1); // Numero di pacchetto atteso
	
	free(args->buff);

	// ----------- SELECTIVE REPEAT RECV -----------

	// creo il pacchetto con cui ackare
	pthread_t thread_id_Ack;
	char* ack_vector[WINDOWSIZE];
	char* ackPacket;
	
	// creo il buffer in cui metto i pacchetti, e quello in cui scrivo quali spazi sono liberi
	char recevedPack[PACKET_LEN];	// Buffer del recvFrom
	char* payloads_fill_vector = malloc(WINDOWSIZE);	// 0 -> libero (atteso), 1 -> occupato (bufferizzato) 
	char* packetPointer[WINDOWSIZE]; // Puntatore alle WINDOWSIZE aree di memoria per i pacchetti della finestra di congestione
	
	struct arg_struct* args_vector[WINDOWSIZE];
    
	// attivo thread write on file che copia i pacchetti
	pthread_t thread_id_Print; 
	struct arg_write_print *argomenti = (struct arg_write_print *)malloc(sizeof(struct arg_write_print));
	
	for(int i = 0; i < WINDOWSIZE ; i++){   // Inizializzazioni aree di memoria per pacchetti ricevuti e ack da inviare
		payloads_fill_vector[i] = '0';
		packetPointer[i] = (char*)calloc(PACKET_LEN,1);// Buffer di dimensione long per inviare pacchetti con dati
		argomenti->payloads_vector[i] = packetPointer[i];
		
		ack_vector[i] = (char*)calloc(HEADER_DIM,1);   // Buffer di dimensione long per inviare pacchetti con dati
		
		ackPacket = ack_vector[i];  				   // Assegno un nuovo buffer per il riempimento 
		if (pthread_mutex_lock(&mutexBitManipulation) != 0) {	    // per evitare accessi sovrapposti
			perror("getRequest: errore nella mutex_lock");
			exit(1);
		}
		memset(ackPacket, setBit(ackPacket[0], 6), 1);
		memset(ackPacket, setBit(ackPacket[0], 7), 1);	// 11
		if (pthread_mutex_unlock(&mutexBitManipulation) !=0) {	// per evitare accessi sovrapposti
			perror("getRequest: errore nella mutex_unlock");
			exit(1);
		}
		
		args_vector[i] = malloc(sizeof(struct arg_struct));
        args_vector[i]->addr = args->addr;
        args_vector[i]->socketDescriptor = args->socketDescriptor;
	}

	argomenti->threadChiamante = pthread_self();
	argomenti->payloads_fill_vector = payloads_fill_vector;
	argomenti->rcv_base = rcv_base;

	if(pthread_create(&thread_id_Print, NULL, (void *)printFilesName, argomenti) != 0) {
		perror("listRequest: errore nella pthread_create");
		exit(1);
	}

	while(1){
		// Ricevo un pacchetto
		if(recvfrom(sockfd, recevedPack, PACKET_LEN, 0, NULL, NULL) < 0){
			perror("listRequest: errore in recvfrom");
			exit(1);
		}
		time_milli = time_milli + bonus;

		// Ho ricevuto un pacchetto correttamente 
		sequenceNumber = (*(unsigned*)(&recevedPack[3])); // Prendo il sequence del pacchetto ricevuto number ricevuto
		#ifdef PRINT
			printf("listRequest: Ho ricevuto %d\n", sequenceNumber);
		#endif
		
		// se è il primo pacchetto, concludo l'handshake
		if(ricorda_cancellare_attesa == 's'){ 
			#ifdef PRINT
				printf("listRequest: handshake chiuso\n");
			#endif
			
			if(pthread_cancel(thread_id_attesa) != 0) {
				perror("listRequest: errore nella pthread_cancel thread_id_attesa");
				exit(1);
			}
			ricorda_cancellare_attesa = 'n';
		}

		// Imposta pacchetto di ack 
		ackPacket = ack_vector[sequenceNumber % WINDOWSIZE];
		memcpy(ackPacket+3,&sequenceNumber,sizeof(sequenceNumber)); // Scrivo il seqNumber nel pacchetto di ack 
		args_vector[(sequenceNumber % WINDOWSIZE)]->buff = ackPacket;	

		if( (sequenceNumber >= *rcv_base) && (sequenceNumber <= *rcv_base+WINDOWSIZE-1)){
			
			#ifdef PRINT
				//printf("listRequest: finestra [rcv_base,rcv_base+WINDOWSIZE-1]\n");
			#endif
			
			if(payloads_fill_vector[sequenceNumber % WINDOWSIZE] == '0'){	// Se ho spazio nel buffer
				memcpy(packetPointer[sequenceNumber % WINDOWSIZE],recevedPack,PACKET_LEN); // Bufferizzo il pacchetto ricevuto
				
				if (pthread_mutex_lock(&mutexBuffFill)!=0) {	//per evitare accessi sovrapposti
					perror("listRequest: errore nella mutex_lock");
					exit(1);
				}
				payloads_fill_vector[sequenceNumber % WINDOWSIZE] = '1';
				if (pthread_mutex_unlock(&mutexBuffFill)!=0) {	//per evitare accessi sovrapposti
					perror("listRequest: errore nella mutex_unlock");
					exit(1);
				}
				
				pthread_attr_t attr;
                pthread_attr_init(&attr);
                pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

				if(pthread_create(&thread_id_Ack, &attr, (void *)gestisciAck, (void*)args_vector[(sequenceNumber % WINDOWSIZE)]) != 0) {
					perror("putRequest: errore nella pthread_create di gestisciAck");
					exit(1);
				}
                pthread_attr_destroy(&attr);

				if (pthread_mutex_lock(&mutexBitManipulation)!=0) {	    //per evitare accessi sovrapposti
					perror("listRequest: errore nella mutex_lock");
					exit(1);
				}
				if(readBit(packetPointer[sequenceNumber % WINDOWSIZE][0], 5) == 1) {
					ultimo = 1; 
					last_packet = sequenceNumber;
				}
				if (pthread_mutex_unlock(&mutexBitManipulation)!=0) {	//per evitare accessi sovrapposti
					perror("listRequest: errore nella mutex_unlock");
					exit(1);
				}
			
				if((ultimo == 1) & (*rcv_base == last_packet)){	// Se ho ricevuto l'ultimo pacchetto
					// Mi aspettavo l'ultimo pacchetto, non ho più pacchetti da ricevere
					// Parte un timer, se non ricevo nulla nel frattempo, termino la get
									// Se ricevo un pacchetto, faccio ripartire il timer di nuovo 
					timer:
					if(pthread_create(&thread_id_timer, NULL, (void *)timer_close, (void*)pthread_self()) != 0) {
						perror("listRequest: errore nella pthread_create di timer");
						exit(1);
					} 

					if(recvfrom(sockfd, recevedPack, PACKET_LEN, 0, NULL, NULL) < 0){
						perror("listRequest: errore in recvfrom"); 
						exit(1);
					}
					time_milli = time_milli + bonus;

					// Se ho ricevuto qualcosa
					pthread_cancel(thread_id_timer); 
					printf("Timer cancellato, il server non è sicuro di avermi inviato tutto\n");

					// Imposta pacchetto di ack 
					sequenceNumber = (*(unsigned*)(&recevedPack[3]));    // Prendo il sequence del pacchetto ricevuto number ricevuto
					
					// Imposta pacchetto di ack 
					ackPacket = ack_vector[sequenceNumber % WINDOWSIZE];
					memcpy(ackPacket+3,&sequenceNumber,sizeof(sequenceNumber)); // Scrivo il seqNumber nel pacchetto di ack 
					args_vector[(sequenceNumber % WINDOWSIZE)]->buff = ackPacket;	

					pthread_attr_t attr;
					pthread_attr_init(&attr);
					pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

					if(pthread_create(&thread_id_Ack, &attr, (void *)gestisciAck, (void*)args_vector[(sequenceNumber % WINDOWSIZE)]) != 0) {
						perror("putRequest: errore nella pthread_create di gestisciAck");
						exit(1);
					}

					pthread_attr_destroy(&attr);
					
					goto timer;
				}
			} else if(sequenceNumber < *rcv_base) {
				pthread_attr_t attr;
                pthread_attr_init(&attr);
                pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

				if(pthread_create(&thread_id_Ack, &attr, (void *)gestisciAck, (void*)args_vector[(sequenceNumber % WINDOWSIZE)]) != 0) {
					perror("putRequest: errore nella pthread_create di gestisciAck");
					exit(1);
				}

                pthread_attr_destroy(&attr);
			}
	
		} else if ((sequenceNumber >= (*rcv_base-WINDOWSIZE)) && (sequenceNumber <= (*rcv_base-1))){
			
			#ifdef PRINT
				//printf("listRequest: finestra [rcv_base-WINDOWSIZE,rcv_base-1] \n");
				printf("listRequest: invio ack %u \n", sequenceNumber);
			#endif

			pthread_attr_t attr;
			pthread_attr_init(&attr);
			pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

			if(pthread_create(&thread_id_Ack, &attr, (void *)gestisciAck, (void*)args_vector[(sequenceNumber % WINDOWSIZE)]) != 0) {
				perror("putRequest: errore nella pthread_create di gestisciAck");
				exit(1);
			}

			pthread_attr_destroy(&attr);
		}
		else{
			#ifdef PRINT
				printf("listRequest: Ignora, rcv_base %d\n",*rcv_base);
			#endif
		}
	}
	for(int i = 0 ; i < WINDOWSIZE ; i++){
        free(packetPointer[i]);
        free(ack_vector[i]);
		free(args_vector[i]);
    }

	close(args->socketDescriptor);
	#ifdef PRINT
		printf("Fine list\n");
	#endif
	pthread_exit(NULL);
}

void* getRequest(struct arg_struct* args) {
	#ifdef PRINT
		printf("Get request \n");
	#endif

	int	sockfd = args->socketDescriptor;				// Descrittore della socket per la recvFrom
	pthread_t thread_id_attesa = args->thread_id_attesa;

	char ricorda_cancellare_attesa = 's';
	int fileptr;               // Descrittore del file aperto
	pthread_t thread_id_timer; // Id del timer 
	unsigned sequenceNumber;   // Numero di pacchetto ricevuto
	int ultimo = 0; 		   // 1 -> ho ricevuto l'ultimo pacchetto
	unsigned last_packet = 0;  // Sequence Number dell'ultimo pacchetto
	unsigned* rcv_base = (unsigned*)calloc(sizeof(unsigned),1); // Numero di pacchetto atteso

	// Creo il path corretto per il raggiungimento del pacchetto che si desidera   
	char* fileName = &(args->buff)[HEADER_DIM];   // Prendo il nome del file richiesto, leggo da header in poi
	char current_file[sizeof(dir_file)+sizeof(fileName)];
	strcpy(current_file, dir_file);
	strcat(current_file, fileName);
	
	// Apro il file richiesto; se non esiste lo creo
	#ifdef PRINT
		printf("getRequest: vorrei aprire: %s\n", current_file);
	#endif
	
	if (remove(current_file) == 0) {	// Cancella il file se già esiste
		#ifdef PRINT
			printf("getRequest: Il file è stato eliminato per ricrearlo\n");
		#endif
	} 

	if ((fileptr = open(current_file, O_CREAT|O_RDWR|O_APPEND, 0777)) < 0){
		perror("getRequest: errore in open");
		exit(1);
	}  
	#ifdef PRINT
		printf("getRequest: file aperto\n");
	#endif
	
	free(args->buff);

	// ----------- SELECTIVE REPEAT RECV -----------

	// creo il pacchetto con cui ackare
	pthread_t thread_id_Ack;
	char* ack_vector[WINDOWSIZE];
	char* ackPacket;
	
	// creo il buffer in cui metto i pacchetti, e quello in cui scrivo quali spazi sono liberi
	char recevedPack[PACKET_LEN];	// Buffer del recvFrom
	char* payloads_fill_vector = malloc(WINDOWSIZE);	// 0 -> libero (atteso), 1 -> occupato (bufferizzato) 
	char* packetPointer[WINDOWSIZE]; // Puntatore alle WINDOWSIZE aree di memoria per i pacchetti della finestra di congestione
	
	struct arg_struct* args_vector[WINDOWSIZE];
    
	// attivo thread write on file che copia i pacchetti
	pthread_t thread_id_Write; 
	struct arg_write_print *argomenti = (struct arg_write_print *)malloc(sizeof(struct arg_write_print));
	
	for(int i = 0; i < WINDOWSIZE ; i++){   // Inizializzazioni aree di memoria per pacchetti ricevuti e ack da inviare
		payloads_fill_vector[i] = '0';
		packetPointer[i] = (char*)calloc(PACKET_LEN,1);// Buffer di dimensione long per inviare pacchetti con dati
		argomenti->payloads_vector[i] = packetPointer[i];
		
		ack_vector[i] = (char*)calloc(HEADER_DIM,1);   // Buffer di dimensione long per inviare pacchetti con dati
		
		ackPacket = ack_vector[i];  				   // Assegno un nuovo buffer per il riempimento 
		if (pthread_mutex_lock(&mutexBitManipulation) != 0) {	    // per evitare accessi sovrapposti
			perror("getRequest: errore nella mutex_lock");
			exit(1);
		}
		memset(ackPacket, setBit(ackPacket[0], 6), 1);
		memset(ackPacket, setBit(ackPacket[0], 7), 1);	// 11
		if (pthread_mutex_unlock(&mutexBitManipulation) !=0) {	// per evitare accessi sovrapposti
			perror("getRequest: errore nella mutex_unlock");
			exit(1);
		}
		args_vector[i] = malloc(sizeof(struct arg_struct));
        args_vector[i]->addr = args->addr;
        args_vector[i]->socketDescriptor = args->socketDescriptor;
	}

	argomenti->fileDescriptor = fileptr;
	argomenti->threadChiamante = pthread_self();
	argomenti->payloads_fill_vector = payloads_fill_vector;
	argomenti->rcv_base = rcv_base;

	if(pthread_create(&thread_id_Write, NULL, (void *)writeOnFile, argomenti) != 0) {
		perror("getRequest: errore nella pthread_create");
		exit(1);
	}

	while(1){
		// Ricevo un pacchetto
		if(recvfrom(sockfd, recevedPack, PACKET_LEN, 0, NULL, NULL) < 0){
			perror("getRequest: errore in recvfrom");
			exit(1);
		}
		time_milli = time_milli + bonus;

		// Ho ricevuto un pacchetto correttamente 
		sequenceNumber = (*(unsigned*)(&recevedPack[3])); // Prendo il sequence del pacchetto ricevuto number ricevuto
		#ifdef PRINT
			printf("getRequest: Ho ricevuto %d\n", sequenceNumber);
		#endif
		
		// se è il primo pacchetto, concludo l'handshake
		if(ricorda_cancellare_attesa == 's'){ 
			#ifdef PRINT
				printf("getRequest: handshake chiuso\n");
			#endif
			
			if(pthread_cancel(thread_id_attesa) != 0) {
				perror("getRequest: errore nella pthread_cancel thread_id_attesa");
				exit(1);
			}
			ricorda_cancellare_attesa = 'n';
		}

		// Imposta pacchetto di ack 
		ackPacket = ack_vector[sequenceNumber % WINDOWSIZE];
		memcpy(ackPacket+3,&sequenceNumber,sizeof(sequenceNumber)); // Scrivo il seqNumber nel pacchetto di ack 
		args_vector[(sequenceNumber % WINDOWSIZE)]->buff = ackPacket;	

		if( (sequenceNumber >= *rcv_base) & (sequenceNumber <= *rcv_base+WINDOWSIZE-1)){
			
			#ifdef PRINT
				//printf("getRequest: finestra [rcv_base,rcv_base+WINDOWSIZE-1]\n");
			#endif
			
			if(payloads_fill_vector[sequenceNumber % WINDOWSIZE] == '0'){	// Se ho spazio nel buffer
				memcpy(packetPointer[sequenceNumber % WINDOWSIZE],recevedPack,PACKET_LEN); // Bufferizzo il pacchetto ricevuto
				
				if (pthread_mutex_lock(&mutexBuffFill)!=0) {	//per evitare accessi sovrapposti
					perror("getRequest: errore nella mutex_lock");
					exit(1);
				}
				payloads_fill_vector[sequenceNumber % WINDOWSIZE] = '1';
				if (pthread_mutex_unlock(&mutexBuffFill)!=0) {	//per evitare accessi sovrapposti
					perror("getRequest: errore nella mutex_unlock");
					exit(1);
				}
				
				pthread_attr_t attr;
                pthread_attr_init(&attr);
                pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

				if(pthread_create(&thread_id_Ack, &attr, (void *)gestisciAck, (void*)args_vector[(sequenceNumber % WINDOWSIZE)]) != 0) {
					perror("getRequest: errore nella pthread_create di gestisciAck");
					exit(1);
				}
                pthread_attr_destroy(&attr);
				
				
				if (pthread_mutex_lock(&mutexBitManipulation)!=0) {	    //per evitare accessi sovrapposti
					perror("getRequest: errore nella mutex_lock");
					exit(1);
				}
				if(readBit(packetPointer[sequenceNumber % WINDOWSIZE][0], 5) == 1) {
					ultimo = 1; 
					last_packet = sequenceNumber;
				}
				if (pthread_mutex_unlock(&mutexBitManipulation)!=0) {	//per evitare accessi sovrapposti
					perror("getRequest: errore nella mutex_unlock");
					exit(1);
				}
			
				if((ultimo == 1) & (*rcv_base == last_packet)){	// Se ho ricevuto l'ultimo pacchetto
					// Mi aspettavo l'ultimo pacchetto, non ho più pacchetti da ricevere
					// Parte un timer, se non ricevo nulla nel frattempo, termino la get
									// Se ricevo un pacchetto, faccio ripartire il timer di nuovo 
					timer:
					if(pthread_create(&thread_id_timer, NULL, (void *)timer_close, (void*)pthread_self()) != 0) {
						perror("getRequest: errore nella pthread_create di timer");
						exit(1);
					} 

					if(recvfrom(sockfd, recevedPack, PACKET_LEN, 0, NULL, NULL) < 0){
						perror("getRequest: errore in recvfrom");
						exit(1);
					}
					time_milli = time_milli + bonus;

					// Se ho ricevuto qualcosa
					pthread_cancel(thread_id_timer); 
					printf("getRequest: Timer cancellato, il server non è sicuro di avermi inviato tutto\n");

					// Imposta pacchetto di ack 
					sequenceNumber = (*(unsigned*)(&recevedPack[3]));    // Prendo il sequence del pacchetto ricevuto number ricevuto
					
					// Imposta pacchetto di ack 
					ackPacket = ack_vector[sequenceNumber % WINDOWSIZE];
					memcpy(ackPacket+3,&sequenceNumber,sizeof(sequenceNumber)); // Scrivo il seqNumber nel pacchetto di ack 
					args_vector[(sequenceNumber % WINDOWSIZE)]->buff = ackPacket;	


					pthread_attr_t attr;
					pthread_attr_init(&attr);
					pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

					if(pthread_create(&thread_id_Ack, &attr, (void *)gestisciAck, (void*)args_vector[(sequenceNumber % WINDOWSIZE)]) != 0) {
						perror("getRequest: errore nella pthread_create di gestisciAck");
						exit(1);
					}

					pthread_attr_destroy(&attr);
					goto timer;
				}
			} else if(sequenceNumber < *rcv_base) {
				pthread_attr_t attr;
                pthread_attr_init(&attr);
                pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

				if(pthread_create(&thread_id_Ack, &attr, (void *)gestisciAck, (void*)args_vector[(sequenceNumber % WINDOWSIZE)]) != 0) {
					perror("getRequest: errore nella pthread_create di gestisciAck");
					exit(1);
				}

                pthread_attr_destroy(&attr);
			}
	
		} else if ((sequenceNumber >= (*rcv_base-WINDOWSIZE)) && (sequenceNumber <= (*rcv_base-1))){
			
			#ifdef PRINT
				//printf("getRequest: finestra [rcv_base-WINDOWSIZE,rcv_base-1] \n");
				printf("getRequest: invio ack %u \n", sequenceNumber);
			#endif

			pthread_attr_t attr;
			pthread_attr_init(&attr);
			pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

			if(pthread_create(&thread_id_Ack, &attr, (void *)gestisciAck, (void*)args_vector[(sequenceNumber % WINDOWSIZE)]) != 0) {
				perror("getRequest: errore nella pthread_create di gestisciAck");
				exit(1);
			}

			pthread_attr_destroy(&attr);
		}
		else{
			#ifdef PRINT
				printf("getRequest: Ignora, rcv_base %d\n",*rcv_base);
			#endif
		}
	}
	for(int i = 0 ; i < WINDOWSIZE ; i++){
        free(packetPointer[i]);
        free(ack_vector[i]);
		free(args_vector[i]);
    }
	close(args->socketDescriptor);
	pthread_exit(NULL);
}

void* putRequest(struct arg_struct* args) {
	#ifdef PRINT
		printf("Put request \n");
	#endif

	//----------------- INIZIALIZZAZIONE RICHIESTA -----------------
    int sockReq = args->socketDescriptor;
    struct arg_read_write* args_R = malloc(sizeof(struct arg_read_write)); // Creo l'aria di memoria della struct per i parametri della read 
	pthread_t thread_id_attesa = args->thread_id_attesa;
	int file;

    // Creo il path corretto per il raggiungimento del pacchetto che si desidera   
    char* fileName = &(args->buff)[HEADER_DIM];   // Prendo il nome del file richiesto, leggo da header in poi
    char current_file[sizeof(dir_file)+sizeof(fileName)];
    strcpy(current_file, dir_file);
    strcat(current_file, fileName);

    // Apro il file e ne prendo le dimensioni che andranno inviate nel payload del primo ack 
    struct stat file_stat;
    if ((file = open(current_file, O_RDWR, 0777)) < 0){
		perror("putRequest: errore in open");
		exit(1);
	}  
	#ifdef PRINT
		printf("putRequest: file aperto\n");
	#endif
	
	args_R->fileDescriptor = file;

    if (fstat(file, &file_stat) < 0){          // Ottengo le dimensioni del file da inviare
        fprintf(stderr, "putRequest: Error fstat: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    unsigned long size = file_stat.st_size;
    #ifdef PRINT
        printf("putRequest: Path: %s --- Numero caratteri nel file: %lu\n", current_file, size);                         
    #endif

    free(args->buff); // Libero la memoria allocata per la richiesta del client nella funzione main 

    //----------------- GESTIONE RICHIESTA -----------------
    struct arg_ackRecv* arg_ack = malloc(sizeof(struct arg_ackRecv)); // Creo l'aria di memoria della struct per i parametri
    struct arg_struct* args_vector[WINDOWSIZE];
    pthread_t* threadIds = (pthread_t*) calloc(WINDOWSIZE,sizeof(pthread_t)); // Buffer contenente gli Id dei thread creati per inviare i pacchetti
    pthread_t* thread_read = (pthread_t*) calloc(sizeof(pthread_t),1);

    unsigned* numPck = calloc(sizeof(unsigned),1);          // Contatore pacchetti inviati
    unsigned short* cong_win = calloc(sizeof(unsigned short),1); // Contatore pacchetti in volo
    int rtnValue;                    // Valore di ritorno della readN, per controllare se è andata a buon fine o meno

    char* packetPointer[WINDOWSIZE]; // Puntatore alle WINDOWSIZE aree di memoria per i pacchetti della finestra di congestione
    for (int i=0 ; i<WINDOWSIZE; i++) {
        packetPointer[i] = (char*)calloc(PACKET_LEN,1); // Buffer di dimensione long per inviare pacchetti con dati
    
        if (pthread_mutex_lock(&mutexBitManipulation)!=0) {	
            perror("putRequest: errore nella mutex_lock");
            exit(1);
        }
        memset((void*)packetPointer[i], setBit(packetPointer[i][0], 6), 1);	// Imposto [01 ... ] secondo bit per la get
        if (pthread_mutex_unlock(&mutexBitManipulation)!=0) {	
            perror("putRequest: errore nella mutex_unlock");
            exit(1);
        }
        
        args_vector[i] = malloc(sizeof(struct arg_struct));
        args_vector[i]->addr = args->addr;
        args_vector[i]->socketDescriptor = args->socketDescriptor;
    }
    char* packet = packetPointer[0];


    // ------------ THREAD ASCOLTA ACK DAL CLIENT ------------  
    pthread_t thread_ack;
    arg_ack->socketDescriptor = args->socketDescriptor;
    arg_ack->cong_win = cong_win;
    arg_ack->threadIds = threadIds;

    if(pthread_create(&thread_ack, NULL, (void *)ackReceiver, arg_ack) != 0) {
        perror("putRequest: errore nella pthread_create ackReceiver");
        exit(1);
    }

    // ----- INVIO FILE CON SELECTIVE -----
    while(size > 0 || *cong_win > 0){              // Finchè c'è qualcosa da inviare || qualcosa da ricevere 
        while(*cong_win < WINDOWSIZE & size > 0){  // Se devo inviare un pacchetto e c'è spazio per farlo
        
        packet = packetPointer[*numPck % WINDOWSIZE]; // Assegno un nuovo buffer per il riempimento 
        memset(packet+1,0,PACKET_LEN-1);              // Resetto tutto il pacchetto, non servirebbe perchè scriviamo e leggiamo con i byte effettivi scritti nell'header
        
        // ----- CREO PACCHETTO DA INVIARE -----
        if(size <= LONG_PAY){ // Ultimo pacchetto da inviare, sono rimasti nel file meno di LONG_PAY 
            if (pthread_mutex_lock(&mutexBitManipulation)!=0) {	
                perror("putRequest: errore nella mutex_lock");
                exit(1);
            }
            memset((void*)packet, setBit(packet[0], 5), 1);	// Imposto [011 ... ] terzo bit per il flag ultimo pacchetto
            if (pthread_mutex_unlock(&mutexBitManipulation)!=0) {	
                perror("putRequest: errore nella mutex_unlock");
                exit(1);
            }
            
            args_R->buff = packet+HEADER_DIM;
            args_R->nBytes = size;
            size = 0; 

        } else { // Caso in cui posso leggere dal file LONG_PAY
            args_R->buff = packet+HEADER_DIM;
            args_R->nBytes = LONG_PAY;
            size -= LONG_PAY;
        }
        
        // Creo l'header del pacchetto
        (*(unsigned short*)(&packet[1])) = (unsigned short)args_R->nBytes; // Assegno il numero di byte da leggere 
        (*(unsigned*)(&packet[3])) = (unsigned)*numPck;                    // Assegno il numero di pacchetto corrente 

        // Chiamo la readN e controllo il valore di ritorno 
        if(pthread_create(thread_read, NULL, (void *)readN, args_R) != 0) {
            perror("putRequest: errore nella pthread_create");
            exit(1);
        }
        if(pthread_join(*thread_read,(void*)&rtnValue) != 0) {
            perror("putRequest: errore nella pthread_join");
            exit(1);
        }
        if(rtnValue == -1){
            perror("putRequest: errore in readN");
            exit(1);
        }

        // ----- INVIO PACCHETTO -----
        
        repeat:
        if(threadIds[(*numPck % WINDOWSIZE)] != 0){ // Controllo se l'area di memoria che dovrà occupare il thread è libera
            printf("REPEAT --------------\n"); 
            goto repeat;
        }

        args_vector[(*numPck % WINDOWSIZE)]->buff = packet; // Salvo in arg->buff tutto il pacchetto da inviare

        pthread_attr_t attr;
		pthread_attr_init(&attr);
		pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

		if(pthread_create(&threadIds[(*numPck % WINDOWSIZE)], &attr, (void*)gestisciPacchetto, args_vector[(*numPck % WINDOWSIZE)]) != 0) {
            perror("putRequest: errore nella pthread_create");
            exit(1);
        }

		pthread_attr_destroy(&attr);

        if (pthread_mutex_lock(&mutexAck)!=0) {	// per evitare accessi sovrapposti
            perror("putRequest: errore nella mutex_lock");
            exit(1);
        }
        *cong_win += 1;
        if (pthread_mutex_unlock(&mutexAck)!=0) {	// per evitare accessi sovrapposti
            perror("putRequest: errore nella mutex_unlock");
            exit(1);
        }

        #ifdef PRINT
            printf("putRequest: #thread: ");
            for(int i = 0; i < WINDOWSIZE ; i++){
            printf("%ld, ", threadIds[i]);
            }
            printf("\n");
            printf("putRequest: NumPack = %d, CongWin %d\n", *numPck ,*cong_win);
        #endif   

        *numPck += 1;   
        }
    }

    // ----- ELIMINO AREE DI MEMORIA E THREAD -----
    for (int i=0 ; i<WINDOWSIZE; i++) { 
        free(packetPointer[i]); 
        free(args_vector[i]);
    };

    if(pthread_cancel(thread_ack) != 0) {
        perror("putRequest: errore nella pthread_cancel dell'ack");
        exit(1); 
    }    

	#ifdef TIME
		gettimeofday(&tv2, NULL);
		long millisec1 = tv1.tv_usec / 1000 + tv1.tv_sec * 1000;
		long millisec2 = tv2.tv_usec / 1000 + tv2.tv_sec * 1000;
		printf("millisecondi trascorsi: %li \n", millisec2 - millisec1);  
	#endif

    close(file);
    close(sockReq);
    free(cong_win);
    free(numPck);
    free(args_R);
    free(arg_ack);
    free(args);
    free(thread_read);
    free(threadIds);

	printf("Richiesta Put Servita\n\n");
	pthread_exit(0);
}


void* gestisciAttesa(struct arg_attesa* arg) { // Creo un timer che dorme, all suo risveglio chiude e riapre gestisciRichiesta
	// In modo da re inviare la richiesta al client 
	
	char* richiesta = (char *)malloc(SHORT_PACK_LEN); 
	richiesta = arg->richiesta;
	pthread_t thread_id_chiamante = arg->threadChiamante;
	pthread_t dummy;
	
	#ifdef PRINT
		printf("gestisciAttesa: attesa del primo pacchetto dal server\n");
	#endif
	usleep(WAIT_FACTOR * timeOutSec * 1000);
	
	#ifdef PRINT
		printf("gestisciAttesa: non ho ricevuto file dal server, reinvio la richiesta\n");
	#endif
	
	if(pthread_cancel(thread_id_chiamante) != 0) {
		perror("gestisciAttesa: errore nella pthread_cancel thread_id_chiamante");
		exit(1);
	}
	if(pthread_create(&dummy, NULL, gestisciRichiesta, (void*)richiesta) != 0) {
		perror("gestisciAttesa: errore nella pthread_create");
		exit(1);
	}
	pthread_exit(NULL);
}

void* ackReceiver(struct arg_ackRecv* arg_ack){ // ------------ ATTESA ACK DAL SERVER ------------  
  
  // ----- ASSEGNAZIONI VARIABILI ------
  int sockReq = arg_ack->socketDescriptor;
  unsigned short* cong_win = arg_ack->cong_win;
  pthread_t* threadIds = arg_ack->threadIds;

  char acked[WINDOWSIZE];           // Vettore di 0/1 per identificare se un pacchetto della cong_win è stato ricevuto dal server o no
  memset(acked, '0', WINDOWSIZE);
  unsigned short send_base = 0;     // Indice primo pacchetto in volo senza ack

  unsigned seqNum;					// Sequence number dell'ack ricevuto
  char* ack_head = (char*) calloc(HEADER_DIM,1);

  while(1){
    if ((recvfrom(sockReq, ack_head, HEADER_DIM, 0, NULL, NULL)) < 0) {
      perror("ackReceiver: Errore in recvfrom");
      exit(1);
    } 
	time_milli = time_milli + bonus;
    seqNum = (*(unsigned*)(&ack_head[3]));           // Numero di pacchetto che è stato riscontrato
    
    #ifdef PRINT
      printf("ackReceiver: ho ricevuto %u primo atteso %u, thread relativo %ld \n", seqNum, send_base, threadIds[seqNum % WINDOWSIZE]);
    #endif
    
    if(threadIds[seqNum % WINDOWSIZE] != 0){  
      pthread_cancel(threadIds[seqNum % WINDOWSIZE]);  // Cancello il thread se esiste
      acked[seqNum % WINDOWSIZE] = '1';                // Imposto l'ack come ricevuto
      threadIds[seqNum % WINDOWSIZE] = 0;              // Tolgo il thread cancellato
    } 
    
    if((seqNum % WINDOWSIZE) == (send_base % WINDOWSIZE)){ // Ho ricevuto il primo pacchetto in volo senza ack
      while(acked[send_base%WINDOWSIZE] == '1'){	// Se il vettore è impostato come "Da ricevere"
        acked[send_base % WINDOWSIZE] = '0';		// Libero l'area 
        send_base += 1;								// Aggiorno il numero del nuovo pacchetto da ricevere 
        
        if (pthread_mutex_lock(&mutexAck)!=0) {	// Per evitare accessi sovrapposti
          perror("ackReceiver: errore nella mutex_lock");
          exit(1);
        }
        *cong_win -= 1;		// Diminuisco la finestra di congestione per far si che si possano inviare pacchetti in rete 
        if (pthread_mutex_unlock(&mutexAck)!=0) {	// Per evitare accessi sovrapposti
          perror("ackReceiver: errore nella mutex_unlock");
          exit(1);
        }
      }
      #ifdef PRINT
        printf("ackReceiver: send_base: %d, congestion window: %d\n",send_base % WINDOWSIZE, *cong_win);
      #endif
    }
  }
  free(ack_head);
}

void* gestisciPacchetto(struct arg_struct* args) {  // Invia un pacchetto a ripetizione interrotto da uno sleep, si ferma quando un
    // thread esterno lo cancella
    char* pack = args->buff; 
    int	sockfd = args->socketDescriptor;

    invio:
    if (pthread_mutex_lock(&mutexScarta)!=0) {	// per evitare accessi sovrapposti
        perror("gestisci pacchetto: errore nella mutex_lock");
        exit(1);
    }
    int decisione = scarta();
    if (pthread_mutex_unlock(&mutexScarta)!=0) {	// per evitare accessi sovrapposti
        perror("gestisci pacchetto: errore nella mutex_unlock");
        exit(1);
    }
    
    if(decisione == 0) {	  // se il pacchetto non va scartato restituisce 0
        if(sendto(sockfd, pack, PACKET_LEN, 0, (const struct sockaddr*)&(args->addr), sizeof(struct sockaddr_in)) < 0) {
            perror("gestisci pacchetto: Errore in sendto");
            exit(1);
        } 

        time_milli = time_milli + malus;
        
        #ifdef PRINT
            printf("gestisci pacchetto: Invio pacchetto %u\n", (*(unsigned*)(&pack[3])));
        #endif
    }
    else {
        #ifdef PRINT
            printf("gestisci pacchetto: Pacchetto scartato %u\n",(*(unsigned*)(&pack[3])));
        #endif
    }
    usleep(timeOutSec*1000);
    #ifdef PRINT
        printf("gestisci pacchetto: timer scaduto, reinvio pacchetto %u\n",(*(unsigned*)(&pack[3])));
    #endif
    goto invio;	// se il timer è scaduto, il thread riprova ad inviare
}

void* gestisciAck(struct arg_struct* args) {
	char* pack = args->buff;
	int	sockfd = args->socketDescriptor;
	
	// Invia al server il pacchetto di richiesta 
	if (pthread_mutex_lock(&mutexScarta)!=0) {	// Per evitare accessi sovrapposti
		perror("gestisci ack: errore nella mutex_lock");
		exit(1);
	}
	int decisione = scarta();
	if (pthread_mutex_unlock(&mutexScarta)!=0) {	// Per evitare accessi sovrapposti
		perror("gestisci ack: errore nella mutex_unlock");
		exit(1);
	}

	if(decisione == 0) {	  // se il pacchetto non viene scartato restituisce 0
		if(sendto(sockfd, pack, HEADER_DIM, 0, (const struct sockaddr*)&(args->addr), sizeof(struct sockaddr_in)) < 0) {
			perror("gestisci ack: Errore in sendto");
			exit(1);
		}
		time_milli = time_milli + malus;
		#ifdef PRINT
            printf("gestisci ack: Invio ACK %u\n", (*(unsigned*)(&pack[3])));
        #endif
	} else {
		#ifdef PRINT
			printf("gestisci ack: ack scartato %u\n",(*(unsigned*)(&pack[3]))); 
		#endif
	}
	pthread_exit(0);
}


void* printFilesName(struct arg_write_print* arg){
	char* vector[WINDOWSIZE];
	
	for(int i = 0 ; i < WINDOWSIZE ; i++){
		vector[i] = arg->payloads_vector[i]; // Vettore con i pacchetti bufferizzati
	} 

	char* fill_vector = arg->payloads_fill_vector; // Vettore di 0 -> libero (atteso), 1 -> occupato (bufferizzato) 
	int ultimo = 0;						// Assume valore uno se ha stampato l'ultimo pacchetto 
	unsigned* rcv_base = arg->rcv_base;	// Contatore di pacchetti scritti

	while(1){
		if(fill_vector[*rcv_base % WINDOWSIZE] == '1'){	// se posso scrivere, ho pacchetti bufferizzati

			// stampo fill_vector
            #ifdef PRINT
                printf("printFiles: ");
                for(int i = 0; i < WINDOWSIZE; i++){
                    printf("%c, ", fill_vector[i]);
                }
                printf("\n");
                printf("printFiles: scrivo il pacchetto %u\n",*rcv_base);
            #endif

			printf("%s",&(vector[(*rcv_base % WINDOWSIZE)][HEADER_DIM]));
			
			// Abbiamo scritto il pacchetto 
			if (pthread_mutex_lock(&mutexBuffFill)!=0) {	// Per evitare accessi sovrapposti
			perror("printFiles: errore nella mutex_lock");
				exit(1);
			}
			fill_vector[*rcv_base % WINDOWSIZE] = '0';      // Libero lo spazio per i successivi pacchetti
			if (pthread_mutex_unlock(&mutexBuffFill)!=0) {	// Per evitare accessi sovrapposti
				perror("printFiles: errore nella mutex_unlock");
				exit(1);
			}
			
			if (pthread_mutex_lock(&mutexBitManipulation)!=0) {	    // Per evitare accessi sovrapposti
				perror("printFiles: errore nella mutex_lock");
				exit(1);
			}
			ultimo = readBit(vector[*rcv_base % WINDOWSIZE][0], 5);
			if (pthread_mutex_unlock(&mutexBitManipulation)!=0) {	// Per evitare accessi sovrapposti
				perror("printFiles: errore nella mutex_unlock");
				exit(1);
			}
			
			if(ultimo == 1){ // verifico se è l'ultimo pack
				#ifdef PRINT
                    printf("printFiles: scritto ultimo packet\n");
                #endif
                printf("\nRichiesta List servita\n\n");
				#ifdef TIME
					gettimeofday(&tv2, NULL);
					long millisec1 = tv1.tv_usec / 1000 + tv1.tv_sec * 1000;
					long millisec2 = tv2.tv_usec / 1000 + tv2.tv_sec * 1000;
					printf("millisecondi trascorsi: %li \n", millisec2 - millisec1);
				#endif
				pthread_exit(0);
			}

			if (pthread_mutex_lock(&mutexRcv_base)!=0) {	    // Per evitare accessi sovrapposti
				perror("printFiles: errore nella mutex_lock");
				exit(1);
			}
			*rcv_base += 1;
			if (pthread_mutex_unlock(&mutexRcv_base)!=0) {	// Per evitare accessi sovrapposti
				perror("printFiles: errore nella mutex_unlock");
				exit(1);
			}
		}
	}
}

void* writeOnFile(struct arg_write_print* arg){
	char* vector[WINDOWSIZE];
	
	for(int i = 0 ; i < WINDOWSIZE ; i++){
		vector[i] = arg->payloads_vector[i]; // Vettore con i pacchetti bufferizzati
	} 

	char* fill_vector = arg->payloads_fill_vector;	 // Vettore di 0 -> libero (atteso), 1 -> occupato (bufferizzato) 
	int ultimo = 0;						// Assume valore uno se ha stampato l'ultimo pacchetto 
	int rtnValue;  						// Valore di ritorno della writeN, per controllare se è andata a buon fine o meno
	pthread_t thread_id_write;
	unsigned* rcv_base = arg->rcv_base;	// Contatore di pacchetti scritti

	struct arg_read_write* args_W = malloc(sizeof(struct arg_read_write)); // Creo l'aria di memoria della struct per i parametri della read 
	args_W->fileDescriptor = arg->fileDescriptor;

	while(1){
		if(fill_vector[*rcv_base % WINDOWSIZE] == '1'){	// se posso scrivere, ho pacchetti bufferizzati

			unsigned size = (*(unsigned short*)(&vector[*rcv_base % WINDOWSIZE][1]));
			args_W->buff = vector[(*rcv_base % WINDOWSIZE)]+HEADER_DIM;
			args_W->nBytes = (size_t)size;

			// stampo fill_vector
            #ifdef PRINT
                printf("writeOnFile: ");
                for(int i = 0; i < WINDOWSIZE; i++){
                    printf("%c, ", fill_vector[i]);
                }
                printf("\n");
                printf("writeOnFile: scrivo il pacchetto %u\n",*rcv_base);
				//printf("%s\n",vector[(*rcv_base % WINDOWSIZE)]+HEADER_DIM);
            #endif

			// Chiamo la readN e controllo il valore di ritorno 
			if(pthread_create(&thread_id_write, NULL, (void *)writeN, args_W) != 0) {
				perror("writeOnFile: errore nella pthread_create");
				exit(1);
			}
			if(pthread_join(thread_id_write,(void*)&rtnValue) != 0) {
				perror("writeOnFile: errore nella pthread_join");
				exit(1);
			}
			
			if(rtnValue == -1){
				perror("writeOnFile: errore in writeN");
				exit(1);
			} else {
				#ifdef PRINT
				    printf("\nwriteOnFile: Finito di scrivere %u\n", *rcv_base);
                #endif
			}
			
			// Abbiamo scritto il pacchetto 
			if (pthread_mutex_lock(&mutexBuffFill)!=0) {	//per evitare accessi sovrapposti
			perror("writeOnFile: errore nella mutex_lock");
				exit(1);
			}
			fill_vector[*rcv_base % WINDOWSIZE] = '0';       // libero lo spazio per i successivi pacchetti
			if (pthread_mutex_unlock(&mutexBuffFill)!=0) {	//per evitare accessi sovrapposti
				perror("writeOnFile: errore nella mutex_unlock");
				exit(1);
			}
			
			if (pthread_mutex_lock(&mutexBitManipulation)!=0) {	    //per evitare accessi sovrapposti
				perror("writeOnFile: errore nella mutex_lock");
				exit(1);
			}
			ultimo = readBit(vector[*rcv_base % WINDOWSIZE][0], 5);
			if (pthread_mutex_unlock(&mutexBitManipulation)!=0) {	//per evitare accessi sovrapposti
				perror("writeOnFile: errore nella mutex_unlock");
				exit(1);
			}
			
			if(ultimo == 1){ // verifico se è l'ultimo pack
				#ifdef PRINT
                    printf("writeOnFile: scritto ultimo packet\n");
                #endif
				close(args_W->fileDescriptor);
                printf("Richiesta Get servita\n\n");

				#ifdef TIME
					gettimeofday(&tv2, NULL);
					long millisec1 = tv1.tv_usec / 1000 + tv1.tv_sec * 1000;
					long millisec2 = tv2.tv_usec / 1000 + tv2.tv_sec * 1000;
					printf("millisecondi trascorsi: %li \n", millisec2 - millisec1);
				#endif

				free(args_W);
				pthread_exit(0);
			}

			if (pthread_mutex_lock(&mutexRcv_base)!=0) {	    //per evitare accessi sovrapposti
				perror("writeOnFile: errore nella mutex_lock");
				exit(1);
			}
			*rcv_base += 1;
			if (pthread_mutex_unlock(&mutexRcv_base)!=0) {	//per evitare accessi sovrapposti
				perror("writeOnFile: errore nella mutex_unlock");
				exit(1);
			}
		}
	}
}

void* writeN(struct arg_read_write* arg){
	int fd = arg->fileDescriptor;
	const void *buf = arg->buff;
	size_t n = arg->nBytes;
	
	size_t nLeft;
	ssize_t nWritten;
	const char *ptr;
	ptr = buf;
	nLeft = n;
	while (nLeft > 0) {
		if ( (nWritten = write(fd, ptr, nLeft)) <= 0) {
			if ((nWritten < 0) && (errno == EINTR)) nWritten = 0;
			else pthread_exit((void*)-1);
		}
		nLeft -= nWritten;
		ptr += nWritten;
	}
	pthread_exit((void*)nLeft);
}

void* readN(struct arg_read_write* args) { 
	
  int fileDescriptor = args->fileDescriptor;
  void *buff = args->buff;
  size_t n = args->nBytes;
  
  size_t nleft = n;
	ssize_t nread;
	char *ptr = (char*)buff;

	while (nleft > 0) {
		if ( (nread = read(fileDescriptor, ptr, nleft)) < 0) {
			if (errno == EINTR)/* funzione interrotta da un segnale prima di averpotuto leggere qualsiasi dato. */
				nread = 0;
			else
				pthread_exit((void*)-1); // errore 
		}
		else if (nread == 0) break; // EOF: si interrompe il ciclo  
		nleft -= nread;
		ptr += nread;
	}  
  
	pthread_exit((void*)nleft); // return >= 0 
}


// Thread che dorme per TIMEOUT_ACK, se si sveglia elimina il thread passato come argomento e se stesso. 
void* timer(pthread_t* thread_id) {
	usleep(TIMEOUT_ACK * 1000);
	#ifdef PRINT
		printf("threadTimer: timer scaduto\n");
	#endif

	if(pthread_cancel(*thread_id) != 0) {
		perror("threadTimer: errore nella pthread_cancel del timer");
		exit(1); 
	}    
	pthread_exit(0);
}

void* timer_close(pthread_t* thread_id) {
    usleep(TIMEOUT_CLOSE*1000*100);
    #ifdef PRINT
        printf("threadTimer: timer terminato, il server ha ricevuto tutti gli ack\n");
    #endif

    if(pthread_cancel(*thread_id) != 0) {
        perror("threadTimer: errore nella pthread_cancel del timer");
        exit(1); 
    }    
    pthread_exit(0);
}

void* time_milli_print(){ //ABCD
	while(1){
		#ifdef TIMER
			printf("time_milli_print: timeOutSec %d TIMEOUT_ACK %d: \n", timeOutSec, TIMEOUT_ACK);
			printf("time_milli_print: millisec %d\n", time_milli);
		#endif
		timeOutSec = timeOutSec + time_milli;
		TIMEOUT_ACK = TIMEOUT_ACK + time_milli;
		TIMEOUT_CLOSE += time_milli;
		if(timeOutSec < 10){
			timeOutSec = 10;
		}
		if(TIMEOUT_ACK < 10){
			TIMEOUT_ACK = 10;
		}
		if(TIMEOUT_CLOSE < 10){
			TIMEOUT_CLOSE = 10;
		}
		time_milli = 0; // una volta aggiornato la riazzero, altrimenti continua a influenzare i timer
		sleep(10);
	}
}
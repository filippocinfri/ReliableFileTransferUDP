/*Struttura del programma 
  main():
    Crea la socket di ascolto e si mette in ricezione di un pacchetto

  thread richiesta:
    Creazione di un thread per ogni richiesta effettuata da client, 
    
  clientRequestManager:
    creazione di una socket per inviare la risposta al client e 
    invio del numero di porta su cui comunicare successivamente, 
    gestione della richiesta del client 
  head: (lunghezza fissa)
    tipo di messaggio (2bit)
    lunghezza contenuto payload (2byte)
    num. offset (4byte)

    pacchetto: (lunghezza fissa)
    payload nel pacchetto: (lunghezza var) */

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/stat.h> //Per la dim del file nella get

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "charToBin.h"
#include "ReadWriteN.h"
#include "libreria.h"

#define SERV_PORT        5193
#define HEADER_DIM       7
#define SHORT_PAY        128
#define LONG_PAY         1024
#define PACKET_LEN       HEADER_DIM + LONG_PAY
#define SHORT_PACK_LEN   HEADER_DIM + SHORT_PAY
#define MAX_SIZE_LEN_FIELD 8 // UNSIGNED LONG 
#define timeOutSec		   500	// secondi per il timeout del timer

#define Pay_len     2 /* secondo e terzo byte all'interno dell'header che descrivono
                        la lunghezza effettiva (da dover leggere) del payload */

struct thread_info {    /* Used as aragument to thread_start() */
  pthread_t thread_id;        /* ID returned by pthread_create() */
  int       thread_num;       /* Application-defined thread # */
  char     *argv_string;      /* From command-line argument */
};

struct arg_struct {
  struct  sockaddr_in addr; //
  char    *buff;            //Buffer dove viene salvata la richiesta del client 
  int		  socketDescriptor;
};

pthread_mutex_t mutexScarta,mutexHeaderPrint;

int  create_thread(char buff[PACKET_LEN], struct sockaddr_in addr);
void listRequestManager(struct arg_struct* arg);
void getRequestManager(struct arg_struct* arg);
void putRequestManager(struct arg_struct* arg);
void* gestisciPacchetto(void* arg);

// MAIN: Creazione Socket - Bind - Attesa richieste - Creazione Thread ogni richiesta
int main(int argc, char **argv) {
  int sockfile;
  socklen_t lenadd = sizeof(struct sockaddr_in);
  struct sockaddr_in addr;
  char buff[SHORT_PACK_LEN];
  int i = 0;
  
  // inizializzo i mutex per controllare accessi alle funzioni ausiliari
  if (pthread_mutex_init(&mutexScarta, NULL) != 0) {
		fprintf(stderr, "main: errore nella mutex_init\n");
  	exit(1);
  }
  if (pthread_mutex_init(&mutexHeaderPrint, NULL) != 0) {
		fprintf(stderr, "main: errore nella mutex_init\n");
  	exit(1);
  }

  if ((sockfile = socket(AF_INET, SOCK_DGRAM, 0)) < 0) { /* crea il socket */
    perror("errore in socket");
    exit(1);
  }

  memset((void *)&addr, 0, lenadd); // Imposta a zero ogni bit dell'indirizzo IP
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY); // Il server accetta pacchetti su una qualunque delle sue interfacce di rete 
  addr.sin_port = htons(SERV_PORT); // numero di porta del server 

  if (bind(sockfile, (struct sockaddr *)&addr, lenadd) < 0) { // assegna l'indirizzo al socket 
    perror("Errore in bind");
    exit(1);
  }
  // Attesa pacchetti dal buffer della socket di ascolto
  while (1) {
    if ( (recvfrom(sockfile, buff, SHORT_PACK_LEN, 0, (struct sockaddr *)&addr, &lenadd)) < 0) {
      perror("Errore in recvfrom\n");
      exit(1);
    }
   
    printf("Ricevuto messaggio da IP: %s e porta: %i\n",inet_ntoa(addr.sin_addr),ntohs(addr.sin_port));
    printf("Contenuto:%s\n", buff);

    /* Creazione Thread per ogni richiesta, poichè abbiamo sicuramente ricevuto qualcosa */
    printf("thread creato: %d \n",create_thread(buff,addr));
  }
  exit(0);
}

//Funzione che inizializza la struttura con i parametri per poi creare effettivamente il thread
int create_thread(char buff[SHORT_PACK_LEN], struct sockaddr_in addr){
  pthread_t tinfo = 0;

  //Leggo i primi due bit del payload:
  char* binary = (char*) malloc(sizeof(char)*(8+1)); 
  charToBinary(buff,binary,1); //Il primo byte del buffer trasformato in 8 char di 0 o 1
  printf("Header in binario: %s, Lunghezza binario: %ld\n", binary, strlen(binary));

  /* Allochiamo memoria per gli argomenti di pthread_create() */
  struct arg_struct* p_arg = malloc(sizeof(struct arg_struct));
  p_arg->addr = addr;
  p_arg->buff = buff;
  //strncpy(p_arg->buff,buff,SHORT_PACK_LEN);

  if (strncmp(binary,"00",2) == 0){ /* ------------ LIST ------------ 
    if ( pthread_create(&tinfo, NULL, (void *)listRequestManager, p_arg)) { 
      printf("pthread_create failed \n");
      exit(1);
    }*/
  }
  else if (strncmp(binary,"01",2) == 0){ /* ------- GET ------- */
   if ( pthread_create(&tinfo, NULL, (void *)getRequestManager, p_arg)) { 
      printf("pthread_create failed \n");
      exit(1);
    }
  }
  else if (strncmp(binary,"10",2) == 0){ /* ------- PUT ------- */
    printf("PUT\n");
    if ( pthread_create(&tinfo, NULL, (void *)putRequestManager, p_arg)) { 
      printf("pthread_create failed \n");
      exit(1);
    }
  }
  return tinfo;
  free(binary);
}

void listRequestManager(struct arg_struct* arg){ 
  
}

void getRequestManager(struct arg_struct* arg){  /* ------- GET ------- */
  //----------------- CREAZIONE NUOVA SOCKET PER NUOVO THREAD -----------------
  int sockReq, n;
  if (( sockReq = socket(AF_INET, SOCK_DGRAM, 0)) < 0) { /* crea socket per nuovo thread */
    perror("errore in socket");
    exit(1);
  }

  arg->socketDescriptor = sockReq;
  
  //----------------- GESTIONE RICHIESTA -----------------
  
  // Apro il file e ne prendo le dimensioni che a ndranno inviate nel payload del primo ack 
  char* fileName = &(arg->buff)[HEADER_DIM];   //Prendo il nome del file richiesto, leggo da header in poi
  char dir_file[] = "./Files/";
  strcat(dir_file, fileName);                  //dir_file ora contiene il path per raggiungere il file richiesto

  struct stat file_stat;
  int file;
  if ((file = open(dir_file, O_RDONLY)) < 0){
    perror("Errore in open");
    exit(1);
  }    

  printf("Path: %s\n",dir_file);

  if (fstat(file, &file_stat) < 0){               // Ottengo le dimensioni del file da inviare
    fprintf(stderr, "Error fstat: %s\n", strerror(errno));
    exit(EXIT_FAILURE);
  }
  unsigned long size = file_stat.st_size;
  printf("Numero caratteri nel file: %lu\n",size); 

  // ------------- INVIO ACK -------------- creo il pacchetto, header + dim lunghezza file da inviare (size)
  
  char* packet = (char*) calloc(SHORT_PACK_LEN,1);
  memset((void*)packet, setBit(packet[0], 6), 1);	// Imposto [01 ... ] secondo bit per la get
  packet[1] = sizeof(unsigned short);             // Imposto nel secondo e terzo byte la lungezza di questo pacchetto 
                                                  // In questo caso è 2B, ed è corretto poichè nel payload ci sarà la dimensione
                                                  // totale del file richiesto, descritto con due bit
  strcat(packet,(char*)size);    // Aggiungo all'header il payload, formando così il pacchetto completo
  arg->buff = packet;                             // Header + len get del file che vogliono
  
  //stampo l'header
	if (pthread_mutex_lock(&mutexHeaderPrint)!=0) {	//per evitare accessi sovrapposti
		fprintf(stderr, "gestore richiesta: errore nella mutex_lock\n");
  	exit(1);
	}
	headerPrint(packet);
	if (pthread_mutex_unlock(&mutexHeaderPrint)!=0) {
		fprintf(stderr, "gestore richiesta: errore nella mutex_unlock\n");
  	exit(1);
	}
  printf("Payload: %lu\n", packet[HEADER_DIM]);

  pthread_t thread_id;	// mantengo ID del thread per poterlo cancellare
  if(pthread_create(&thread_id, NULL, gestisciPacchetto, arg) != 0) {
    fprintf(stderr, "main: errore nella pthread_create\n");
    exit(1);
  }
  // -------------- ATTESA ACK DEL CLIENT ------------  solo header 

  char recvlineShort[HEADER_DIM];                 // Creo buffer che contiene l'header dell'ack del client 
  n = recvfrom(sockReq, recvlineShort , HEADER_DIM, 0, NULL, NULL);
  if (n < 0) {
    perror("Errore in recvfrom\n");
    exit(1);
  } 
  if (n > 0) {
    char head = recvlineShort[0];
    if(readBit(head, 7) == 1 & readBit(head, 6) == 1){
      printf("Ho ricevuto qualcosa\n");
      pthread_cancel(thread_id);              // In gestisci pacchetto
      // *** ricordati controlli cancel***
      #ifdef PRINT
      printf("PRINT: thread cancellato \n");
      #endif
    }
    else{
      //ho ricevuto una risposta != ack, situazione inattesa
    }
  }
  printf("Ack ricevuto\n");
  free(packet);                           // Non devo più inviare ack vero(?)

  // ---- INVIO FILE GET -----
  char payload[LONG_PAY];
  packet = (char*) calloc(PACKET_LEN,1); 
  char header[HEADER_DIM];
  memset((void*)header, setBit(header[0], 6), 1);	  // Imposto [01 ... ] secondo bit per la get

  int i = 0; //La sto utilizzando per vedere quanti pacchetti invio
  while (size > 0){
    // Resettiamo packet e payload
    memset(&packet[HEADER_DIM],0,LONG_PAY);         // Resetto lasciando l'header 
    memset(payload,0,LONG_PAY);                     // Resetto tutto

    if(size <= LONG_PAY){         // Se size è minore del payload, scrivo size byte 
      header[1] = (unsigned short)size;
      readN(file, payload, size);
      size = 0; 

    } else {                      // Se size è maggiore del payload, scrivo payload byte 
      header[1] = (unsigned short)LONG_PAY;
      readN(file, payload, LONG_PAY); 
      size -= LONG_PAY;
    }
    strcat(packet,header);
    strcat(packet+HEADER_DIM,payload);

    //stampo l'header
    if (pthread_mutex_lock(&mutexHeaderPrint)!=0) {	//per evitare accessi sovrapposti
      fprintf(stderr, "gestore richiesta: errore nella mutex_lock\n");
      exit(1);
    }
    headerPrint(header);
    if (pthread_mutex_unlock(&mutexHeaderPrint)!=0) {
      fprintf(stderr, "gestore richiesta: errore nella mutex_unlock\n");
      exit(1);
    }
    printf("num pacchetto inviato: %d \npacchetto:\n%s \nsize = %ld \n", i++, &packet[HEADER_DIM], size);

    if(sendto(sockReq, packet, PACKET_LEN, 0, (const struct sockaddr*)&(arg->addr), sizeof(struct sockaddr_in)) < 0) {
      perror("Errore in sendto invio get");
      exit(1);
    }
  }
  close(file);
  free(packet);
  printf("fine get\n"); // Fine invio get
} 

void putRequestManager(struct arg_struct* arg){
/* ------- PUT ------- */
    int sockReq;
    if(sendto(sockReq, arg->buff /*ACK*/, PACKET_LEN, 0, (const struct sockaddr*)&(arg->addr), sizeof(struct sockaddr_in)) < 0) {
        perror("errore in sendto");
        exit(1);
    }
    //RECIVEFROM?
}

void* gestisciPacchetto(void* arg) {
  struct arg_struct* args = (struct arg_struct*)arg;
  int	sockfd = args->socketDescriptor;
  char*	pack;
  pack = args->buff;
  
  invio:
  /* Invia al server il pacchetto di richiesta */
  pthread_mutex_lock(&mutexScarta);
  int decisione = scarta();
  pthread_mutex_unlock(&mutexScarta);
  if(decisione == 0) {	  // se il pacchetto non va scartato restituisce 0
  	if(sendto(sockfd, pack, SHORT_PACK_LEN, 0, (const struct sockaddr*)&(args->addr), sizeof(struct sockaddr_in)) < 0) {
      perror("Errore in sendto");
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
  //stampo l'header
	if (pthread_mutex_lock(&mutexHeaderPrint)!=0) {	//per evitare accessi sovrapposti
		fprintf(stderr, "gestore richiesta: errore nella mutex_lock\n");
  	exit(1);
	}
	headerPrint(head);
	if (pthread_mutex_unlock(&mutexHeaderPrint)!=0) {
		fprintf(stderr, "gestore richiesta: errore nella mutex_unlock\n");
  	exit(1);
	}

*/

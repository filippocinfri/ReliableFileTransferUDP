#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <string.h>
#include <pthread.h>
#include "libreria.h"

#define	SERV_PORT		5193
#define SHORTPAY		128
#define LONGPAY			1024
#define HEADER_DIM		7
#define	PACK_LEN		LONGPAY+HEADER_DIM
#define SHORT_PACK_LEN	SHORTPAY+HEADER_DIM
#define timeOutSec		12.2	// secondi per il timeout del timer
#define windowSize		4		// dimensione della finestra del selective repeat

struct thread_info {    /* Used as aragument to thread_start() */
  pthread_t thread_id;        /* ID returned by pthread_create() */
  int       thread_num;       /* Application-defined thread # */
  char     *argv_string;      /* From command-line argument */
};

struct arg_struct {
  struct sockaddr_in addr; //
  char *  buff; //Buffer dove viene salvato il packet
};

int create_thread(char buff[SHORT_PACK_LEN], struct sockaddr_in addr);
void clientRequestManager(struct arg_struct* arg);

//IMPOSTARE SETUP INIZIALE

int main(int argc, char **argv) {
  int sockfd;
  socklen_t lenadd = sizeof(struct sockaddr_in);
  struct sockaddr_in addr;
  char buff[SHORT_PACK_LEN];

  if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) { /* crea il socket */
    perror("errore in socket");
    exit(1);
  }

  memset((void *)&addr, 0, lenadd); // Imposta a zero ogni bit dell'indirizzo IP
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY); // Il server accetta pacchetti su una qualunque delle sue interfacce di rete 
  addr.sin_port = htons(SERV_PORT); // numero di porta del server 

  if (bind(sockfd, (struct sockaddr *)&addr, lenadd) < 0) { // assegna l'indirizzo al socket 
    perror("errore in bind");
    exit(1);
  }
  

  char * pack;
  while (1) { //RECVFROM
    pack = (char *)calloc(SHORT_PACK_LEN, 1);
    if ( (recvfrom(sockfd, pack, SHORT_PACK_LEN, 0, (struct sockaddr *)&addr, &lenadd)) < 0) {
      perror("errore in recvfrom\n");
      exit(1);
    }
   
    printf("Ricevuto messaggio da IP: %s e porta: %i\n",inet_ntoa(addr.sin_addr),ntohs(addr.sin_port));
    printf("Ricevuto %s \n", pack+HEADER_DIM);

    /* Creazione Thread per ogni richiesta, poich� abbiamo sicuramente ricevuto qualcosa */
    printf("thread creato: %d \n",create_thread(pack,addr));
  }
  exit(0);
}

int create_thread(char * pack, struct sockaddr_in addr){
  pthread_t tinfo = 0;
  
  /* Allocate memory for pthread_create() arguments */
  struct arg_struct* p_arg = malloc(sizeof(struct arg_struct));
  p_arg->addr = addr;
  p_arg->buff = pack;
  printf("Ricevuto %s \n", p_arg->buff+HEADER_DIM);

  /* The pthread_create() call, stores the thread ID into corresponding element of tinfo[] */
  if ( pthread_create(&tinfo, NULL, (void *)clientRequestManager, p_arg)) { 
    printf("pthread_create failed \n");
    exit(1);
  }
  
  return tinfo;
}

void clientRequestManager(struct arg_struct* arg){ 
  //----------------- CREAZIONE SOCKET -----------------
  int sockReq;
  if (( sockReq = socket(AF_INET, SOCK_DGRAM, 0)) < 0) { /* crea socket per nuovo thread */
    perror("errore in socket");
    exit(1);
  }
  
  struct sockaddr_in c_addr = arg->addr;

  //----------------- GESTIONE RICHIESTA -----------------
  char * packet = arg->buff;
  char firstByteHead = packet[0];
  unsigned short lunghezzaPayload = packet[1];
	printf("%hu %d \n", lunghezzaPayload, lunghezzaPayload);
	if(readBit(firstByteHead, 7) == 0 & readBit(firstByteHead, 6) == 1){
		#ifdef PRINT
		printf("PRINT: letto: get \n");
		#endif
	}
  else{
    //ho ricevuto una risposta != ack, situazione inattesa
    #ifdef PRINT
    printf("PRINT: letto: NON get \n");
    #endif
  }
  pthread_exit(NULL);

  /*
  // creo il socket
  int sockfd;
  struct sockaddr_in clientaddr;
  sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	if ((sockfd) < 0) {			// controllo errori
		perror("errore in socket");
		exit(1);
	}
	memset((void *)&clientaddr, 0, sizeof(clientaddr));	// azzera servaddr
	clientaddr.sin_family = AF_INET;			// assegna il tipo di indirizzo
	clientaddr.sin_port = htons(SERV_PORT);		// assegna la porta del server
	// assegna l'indirizzo del server, preso dalla riga di comando.
	L'indirizzo è una stringa da convertire in intero secondo network byte order.
	if (inet_pton(AF_INET, indirizzoServ, &servaddr.sin_addr) <= 0) {
		// inet_pton (p=presentation) vale anche per indirizzi IPv6
		fprintf(stderr, "errore in inet_pton per %s", indirizzoServ);
		exit(1);
	}
  
  char risposta[MAXLINE];
  while(1){
  	scanf("%s", risposta);
  	printf("risposta inserita: %s \n", risposta);
  	//RISPOSTA AL CLIENT, 3) Rispondere, con l'invio numero di porta (se serve) su cui inviare il file 
  	if(sendto(sockReq, risposta, strlen(risposta), 0, (struct sockaddr_in*)&(arg->addr), sizeof(struct sockaddr_in)) < 0) {
    		perror("errore in sendto");
    		exit(1);
  	}
    if(strncmp(risposta, "stop", 4) == 0){
      break;
    }
  }
  

  free(arg);
  printf("Risposta inviata\n");
  */
}

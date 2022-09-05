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
#define MAX_SIZE_LEN_FIELD 4 // UNSIGNED LONG 
#define timeOutSec		   500	// secondi per il timeout del timer
#define WINDOWSIZE       5
#define TIMEOUT_ACK      5000

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

int  requestControl(struct arg_struct* args);
void listRequestManager(struct arg_struct* args);
void getRequestManager(struct arg_struct* args);
void putRequestManager(struct arg_struct* args);
void* gestisciPacchetto(struct arg_struct* arg);
void* gestisciAck(struct arg_struct* arg);
void* timer(pthread_t* thread_id);
void* threeWay(struct arg_struct* args);

// MAIN: Creazione Socket - Bind - Attesa richieste - Creazione Thread ogni richiesta
int main(int argc, char **argv) {
  int sockfile;
  socklen_t lenadd = sizeof(struct sockaddr_in);
  struct sockaddr_in addr;
  char buff[SHORT_PACK_LEN];
  
  // inizializzo i mutex per controllare accessi alle funzioni ausiliari
  if (pthread_mutex_init(&mutexScarta, NULL) != 0) {
		fprintf(stderr, "main: errore nella mutex_init\n");
  	exit(1);
  }
  if (pthread_mutex_init(&mutexHeaderPrint, NULL) != 0) {
		fprintf(stderr, "main: errore nella mutex_init\n");
  	exit(1);
  }

  if ((sockfile = socket(AF_INET, SOCK_DGRAM, 0)) < 0) { // Crea il socket di ascolto
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
    pthread_t tinfo;
    if ( (recvfrom(sockfile, buff, SHORT_PACK_LEN, 0, (struct sockaddr *)&addr, &lenadd)) < 0) {
      perror("Errore in recvfrom\n");
      exit(1);
    }
   
    printf("Ricevuto messaggio da IP: %s e porta: %i\n",inet_ntoa(addr.sin_addr),ntohs(addr.sin_port));
    
    /* Creazione Thread per ogni richiesta, poichè abbiamo sicuramente ricevuto qualcosa */
    //--------------- Alloco memoria per gli argomenti di pthread_create() -----------------
    struct arg_struct* p_args = malloc(sizeof(struct arg_struct));
    p_args->addr = addr;
    p_args->buff = buff;
    if ( pthread_create(&tinfo, NULL, (void *)requestControl, p_args)  != 0) { 
      printf("Non è stato possibile creare un nuovo thread, la richiesta del Client non verrà servita\n");
    }
    //printf("Thread creato per la verifica della richiesta: %d \n",&tinfo);
  }
  exit(0);
}

// Funzione che effettua i controlli, completa la struttura con i parametri, crea thread per effettuare la connessione.
int requestControl(struct arg_struct* args){
  pthread_t tinfo = pthread_self();
  //----------------- CREAZIONE NUOVA SOCKET PER CONNESSIONE PER LA GESTIONE RICHIESTA (o NACK)-----------------
  int sockRequest;
  if (( sockRequest = socket(AF_INET, SOCK_DGRAM, 0)) < 0) { /* crea socket per nuovo thread */
    perror("Errore nella creazione della socket, la richiesta verrà scartata :( \n");
    exit(1);
  }
  args->socketDescriptor = sockRequest;

  // Leggo i primi due bit dell'header:
  int bit1 = readBit(args->buff[0], 7);
  int bit2 = readBit(args->buff[0], 6);

  if(bit1 == 0 & bit2 == 0) goto threeWay; // Se LIST, vado diretto alla connessione
  
  int nack = 0, file;
  char* packet_nack = (char*) calloc(HEADER_DIM,1);
  char* fileName = &(args->buff)[HEADER_DIM]; // Prendo il nome del file richiesto, leggo da header in poi
  char dir_file[] = "./Files/";
  strcat(dir_file, fileName); // dir_file ora contiene il path per raggiungere il file richiesto

  //------------------------------------------CONTROLLI per GET e PUT ------------------------------------------
  if (bit1 == 0 & bit2 == 1){  // SE GET -> Verifico se il File è presente nel server
    if ((file = open(dir_file, O_RDONLY)) < 0){ // FILE NON PRESENTE -> INVIO UN NACK
      nack = 1; // FILE GIA' PRESENTE, VA INVIATO UN NACK
    }
  }
  else if (bit1 == 1 & bit2 == 0){// SE PUT -> Verifico se un File con lo stesso nome sia già presente nel server
    if ((file = open(dir_file, O_RDONLY)) > 0){
      nack = 1; // FILE GIA' PRESENTE, VA INVIATO UN NACK
    }
  }

  close(file);

  if (nack){      // Invio Nack
    printf("if che ho fatto con solo nack dentro\n");
    pthread_mutex_lock(&mutexScarta);
    int decisione = scarta();
    pthread_mutex_unlock(&mutexScarta);

    if (decisione == 0){
      memset((void*)packet_nack, setBit(packet_nack[0], 6), 1);	// Imposto [11 ... ] 
      memset((void*)packet_nack, setBit(packet_nack[0], 7), 1);
      if(sendto(sockRequest, packet_nack, HEADER_DIM, 0,(const struct sockaddr*) &(args->addr), sizeof(struct sockaddr_in)) < 0) {
        perror("Errore in sendto\n");
        exit(1);
      }
    }
    free(packet_nack);
    close(sockRequest);
    pthread_exit(&tinfo);
  }
  free(packet_nack);

  threeWay:
  // --------------------- Creo Thread per l'instaurazione di connessione ---------------------------
  if ( pthread_create(&tinfo, NULL, (void*)threeWay, args)) { 
    printf("pthread_create failed \n");
    exit(1);
  }
  return tinfo;
}

// Gestione richieste di tipo: LIST
void listRequestManager(struct arg_struct* args){ /* ------- LIST ------- */
 
  int sockReq = args->socketDescriptor;
  struct sockaddr_in c_addr = args->addr; //Salvo indirizzo del client
  char* packet = (char*)calloc((PACKET_LEN),1); // buffer di dimensione long per inviare pacchetti con dati
  
  DIR *folder;
  struct dirent *file;
  folder = opendir("./Files/");

  int payload_lenght;

  // Invio pacchetti contenenti i nomi dei file disponibili
  if (folder) {
    printf("Sono entrato nella cartella per leggere i nomi\n");
    payload_lenght = 0;                        // Contatore di quanto scrivo nel file

    while ((file = readdir(folder)) != NULL) { //Fin quando la cartella è aperta
      if(file->d_type == DT_REG ) {           //Controlla che il file sia di tipo regolare 

        if(payload_lenght + (strlen(file->d_name)+1) <= LONG_PAY ){ //Controlla che ci sia spazio nel buffer
          strcat(packet + HEADER_DIM, file->d_name);
          strcat(packet + HEADER_DIM,"\n");
          payload_lenght += (strlen(file->d_name)+1);
        } 
        else { // Invio pacchetto
          // Creo Header
          (*(unsigned short*)(&packet[1])) = payload_lenght;
          printf("Lunghezza payload nella funzione else(spazio pieno): %hu\n",payload_lenght);

          //stampo l'header
          if (pthread_mutex_lock(&mutexHeaderPrint)!=0) {  //per evitare accessi sovrapposti
            fprintf(stderr, "gestore richiesta: errore nella mutex_lock\n");
            exit(1);
          }
          headerPrint(packet);
          if (pthread_mutex_unlock(&mutexHeaderPrint)!=0) {
            fprintf(stderr, "gestore richiesta: errore nella mutex_unlock\n");
            exit(1);
          }

          printf("Payload: %s\n", packet+HEADER_DIM);

          if(sendto(sockReq, packet, PACKET_LEN, 0, (const struct sockaddr*)&(args->addr), sizeof(struct sockaddr_in)) < 0) {
            perror("Errore in sendto invio");
            exit(1);
          }

          printf("pacchetto inviato\n");
          payload_lenght = strlen(file->d_name)+1; //il nome del file che mi aveva fatto sforare
          strcpy(packet+HEADER_DIM,file->d_name);
          strcat(packet+HEADER_DIM,"\n");
          }
        }
      }
      closedir(folder);
    }

    // Invio del rimanente pacchetto // SET BIT FLAG FINE 
    if (payload_lenght > 0){ //c'è qualcosa da inviare
      (*(unsigned short*)(&packet[1])) = payload_lenght;
      printf("Lunghezza payload: %hu \n",payload_lenght);

      if (pthread_mutex_lock(&mutexHeaderPrint)!=0) {  //per evitare accessi sovrapposti
        fprintf(stderr, "gestore richiesta: errore nella mutex_lock\n");
          exit(1);
      }
      headerPrint(packet);
      if (pthread_mutex_unlock(&mutexHeaderPrint)!=0) {
        fprintf(stderr, "gestore richiesta: errore nella mutex_unlock\n");
          exit(1); 
      }

      printf("Payload: %s\n",  packet+HEADER_DIM);

      if(sendto(sockReq, packet, PACKET_LEN, 0,(const struct sockaddr*) &(args->addr), sizeof(struct sockaddr_in)) < 0) {
        perror("errore in sendto");
        exit(1);
      }
      printf("Inviato ultimo pacchetto\n");
    }
  
  printf("Dovrei aver fatto le cose correttamente, ora spacco, termino, ciao\n");
  free(packet);
}

void getRequestManager(struct arg_struct* args){  /* ------- GET ------- */
  int sockReq = args->socketDescriptor;
  
  //----------------- GESTIONE RICHIESTA -----------------
  // Apro il file e ne prendo le dimensioni che andranno inviate nel payload del primo ack 
  char* fileName = &(args->buff)[HEADER_DIM];   //Prendo il nome del file richiesto, leggo da header in poi
  char dir_file[] = "./Files/";
  strcat(dir_file, fileName);                  //dir_file ora contiene il path per raggiungere il file richiesto

  struct stat file_stat;
  int file;
  if ((file = open(dir_file, O_RDONLY)) < 0){
    // Devo inviare un ack 11 al client 
  }
  printf("Path: %s --- ",dir_file);

  if (fstat(file, &file_stat) < 0){               // Ottengo le dimensioni del file da inviare
    fprintf(stderr, "Error fstat: %s\n", strerror(errno));
    exit(EXIT_FAILURE);
  }
  unsigned long size = file_stat.st_size;
  printf("Numero caratteri nel file: %lu\n",size);                         

  // ---- INVIO FILE GET -----
  char* header = (char*) calloc(HEADER_DIM,1);
  char* payload = (char*) calloc(LONG_PAY,1);
  pthread_t* thread_pck = (pthread_t*) calloc(sizeof(pthread_t),1);	// mantengo ID del thread per poterlo cancellare
  char* packet = (char*) calloc(PACKET_LEN,1); 

  unsigned int numPck = 0;                          // Numero di pacchetti da inviare
  memset((void*)header, setBit(header[0], 6), 1);	  // Imposto [01 ... ] secondo bit per la get

  while (size > 0){                                 
    // Resettiamo packet e payload
    memset(packet,0,PACKET_LEN);                    // Resetto tutto il pacchetto lasciando l'header 
    memset(payload,0,LONG_PAY);                     // Resetto tutto il payload 

    if(size <= LONG_PAY){                             // Se size è minore del payload, scrivo size byte 
      memset((void*)header, setBit(header[0], 5), 1);	// Imposto [011 ... ] terzo bit per il flag ultimo pacchetto
      (*(unsigned short*)(&header[1])) = size;
      readN(file, payload, size);                     // Leggo i rimanenti byte fino a terminare il file
      size = 0; 

    } else {                                          // Se size è maggiore del payload, scrivo payload byte 
      (*(unsigned short*)(&header[1])) = (unsigned short)LONG_PAY;
      readN(file, payload, LONG_PAY); 
      size -= LONG_PAY;
    }
    (*(unsigned*)(&header[3])) = numPck++;           // Indice del numero di pacchetto inviato                          

    strcpy(packet,header);
    strcat(packet+HEADER_DIM,payload);

    //stampo header + pacchetto 
    if (pthread_mutex_lock(&mutexHeaderPrint)!=0) {	//per evitare accessi sovrapposti
      fprintf(stderr, "gestore richiesta: errore nella mutex_lock\n");
      exit(1);
    }
    headerPrint(header);
    if (pthread_mutex_unlock(&mutexHeaderPrint)!=0) {
      fprintf(stderr, "gestore richiesta: errore nella mutex_unlock\n");
      exit(1);
    }
    printf("num pacchetto inviato: %d, size = %ld \npacchetto:\n%s\n", numPck-1, size, packet+HEADER_DIM);

    // Invio il file in gestisciPacchetto
    args->buff = packet;                                             // Salvo in arg->buff tutto il pacchetto da inviare
    if(pthread_create(thread_pck, NULL, (void*)gestisciPacchetto, args) != 0) {
      fprintf(stderr, "main: errore nella pthread_create\n");
      exit(1);
    }

    // -------------- ATTESA ACK DEL CLIENT DEL PACCHETTO ------------  
    printf("Sono in attesa di ack del pck\n");
    int n;
    if (n = recvfrom(sockReq, header, HEADER_DIM, 0, NULL, NULL) < 0) {
      perror("Errore in recvfrom\n");
      exit(1);
    } if (n > 0) {
      printf("qui ci sono\n");
      char head = header[0];
      if(readBit(head, 7) == 1 & readBit(head, 6) == 1){
        if(header[3] == packet[3]){               // Se ho ricevuto l'ack del pacchetto che volevo
          pthread_cancel(*thread_pck);              // Ho ricevuto l'ack e killo il thread figlio, che avrebbe re-inviato il pck altrim. 
          // *** ricordati controlli cancel***
          #ifdef PRINT
          printf("PRINT: thread cancellato \n");
          #endif
        }
      }
      else{
        //ho ricevuto una risposta != ack, situazione inattesa
      }
    }
  }
  close(file);
  free(payload);
  free(packet);
  free(header);
  free(thread_pck);
  printf("fine get\n"); // Fine invio get
} 

void putRequestManager(struct arg_struct* args){ /* ------- PUT ------- */
//----------------- CREAZIONE NUOVA SOCKET PER NUOVO THREAD -----------------
  int sockReq, n;
  if (( sockReq = socket(AF_INET, SOCK_DGRAM, 0)) < 0) { // crea socket per nuovo thread 
    perror("errore in socket");
    exit(1);
  }

  args->socketDescriptor = sockReq;
  
  // ------------- INVIO ACK -------------- creo il pacchetto, header + dim lunghezza file da inviare (size)
  
  char* packet = (char*) calloc(SHORT_PACK_LEN,1);
  memset((void*)packet, setBit(packet[0], 7), 1);	// Imposto [10 ... ] primo bit per la put

  pthread_t thread_id;	// mantengo ID del thread per poterlo cancellare
  if(pthread_create(&thread_id, NULL, (void*)gestisciPacchetto, args) != 0) {
    fprintf(stderr, "main: errore nella pthread_create\n");
    exit(1);
  }

  // -------------- ATTESA ACK DEL CLIENT ------------  solo header 
  n = recvfrom(sockReq, packet, SHORT_PACK_LEN, 0, NULL, NULL);
  if (n < 0) {
    perror("Errore in recvfrom\n");
    exit(1);
  } 
  if (n > 0) {
    char head = packet[0];
    if(readBit(head, 7) == 1 & readBit(head, 6) == 1){
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

  free(packet);
  // ----------------- GESTIONE RICHIESTA -----------------
  packet = (char*) calloc(PACKET_LEN,1);                    // Buffer per ricevere il singolo pacchetto
  char* ackHeader = (char*) calloc(HEADER_DIM,1);           // Buffer per l'ack
  char* buffer = (char*) calloc(WINDOWSIZE,PACKET_LEN);     // Buffer totale, contenenti header+payload di tutti i pacchetti (in ordine)

  memset((void*)ackHeader, setBit(ackHeader[0], 7), 1);	    // Imposto [10 ... ] 
  

  // Apro il file e ne prendo le dimensioni che andranno inviate nel payload del primo ack 
  char* fileName = &(args->buff)[HEADER_DIM];   //Prendo il nome del file richiesto, leggo da header in poi
  char dir_file[] = "./Files/";
  strcat(dir_file, fileName);                  //dir_file ora contiene il path per raggiungere il file richiesto

  struct stat file_stat;
  int file;
  if ((file = open(dir_file, O_CREAT|O_WRONLY|O_APPEND)) < 0){
    perror("Errore in open");
    exit(1);
  }  // Ho appena creato il file richiesto

  while (1){
    int numPacketAttempt = 0;     // Parte da uno, quindi attenzione con gli indici, il primo pacchetto che mi aspetto è con indice 1
    unsigned short size =  0;
    char rcvPacket[WINDOWSIZE];  // Buffer che conterrà i numeri di pacchetti ricevuti fuori ordine ma che non ho scritto

    memset((void *)&rcvPacket, 0, WINDOWSIZE);
    if (n = recvfrom(sockReq, packet , PACKET_LEN, 0, NULL, NULL) < 0) {
      perror("Errore in recvfrom\n");
      exit(1);
    } 
    if (n > 0) {
      char head = packet[0];
      if(readBit(head, 7) == 1 & readBit(head, 6) == 0){  // Controlla che abbia ricevuto un pacchetto [10...]
        if (numPacketAttempt == packet[3]){    // Se il pacchetto arrivato è quello che mi aspettavo
      
          size =  (*(unsigned short*)(&packet[1]));       // Prendo il numero effettivo di B da scrivere 
          writeN(file,&packet[HEADER_DIM], size);
          rcvPacket[numPacketAttempt % WINDOWSIZE] = 0;      
          numPacketAttempt += 1;                          // Aggiorno il numero atteso

          while(rcvPacket[numPacketAttempt % WINDOWSIZE] == 1){        // Controllo che ci sia qualcosa da scrivere 
            strncpy(packet,&buffer[numPacketAttempt % WINDOWSIZE],PACKET_LEN);
            
            size =  (*(unsigned short*)(&packet[1]));       // Prendo il numero effettivo di B da scrivere 
            writeN(file,&packet[HEADER_DIM], size);
            rcvPacket[numPacketAttempt%WINDOWSIZE] = 0;      
            numPacketAttempt += 1;                        // Aggiorno il numero atteso
          }
        } else if (packet[3] > numPacketAttempt){ // Non mi aspettavo il numero di pacchetto arrivato
          buffer[packet[3]%WINDOWSIZE] = packet[HEADER_DIM];
          rcvPacket[packet[3]%WINDOWSIZE] = 1;
        }
        // Ack di quello che mi è arrivato

        pthread_t thread_id;	// mantengo ID del thread per poterlo cancellare
        ackHeader[3] = (*(unsigned*)(&packet[3]));
        args->buff = ackHeader;
        if(pthread_create(&thread_id, NULL, (void*)gestisciPacchetto, args) != 0) {
          fprintf(stderr, "main: errore nella pthread_create\n");
          exit(1);
        }
      }
      else {
        //ho ricevuto una risposta != ack, situazione inattesa
      }
      // Controlla il primo elemento della lista, se c'è va scritto e goto ma come cazzo gli passo le cose
    }
  }
  free(packet);
}

void* threeWay(struct arg_struct* args){
  // ------------- INVIO ACK ACCETTAZIONE RICHIESTA --------------   
  int sockReq = args->socketDescriptor;
  char ack[HEADER_DIM];
  memset(ack,0,HEADER_DIM);
  ack[0] = args->buff[0];
  
  // Invio dell'ack per inizializzare la connessione 
  pthread_mutex_lock(&mutexScarta);
  int decisione = scarta();
  pthread_mutex_unlock(&mutexScarta);
  if(decisione == 0) {	  // se il pacchetto non va scartato restituisce 0
  	if(sendto(sockReq, ack, HEADER_DIM, 0, (const struct sockaddr*)&(args->addr), sizeof(struct sockaddr_in)) < 0) {
      perror("Errore in sendto");
      exit(1);
    }
  }

  // -------------- ATTESA ACK DEL CLIENT PER INIZIARE IL TRASFERIMENTO ------------ 
  pthread_t* thread_id_timer = (pthread_t*)calloc(sizeof(pthread_t), 1);	// mantengo ID del thread per poterlo cancellare
  pthread_t* thread_self = (pthread_t*)pthread_self();
  if(pthread_create(thread_id_timer, NULL, (void*)timer, thread_self) != 0) {   // Attivo il timer, se non ricevo ack chiudo il thread della richiesta
    fprintf(stderr, "main: errore nella pthread_create\n");
    exit(1); 
  }

  int n;
  if (n = recvfrom(sockReq, ack , HEADER_DIM, 0, NULL, NULL) < 0) {
    perror("Errore in recvfrom");
    exit(1);
  } 
  if (n > 0) {
    char head = ack[0];
    if(readBit(head, 7) == 1 & readBit(head, 6) == 1){ 
      pthread_cancel(*thread_id_timer);              // Se ho ricevuto l'ack del client, il timer non scade ed il thread non si chiude 
      // *** ricordati controlli cancel***
      #ifdef PRINT
      printf("PRINT: thread cancellato \n");
      #endif
    }
    else{
      //ho ricevuto una risposta != ack, situazione inattesa
      // Va chiuso
    }
  }
  printf("Ack ricevuto\n");
  free(thread_id_timer);

  // FINE THREEWAY 
  pthread_t tinfo;
  char head = args->buff[0];
  int bit1 = readBit(head, 7);
  int bit2 = readBit(head, 6);

  if (bit1 == 0 & bit2 == 0){ /* ------- LIST --------- */
    if ( pthread_create(&tinfo, NULL, (void *)listRequestManager, args)) { 
      printf("pthread_create failed \n");
      exit(1);
    }
  } else if (bit1 == 0 & bit2 == 1){ /* ------- GET --------- */
    if ( pthread_create(&tinfo, NULL, (void *)getRequestManager, args)) { 
      printf("pthread_create failed \n");
      exit(1);
    }
  }
  else if (bit1 == 1 & bit2 == 0){ /* ------- PUT -------- */
    printf("PUT\n");
    if ( pthread_create(&tinfo, NULL, (void *)putRequestManager, args)) { 
      printf("pthread_create failed \n");
      exit(1);
    }
  }

  pthread_exit(0);
}

void* gestisciPacchetto(struct arg_struct* args) {
  //struct arg_struct* args = (struct arg_struct*)arg;
  int	sockfd = args->socketDescriptor;
  char*	pack;
  pack = args->buff;
  
  invio:
  /* Invia al server il pacchetto di richiesta */
  pthread_mutex_lock(&mutexScarta);
  int decisione = scarta();
  pthread_mutex_unlock(&mutexScarta);
  if(decisione == 0) {	  // se il pacchetto non va scartato restituisce 0
  	if(sendto(sockfd, pack, PACKET_LEN, 0, (const struct sockaddr*)&(args->addr), sizeof(struct sockaddr_in)) < 0) {
      perror("Errore in sendto");
      exit(1);
    }
    //pthread_exit(0); //??
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

void* gestisciAck(struct arg_struct* args) {
  //struct arg_struct* args = (struct arg_struct*)arg;
  int	sockfd = args->socketDescriptor;
  char*	pack;
  pack = args->buff;
  
  /* Invia al server il pacchetto di richiesta */
  pthread_mutex_lock(&mutexScarta);
  int decisione = scarta();
  pthread_mutex_unlock(&mutexScarta);
  if(decisione == 0) {	  // se il pacchetto non va scartato restituisce 0
  	if(sendto(sockfd, pack, HEADER_DIM, 0, (const struct sockaddr*)&(args->addr), sizeof(struct sockaddr_in)) < 0) {
      perror("Errore in sendto");
      exit(1);
    }
  }
  else {
  	#ifdef PRINT
  	printf("PRINT: pacchetto scartato \n"); // il client non usa questa informazione
  	#endif
  }
  pthread_exit(0);
}

void* timer(pthread_t* thread_id) {
  sleep(TIMEOUT_ACK);
  printf("thread Timer: timer scaduto\n");
  pthread_cancel(*thread_id);   
  pthread_exit(0);
}

/* //stampo l'header
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
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

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "libreria.h"

#define SERV_PORT        5193
#define HEADER_DIM       7
#define SHORT_PAY        128
#define LONG_PAY         1024
#define PACKET_LEN       HEADER_DIM + LONG_PAY
#define SHORT_PACK_LEN   HEADER_DIM + SHORT_PAY
#define MAX_SIZE_LEN_FIELD 4 // UNSIGNED LONG 
#define timeOutSec		 500	// secondi per il timeout del timer
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
  int	  socketDescriptor;
};

struct arg_read {
  int fd;
  void *buf;
  size_t n;
};

pthread_mutex_t mutexScarta, mutexControlla, mutexHeaderPrint, mutexBitManipulation;	// mutex per consent. accesso unico a scarta e controlla

void  requestControl(struct arg_struct* args);
void listRequestManager(struct arg_struct* args);
void getRequestManager(struct arg_struct* args);
void putRequestManager(struct arg_struct* args);
void* gestisciPacchetto(struct arg_struct* arg);
void* gestisciAck(struct arg_struct* arg);
void* timer(pthread_t* thread_id);
void threeWay(struct arg_struct* args);
void* readN(struct arg_read* args);

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
void requestControl(struct arg_struct* args){
  pthread_t tinfo = pthread_self();
  //----------------- CREAZIONE NUOVA SOCKET PER CONNESSIONE PER LA GESTIONE RICHIESTA (o NACK)-----------------
  int sockRequest;
  if (( sockRequest = socket(AF_INET, SOCK_DGRAM, 0)) < 0) { /* crea socket per nuovo thread */
    perror("Errore nella creazione della socket, la richiesta verrà scartata :( \n");
    exit(1);
  }
  args->socketDescriptor = sockRequest;

  // Leggo i primi due bit dell'header:
  if (pthread_mutex_lock(&mutexBitManipulation)!=0) {	//per evitare accessi sovrapposti
		fprintf(stderr, "gestore richiesta: errore nella mutex_lock\n");
  	exit(1);
	}
  int bit1 = readBit(args->buff[0], 7);
  int bit2 = readBit(args->buff[0], 6);
  if (pthread_mutex_unlock(&mutexBitManipulation)!=0) {	//per evitare accessi sovrapposti
		fprintf(stderr, "gestore richiesta: errore nella mutex_lock\n");
  	exit(1);
	}

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
    printf("Invio un nack\n");
    pthread_mutex_lock(&mutexScarta);
    int decisione = scarta();
    pthread_mutex_unlock(&mutexScarta);

    if (decisione == 0){
      if (pthread_mutex_lock(&mutexBitManipulation)!=0) {	//per evitare accessi sovrapposti
        fprintf(stderr, "gestore richiesta: errore nella mutex_lock\n");
        exit(1);
      }
      memset(packet_nack, setBit(packet_nack[0], 6), 1);	// Imposto [11 ... ] 
      memset(packet_nack, setBit(packet_nack[0], 7), 1);
      if(sendto(sockRequest, packet_nack, HEADER_DIM, 0,(const struct sockaddr*) &(args->addr), sizeof(struct sockaddr_in)) < 0) {
        perror("Errore in sendto\n");
        exit(1);
      }
      if (pthread_mutex_unlock(&mutexBitManipulation)!=0) {	//per evitare accessi sovrapposti
        fprintf(stderr, "gestore richiesta: errore nella mutex_lock\n");
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
  } else {
    printf("thread creato threeWay\n");
  }

  printf("Passiamo in threeWay\n");
  pthread_exit(0);
}

// Gestione richieste di tipo: LIST
void listRequestManager(struct arg_struct* args){ /* ------- LIST ------- */
 
  int sockReq = args->socketDescriptor;
  char* packet = (char*)calloc(PACKET_LEN, 1); // Buffer di dimensione long per inviare pacchetti con dati
  char* ack = (char*)calloc(HEADER_DIM, 1);

  unsigned int numPck = 0;          // Contatore pacchetti inviati
  unsigned int cong_win = 0;        // Contatore pacchetti in volo
  unsigned int send_base = 0;       // Indice primo pacchetto in volo senza ack
  char acked[WINDOWSIZE];           // Buffer di 0/1 (non riscontrato/riscontrato)
  pthread_t threadIds[WINDOWSIZE];  // Buffer contenente gli Id dei thread creati per inviare i pacchetti
  memset((void*)acked, '0', WINDOWSIZE);
  
  DIR *folder;
  struct dirent *file;
  folder = opendir("./Files/");

  // Invio pacchetti contenenti i nomi dei file disponibili
  if (folder) {
    unsigned short payload_lenght = 0; // Contatore di quanto scrivo nel file
    printf("Sono entrato nella cartella per leggere i nomi\n");

    while(1){ // PE MO NON LO SO
      while(cong_win<WINDOWSIZE & ((file = readdir(folder)) != NULL)){  // Posso inviare dei pacchetti !!!!!!!!!!!!!!!!!! Risolvere loop file piccoli
        if (file->d_type == DT_REG) { // Costruisco UN payload inserendo nomi nel buff fin quando non è pieno, poi invio.
          //Controlla che il file sia di tipo regolare 
          if(payload_lenght + strlen(file->d_name) + 1 <= LONG_PAY ){ //Controlla che ci sia spazio nel buffer
            strcat(packet + HEADER_DIM, file->d_name);
            strcat(packet + HEADER_DIM,"\n");
            payload_lenght += strlen(file->d_name) + 1;
          }
          else {  // ----- INVIO PACCHETTO -----
            printf("Lunghezza payload nella funzione else(spazio pieno): %hu\n",payload_lenght);
            // Creo Header
            (*(unsigned short*)(&packet[1])) = payload_lenght;
            (*(unsigned*)(&packet[3])) = numPck++;           // Indice del numero di pacchetto inviato
            args->buff = packet;
            if(pthread_create(&threadIds[cong_win], NULL, (void*)gestisciPacchetto, args) != 0) {
              fprintf(stderr, "main: errore nella pthread_create\n");
              exit(1);
            }
            numPck ++;
            cong_win ++;
            #ifdef PRINT//Stampo l'header
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
            #endif
            printf("pacchetto inviato\n");
            payload_lenght = strlen(file->d_name) + 1; //il nome del file che mi aveva fatto sforare
            strcpy(packet+HEADER_DIM,file->d_name);
            strcat(packet+HEADER_DIM,"\n");
          }
        }
        // Folder finita !!!!!!!!!!
      }

      // --------------------------- ATTESA ACK DAL CLIENT ------------------------ 
      if (recvfrom(sockReq, ack, HEADER_DIM, 0, NULL, NULL) < 0) {
        perror("Errore in recvfrom\n");
        exit(1);
      }
      unsigned seqNum = (*(unsigned*)(&ack));
      pthread_cancel(threadIds[seqNum % WINDOWSIZE]);  // Cancello il thread relativo al pacchetto che mi è stato riscontrato
      acked[seqNum % WINDOWSIZE] = '1';                // Imposto l'ack come ricevuto
      if(seqNum == (send_base % WINDOWSIZE)){          // Ho ricevuto il primo pacchetto in volo senza ack
        for(int i = 0; acked[i] == '0'; i++ ){              // Devo scorrere send_base fino al primo non riscontrato
          send_base += (send_base+1) % WINDOWSIZE;
          cong_win -= 1;
          acked[i] = '0';
        }
      }
    }
    pthread_exit(0);
  } else { 
    printf("Impossibile accedere alla cartella Files\n");
    free(packet);
    free(ack);
    pthread_exit(0);
  } 
}

void getRequestManager(struct arg_struct* args){  /* ------- GET ------- */
  int sockReq = args->socketDescriptor;
  printf("Inizio get\n");
  //----------------- INIZIALIZZAZIONE RICHIESTA -----------------
  // Apro il file e ne prendo le dimensioni che andranno inviate nel payload del primo ack 
  char* fileName = &(args->buff)[HEADER_DIM];   //Prendo il nome del file richiesto, leggo da header in poi
  char dir_file[] = "./Files/";
  strcat(dir_file, fileName);                  //dir_file ora contiene il path per raggiungere il file richiesto

  struct arg_read* args_R = malloc(sizeof(struct arg_read));
  struct stat file_stat;
  
  int file = open(dir_file, O_RDONLY); // Non controllo se è stato aperto perchè è stato già fatto sopra
  printf("Path: %s --- ",dir_file);

  if (fstat(file, &file_stat) < 0){               // Ottengo le dimensioni del file da inviare
    fprintf(stderr, "Error fstat: %s\n", strerror(errno));
    exit(EXIT_FAILURE);
  }
  unsigned long size = file_stat.st_size;
  printf("Numero caratteri nel file: %lu\n",size);                         

  //----------------- GESTIONE RICHIESTA -----------------
  //pthread_t* thread_pck = (pthread_t*) calloc(sizeof(pthread_t),1);	// mantengo ID del thread per poterlo cancellare
  pthread_t* thread_read = (pthread_t*) calloc(sizeof(pthread_t),1);
  char* packet = (char*) calloc(PACKET_LEN,1);
  char* ack_head = (char*) calloc(HEADER_DIM,1);
  
  unsigned* numPck = calloc(sizeof(unsigned*),1);          // Contatore pacchetti inviati
  unsigned* cong_win = calloc(sizeof(unsigned*),1);        // Contatore pacchetti in volo
  unsigned* send_base = calloc(sizeof(unsigned*),1);       // Indice primo pacchetto in volo senza ack
  
  int rtnValue;
  char acked[WINDOWSIZE];           // Buffer di 0/1 (non riscontrato/riscontrato)
  pthread_t threadIds[WINDOWSIZE];  // Buffer contenente gli Id dei thread creati per inviare i pacchetti
  *numPck = 0;
  args_R->fd = file;

  if (pthread_mutex_lock(&mutexBitManipulation)!=0) {	
    fprintf(stderr, "get: errore nella mutex_lock\n");
    exit(1);
  }
  memset((void*)packet, setBit(packet[0], 6), 1);	// Imposto [01 ... ] terzo bit per il flag ultimo pacchetto
  if (pthread_mutex_unlock(&mutexBitManipulation)!=0) {	
    fprintf(stderr, "get: errore nella mutex_lock\n");
    exit(1);
  }

  while(size > 0 || *cong_win > 0){

    while(*cong_win < WINDOWSIZE & size > 0){  
      printf("Numero caratteri ancora da inviare: %lu\n",size); 
      memset(packet+HEADER_DIM,0,LONG_PAY);          // Resetto tutto il pacchetto lasciando inalterato l'header
      (*(unsigned*)(&packet[3])) = *(numPck);        // Indice del numero di pacchetto inviato
      
      // ----- CREO PACCHETTO DA INVIARE -----
      if(size <= LONG_PAY){                             // Se size è minore del payload, scrivo size byte 
        printf("Num pacchetto %d\n", (*(unsigned*)(&packet[3])));

        if (pthread_mutex_lock(&mutexBitManipulation)!=0) {	
          fprintf(stderr, "get: errore nella mutex_lock\n");
          exit(1);
        }
        memset((void*)packet, setBit(packet[0], 5), 1);	// Imposto [011 ... ] terzo bit per il flag ultimo pacchetto
        if (pthread_mutex_unlock(&mutexBitManipulation)!=0) {	
          fprintf(stderr, "get: errore nella mutex_lock\n");
          exit(1);
        }
        
        (*(unsigned short*)(&packet[1])) = (unsigned short)size;
        args_R->buf = packet+HEADER_DIM;
        args_R->n = size;

        if(pthread_create(thread_read, NULL, (void *)readN, &args_R) != 0) {
          fprintf(stderr, "getRequest: errore nella pthread_create\n");
          exit(1);
        }
        if(pthread_join(*thread_read,(void*)&rtnValue) < 0) {
          fprintf(stderr, "getRequest: errore nella pthread_join\n");
          exit(1);
			  }
        size = 0; 

      } else {                                          // Se size è maggiore del payload, scrivo payload byte 
        (*(unsigned short*)(&packet[1])) = (unsigned short)LONG_PAY;
        args_R->buf = packet+HEADER_DIM;
        args_R->n = LONG_PAY;

        if(pthread_create(thread_read, NULL, (void *)readN, &args_R) != 0) {
          fprintf(stderr, "getRequest: errore nella pthread_create\n");
          exit(1);
        }
        if(pthread_join(*thread_read,(void*)&rtnValue) != 0) {
          fprintf(stderr, "getRequest: errore nella pthread_join\n");
          exit(1);
			  }
        size -= LONG_PAY;
      }
      strncpy(packet+HEADER_DIM,(char*)args_R->buf, (*(unsigned short*)(&packet[1])));
      // ----- INVIO PACCHETTO -----
      args->buff = packet; // Salvo in arg->buff tutto il pacchetto da inviare
      //Controlla che dento arg buff non ci sia qualcosa che sfori nell'ultimo pck
      if(pthread_create(&threadIds[*cong_win], NULL, (void*)gestisciPacchetto, args) != 0) {
        fprintf(stderr, "main: errore nella pthread_create\n");
        exit(1);
      }

      *cong_win += 1;
      *numPck += 1;
    }
    
    // ------------ ATTESA ACK DAL CLIENT ------------  
    if (recvfrom(sockReq, ack_head, HEADER_DIM, 0, NULL, NULL) < 0) {
      perror("Errore in recvfrom\n");
      exit(1);
    } 
    
    unsigned seqNum = (*(unsigned*)(&ack_head[3]));  // Prendo il numero di pacchetto che è stato riscontrato
    printf("\nRicevuto ack del pacchetto %u\n", seqNum);
    pthread_cancel(threadIds[seqNum % WINDOWSIZE]);  // Cancello il thread relativo al pacchetto che mi è stato riscontrato
    acked[seqNum % WINDOWSIZE] = '1';                // Imposto l'ack come ricevuto
    if((seqNum % WINDOWSIZE) == (*send_base % WINDOWSIZE)){          // Ho ricevuto il primo pacchetto in volo senza ack
      for(int i = 0; acked[i] == '0'; i++ ){         // Devo scorrere send_base fino al primo non riscontrato
        *send_base = (*send_base+1) % WINDOWSIZE;
        *cong_win = (*cong_win)-1;
        acked[i] = '0';
      }
    }
    printf("SendBase = %d, seqNum = %d, CongWin %d\n",(*send_base % WINDOWSIZE),seqNum% WINDOWSIZE,*cong_win);
  }
  
  close(file);
  free(send_base);
  free(cong_win);
  free(numPck);
  free(packet);
  free(ack_head);
  free(args_R);
  free(thread_read);
  printf("fine get\n");
  pthread_exit(0);

} 

void putRequestManager(struct arg_struct* args){ /* ------- PUT ------- 
//----------------- CREAZIONE NUOVA SOCKET PER NUOVO THREAD -----------------
  int n;
  int sockReq = args->socketDescriptor;
  
  
  // ----------------- GESTIONE RICHIESTA -----------------
  char* packet = (char*) calloc(PACKET_LEN,1);                    // Buffer per ricevere il singolo pacchetto
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
  pthread_exit(0);*/
}

void threeWay(struct arg_struct* args){
  printf("OK\n");
  // ------------- INVIO ACK ACCETTAZIONE RICHIESTA --------------   
  int sockReq = args->socketDescriptor;
  char ack[HEADER_DIM];
  memset((void*)ack,0,HEADER_DIM);
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
  printf("Invio primo ack per instaurare la connessione\n");

  // -------------- ATTESA ACK DEL CLIENT PER INIZIARE IL TRASFERIMENTO ------------ 
  printf("Creo il timer\n");
  pthread_t* thread_id_timer = (pthread_t*)calloc(sizeof(pthread_t), 1);	// mantengo ID del thread per poterlo cancellare
  pthread_t* thread_self = (pthread_t*)pthread_self();
  if(pthread_create(thread_id_timer, NULL, (void*)timer, thread_self) != 0) {   // Attivo il timer, se non ricevo ack chiudo il thread della richiesta
    fprintf(stderr, "main: errore nella pthread_create\n");
    exit(1); 
  }

  int n;
  if ((n = recvfrom(sockReq, ack , HEADER_DIM, 0, NULL, NULL)) < 0) {
    perror("Errore in recvfrom in ThreeWay");
    exit(1);
  } 
  if (n > 0) {
    char head = ack[0];
    if (pthread_mutex_lock(&mutexBitManipulation)!=0) {	//per evitare accessi sovrapposti
      fprintf(stderr, "gestore richiesta: errore nella mutex_lock\n");
      exit(1);
    }
    
    if(readBit(head, 7) == 1 & readBit(head, 6) == 1){ 
      pthread_cancel(*thread_id_timer);              // Se ho ricevuto l'ack del client, il timer non scade ed il thread non si chiude 
      printf("Timer cancellato\n");
      // *** ricordati controlli cancel***
      #ifdef PRINT
      printf("PRINT: thread cancellato \n");
      #endif
    }
    else{
      //ho ricevuto una risposta != ack, situazione inattesa
      // Va chiuso
    }
    if (pthread_mutex_unlock(&mutexBitManipulation)!=0) {	//per evitare accessi sovrapposti
        fprintf(stderr, "gestore richiesta: errore nella mutex_lock\n");
        exit(1);
    }
  }
  printf("Ack ricevuto\n");
  free(thread_id_timer);

  // FINE THREEWAY 
  pthread_t tinfo;
  char head = args->buff[0];

  if (pthread_mutex_lock(&mutexBitManipulation)!=0) {	//per evitare accessi sovrapposti
		fprintf(stderr, "gestore richiesta: errore nella mutex_lock\n");
  		exit(1);
	}
  int bit1 = readBit(head, 7);
  int bit2 = readBit(head, 6);
  if (pthread_mutex_unlock(&mutexBitManipulation)!=0) {	//per evitare accessi sovrapposti
		fprintf(stderr, "gestore richiesta: errore nella mutex_lock\n");
  		exit(1);
	}

  if (bit1 == 0 & bit2 == 0){ /* ------- LIST --------- */
    printf("list\n");
    if ( pthread_create(&tinfo, NULL, (void *)listRequestManager, args)) { 
      printf("pthread_create failed \n");
      exit(1);
    }
  } else if (bit1 == 0 & bit2 == 1){ /* ------- GET --------- */
    printf("get\n");
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

  char*	pack = (char*)calloc(sizeof(PACKET_LEN),1);
  pack = args->buff;
  int	sockfd = args->socketDescriptor;
  printf("Invio pacchetto numero %u\n",(*(unsigned*)(&pack[3])));
  invio:
  /* Invia al server il pacchetto di richiesta */
  pthread_mutex_lock(&mutexScarta);
  int decisione = scarta();
  pthread_mutex_unlock(&mutexScarta);
  if(decisione == 0) {	  // se il pacchetto non va scartato restituisce 0
  	if(sendto(sockfd, pack, PACKET_LEN, 0, (const struct sockaddr*)&(args->addr), sizeof(struct sockaddr_in)) < 0) {
      perror("Errore in sendto");
      exit(1);
    } else {
      #ifdef PRINT
        if (pthread_mutex_lock(&mutexHeaderPrint)!=0) {	//per evitare accessi sovrapposti
          fprintf(stderr, "gestore richiesta: errore nella mutex_lock\n");
          exit(1);
        }
        headerPrint(pack);
        if (pthread_mutex_unlock(&mutexHeaderPrint)!=0) {
          fprintf(stderr, "gestore richiesta: errore nella mutex_unlock\n");
          exit(1);
        }
        printf("Pacchetto: \n%s",&pack[HEADER_DIM]);
      #endif
      pthread_exit(0);
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

void* readN(struct arg_read* args) { /* (int fd, void *buf, size_t n) */
	
  int fd = args->fd;
  void *buf = args->buf;
  size_t n = args->n;
  
  size_t nleft;
	ssize_t nread;
	char *ptr;

	ptr = buf;
	nleft = n;
	while (nleft > 0) {
		if ( (nread = read(fd, ptr, nleft)) < 0) {
			if (errno == EINTR)/* funzione interrotta da un segnale prima di averpotuto leggere qualsiasi dato. */
				nread = 0;
			else
				pthread_exit((void*)-1); /*errore */
		}
		else if (nread == 0) break;/* EOF: si interrompe il ciclo */
		nleft -= nread;
		ptr += nread;
	}  /* end while */
	pthread_exit((void*)nleft);/* return >= 0 */
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
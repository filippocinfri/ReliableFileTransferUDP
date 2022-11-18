#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/stat.h> //Per la dim del file nella get

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
char dir_file[] = "./Files_Server/"; // Directory della cartella contenente tutti i Files presenti sul server

// mutex per consent. accesso unico a scarta e controlla
pthread_mutex_t mutexScarta, mutexControlla, mutexHeaderPrint, mutexBitManipulation, mutexAck, mutexBuffFill, mutexRcv_base;

// Variabili per il timer adattivo
int time_milli;	//ABCD
int timeOutSec = 3200;  //9200*1000 = 9.2sec //usleep in microsecondi
int TIMEOUT_ACK = 2000;
int TIMEOUT_CLOSE = 3000*100;
int bonus = 20;
int malus = -30;

struct arg_struct {     // Struttura per passare gli argomenti ai thread principali per la gestione della richiesta del client
    struct    sockaddr_in addr; 
    char      *buff;            // Buffer dove viene salvata la richiesta del client 
    int	      socketDescriptor;
};

struct arg_read_write { // Struttura da passare come argomento al thread writeN
    int		fileDescriptor;
    void*	buff;
    size_t	nBytes;
};

struct arg_ackRecv{     // Struttura per passare gli argomenti al thread: ackReceiver
    unsigned short* cong_win;
    pthread_t*  threadIds;
    int	        socketDescriptor;
};

struct arg_write {	    // Struttura da passare come argomento al thread: writeOnFile
    int			fileDescriptor;
    pthread_t	threadChiamante;
    char*		payloads_vector[WINDOWSIZE];
    char*		payloads_fill_vector;
    unsigned* 	rcv_base;
};

void requestControl(struct arg_struct* args);
void threeWay(struct arg_struct* args);

void listRequestManager(struct arg_struct* args);
void getRequestManager(struct arg_struct* args);
void putRequestManager(struct arg_struct* args);

void* gestisciAck(struct arg_struct* arg);
void* gestisciPacchetto(struct arg_struct* arg);
void* ackReceiver(struct arg_ackRecv* arg_ack);

void* readN(struct arg_read_write* args);
void* writeN(struct arg_read_write * arg);
void* writeOnFile(struct arg_write* arg);

void* timer(pthread_t* thread_id);
void* timer_close(pthread_t* thread_id);
void* time_milli_print();

// MAIN: Creazione Socket - Bind - Attesa richieste - Creazione Thread ogni richiesta
int main(int argc, char **argv) {

    pthread_t dummy;
    int sockfile;
    pthread_t tinfo;
    socklen_t lenadd = sizeof(struct sockaddr_in);
    struct sockaddr_in addr;
    struct arg_struct* p_args;

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

    if ((sockfile = socket(AF_INET, SOCK_DGRAM, 0)) < 0) { // Creazione socket di ascolto
        perror("main: errore in socket");
        exit(1);
    }

    memset((void *)&addr, 0, lenadd);         // Imposta a zero ogni bit dell'indirizzo IP
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY); // Il server accetta pacchetti su una qualunque delle sue interfacce di rete 
    addr.sin_port = htons(SERV_PORT);         // Numero di porta del server 

    if (bind(sockfile, (struct sockaddr *)&addr, lenadd) < 0) { // Assegna l'indirizzo alla socket di ascolto
        perror("Main: errore in bind");
        exit(1);
    }

    time_milli = 0; //ABCD
    // attivo il gthread che lo stampa // ABCD
    if(pthread_create(&dummy, NULL, time_milli_print, NULL) != 0) {
        perror("main: errore nella pthread_create");
        exit(1);
    }

    // Attesa pacchetti dal buffer della socket di ascolto
    while (1) {
        
        char *buff = malloc(SHORT_PACK_LEN); // Buffer dove verranno inserite le richeste di ogni client  

        if ((recvfrom(sockfile, buff, SHORT_PACK_LEN, 0, (struct sockaddr *)&addr, &lenadd)) < 0) {
            perror("Main: errore in recvfrom");
            exit(1);
        }
        time_milli = time_milli + bonus;

        // Creazione Thread per ogni richiesta, poichè abbiamo sicuramente ricevuto qualcosa 
        //--------------- Alloco memoria per gli argomenti di pthread_create() -----------------
        p_args = malloc(sizeof(struct arg_struct));
        p_args->addr = addr;
        p_args->buff = buff;

        printf("Main: Ricevuto messaggio da IP: %s e porta: %i\n",inet_ntoa(addr.sin_addr),ntohs(addr.sin_port));
        
        if (pthread_create(&tinfo, NULL, (void *)requestControl, p_args)  != 0) { 
            printf("Main: Non è stato possibile creare un nuovo thread, la richiesta del Client non verrà servita\n");
            // Il server deve rimanere attivo
        }
    }
    exit(0);
}

// Funzione che effettua i controlli, completa la struttura con i parametri, crea thread per effettuare la connessione.
void requestControl(struct arg_struct* args){
    // ----------------- CREAZIONE NUOVA SOCKET PER CONNESSIONE PER LA GESTIONE RICHIESTA (o NACK) -----------------
    pthread_t tinfo;
    int sockRequest;
    if (( sockRequest = socket(AF_INET, SOCK_DGRAM, 0)) < 0) { // Crea socket che gestisce la richiesta
        perror("requestControl: Errore nella creazione della socket, la richiesta verrà scartata");
        exit(1);
    }
    args->socketDescriptor = sockRequest; // Aggiungo il descrittore della socket alla struttura per il prossimo thread

    // Leggo i primi due bit dell'header per controllare la richiesta ricevuta:
    if (pthread_mutex_lock(&mutexBitManipulation)!=0) {	// Per evitare accessi sovrapposti
        perror("requestControl: errore nella mutex_lock");
        exit(1);
    }
    int bit1 = readBit(args->buff[0], 7);
    int bit2 = readBit(args->buff[0], 6);
    if (pthread_mutex_unlock(&mutexBitManipulation)!=0) { // Per evitare accessi sovrapposti
        perror("requestControl: errore nella mutex_unlock");
        exit(1);
    }

    if((bit1 == 0) & (bit2 == 0)) goto threeWay; // Se LIST[00], vado diretto alla connessione 

    // ---------------------------- CONTROLLI per GET e PUT ----------------------------
    int nack = 0, file;
    char* fileName = &(args->buff)[HEADER_DIM]; // Prendo il nome del file richiesto, leggo il payload
    char current_file[sizeof(dir_file)+sizeof(fileName)];
    strcpy(current_file,dir_file);
    strcat(current_file, fileName);

    if ((bit1 == 0) & (bit2 == 1)){  // SE GET [01] -> Verifico se il File è presente nel server
        if ((file = open(current_file, O_RDONLY)) < 0){ 
            nack = 1; // FILE NON PRESENTE -> INVIO UN NACK
            printf("La richiesta non verrà servita: File non presente sul server\n");
        }
        close(file);
    }
    else if (bit1 == 1 & bit2 == 0){ // SE PUT [10] -> Verifico se un File con lo stesso nome sia già presente nel server
        if ((file = open(current_file, O_RDONLY)) >= 0){
            nack = 1; // FILE GIA' PRESENTE, VA INVIATO UN NACK
            printf("La richiesta non verrà servita: File già presente sul server\n");
        }
        close(file);
    }

    if (nack){ // Invio Nack (Per notificare al Client che non è possibile eseguire la sua richiesta)
        #ifdef PRINT
            printf("requestControl: Invio nack poichè impossibile eseguire la richiesta di IP: %s e porta: %i \n",inet_ntoa(args->addr.sin_addr),ntohs(args->addr.sin_port));
        #endif
        free(args->buff);

        char* packet_nack = (char*)calloc(HEADER_DIM,1); 
        int rtnValue;

        if (pthread_mutex_lock(&mutexBitManipulation)!=0) {	// Per evitare accessi sovrapposti
            perror("requestControl: errore nella mutex_lock");
            exit(1);
        }
        memset(packet_nack, setBit(packet_nack[0], 6), 1);	// Imposto [11 ... ] 
        memset(packet_nack, setBit(packet_nack[0], 7), 1);
        if (pthread_mutex_unlock(&mutexBitManipulation)!=0) {	//per evitare accessi sovrapposti
            perror("requestControl: errore nella mutex_unlock");
            exit(1);
        }
        
        args->buff = packet_nack;

        if(pthread_create(&tinfo, NULL, (void*)gestisciAck, args) != 0) {
            perror("requestControl: errore nella creazione del Thread per invio nack");
            exit(1);
        }
        if(pthread_join(tinfo,(void*)&rtnValue) != 0) {
            perror("requestControl: errore nella pthread_join");
            exit(1);
        }
        if(rtnValue != 0){
            perror("requestControl: errore in gestisciAck");
            exit(1);
        }

        close(sockRequest);
        free(args);
        free(packet_nack);
        pthread_exit(0);
    }

    threeWay:
    // --------------------- Creo Thread per l'instaurazione di connessione ---------------------------
    if (pthread_create(&tinfo, NULL, (void*)threeWay, args)) { 
        perror("requestControl di threeWay: pthread_create failed");
        exit(1);
    } else {
        #ifdef PRINT
            printf("requestControl: Inizio ThreeWay con client di IP: %s e porta: %i \n",inet_ntoa(args->addr.sin_addr),ntohs(args->addr.sin_port));
        #endif
    }
    pthread_exit(0);
}

void threeWay(struct arg_struct* args){
    // ------------- INVIO ACK ACCETTAZIONE RICHIESTA -------------- 
    pthread_t tinfo;  
    int sockReq = args->socketDescriptor;
    char* ack = calloc(HEADER_DIM,1);
    ack[0] = args->buff[0];         // Primo byte dell'header, per dire al client di quale ack si tratta (?) 

    struct arg_struct* ackArgs = malloc(sizeof(struct arg_struct)); //Struttura momentanea per l'invio degli ack in fase di connessione.
    ackArgs->socketDescriptor = args->socketDescriptor;
    ackArgs->addr = args->addr;
    ackArgs->buff = ack; //

    // Invio dell'ack per inizializzare la connessione 
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    if(pthread_create(&tinfo, &attr, (void*)gestisciAck, ackArgs) != 0) { // Verrà liberata la struttura ackArgs al termine
        perror("threeWay: errore nella creazione del Thread per invio del primo Ack");
        goto ErrorClose;
    }

    pthread_attr_destroy(&attr);
    

    #ifdef PRINT
        printf("threeWay: Invio primo ack per instaurare la connessione\n");
    #endif  
    // ------------------- ATTESA ACK DEL CLIENT PER INIZIARE IL TRASFERIMENTO ----------------------------
    pthread_t thread_id_timer;  //  ID del thread timer per poterlo cancellare

    if(pthread_create(&thread_id_timer, NULL, (void*)timer, (pthread_t*)pthread_self()) != 0) {
        perror("threeWay: errore nella creazione del thread Timer");
        goto ErrorClose;
    }
    #ifdef PRINT
        printf("threeWay: Timer Avviato-> Attendo Ack dal client\n");
    #endif 

    if ((recvfrom(sockReq, ack , HEADER_DIM, 0, NULL, NULL)) < 0) {
        perror("threeWay: Errore in recvfrom");
        goto ErrorClose;
    }
    
    time_milli = time_milli + bonus;

    if (pthread_mutex_lock(&mutexBitManipulation)!=0) {  // Per evitare accessi sovrapposti
        perror("threeWay: errore nella mutex_lock bitManipulation");
        exit(1);
    }

    if(readBit(ack[0], 7) == 1 & readBit(ack[0], 6) == 1){ // Se ho ricevuto l'ack del client, fermo il timer e chiudo il timer
        if(pthread_cancel(thread_id_timer) != 0) {
            perror("threeWay: errore nella pthread_cancel del timer");
            exit(1); 
        }           
        #ifdef PRINT
            printf("threeWay: thread timer cancellato \n");
        #endif
    }
    else{
        #ifdef PRINT
            printf("threeWay: Ricevuto qualcosa che non è un Ack -> Non dovrebbe succedere");
        #endif
        if (pthread_mutex_unlock(&mutexBitManipulation)!=0) {  //per evitare accessi sovrapposti
            perror("threeWay: errore nella mutex_unlock bitManipulation");
            exit(1);
        }
        goto ErrorClose;
    }

    int bit1 = readBit(args->buff[0], 7);
    int bit2 = readBit(args->buff[0], 6);

    if (pthread_mutex_unlock(&mutexBitManipulation)!=0) {  //per evitare accessi sovrapposti
        perror("threeWay: errore nella mutex_unlock bitManipulation");
        exit(1);
    }

    // FINE THREEWAY 

    if ((bit1 == 0) & (bit2 == 0)){ /* ------- LIST --------- */
        #ifdef PRINT
            printf("List\n");
        #endif
        if (pthread_create(&tinfo, NULL, (void *)listRequestManager, args)) { 
            perror("threeWay: pthread_create list failed");
            goto ErrorClose;
        }
    } else if ((bit1 == 0) & (bit2 == 1)){ /* ------- GET --------- */
        #ifdef PRINT
            printf("Get\n");
        #endif
        if (pthread_create(&tinfo, NULL, (void *)getRequestManager, args)) { 
            perror("threeWay pthread_create get failed");
            goto ErrorClose;
        }
    }
    else if ((bit1 == 1) & (bit2 == 0)){ /* ------- PUT -------- */
        #ifdef PRINT
            printf("Put\n");
        #endif
        if (pthread_create(&tinfo, NULL, (void *)putRequestManager, args)) { 
            perror("threeWay: pthread_create put failed");
            goto ErrorClose;
        }
    }

    free(ackArgs);
    free(ack);    
    pthread_exit(0);

    ErrorClose: 
    printf("threeWay: La richiesta non verrà gestita\n");
    free(ack);
    free(args->buff);
    free(ackArgs);
    free(args);
    pthread_exit(0);
}


void listRequestManager(struct arg_struct* args){ /* ------- LIST ------- */
    // ----- INIZIALIZZAZIONE RICHIESTA -----
    free(args->buff);   // Non utilizziamo il buffer che ci viene inviato (vuoto)

    int sockReq = args->socketDescriptor; 
    struct arg_struct* args_vector[WINDOWSIZE];
    struct arg_ackRecv* arg_ack = malloc(sizeof(struct arg_ackRecv)); // Creo l'aria di memoria della struct per i parametri 
        
    char* packetPointer[WINDOWSIZE]; // Puntatore alle WINDOWSIZE aree di memoria per i pacchetti della finestra di congestione
    
    for (int i = 0 ; i < WINDOWSIZE; i++) {
        packetPointer[i] = calloc(PACKET_LEN,1); // Buffer di dimensione long per inviare pacchetti con dati
        args_vector[i] = malloc(sizeof(struct arg_struct));
        args_vector[i]->addr = args->addr;
        args_vector[i]->socketDescriptor = args->socketDescriptor;
    }
    char* packet = packetPointer[0];
    char last_wind = '0';             // Imposto a 1 se ho terminato i pacchetti da poter inviare (Letto tutto dalla folder)
    unsigned numOfPack = 0;           // Contatore pacchetti inviati
    
    pthread_t* threadIds = calloc(WINDOWSIZE,sizeof(pthread_t)); // Buffer contenente gli Id dei thread creati per inviare i pacchetti
    unsigned short* cong_win = calloc(sizeof(unsigned short),1); // Contatore pacchetti in volo

    // ------------ THREAD ASCOLTA ACK DAL CLIENT ------------  
    pthread_t thread_ack;
    arg_ack->socketDescriptor = args->socketDescriptor;
    arg_ack->cong_win = cong_win;
    arg_ack->threadIds = threadIds;

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    if(pthread_create(&thread_ack, &attr, (void *)ackReceiver, arg_ack) != 0) {
        perror("listRequest: errore nella pthread_create ackReceiver");
        pthread_exit(0);
    }

    pthread_attr_destroy(&attr);

    // ----- GESTIONE RICHIESTA -----
    DIR *folder;
    struct dirent *file;
    folder = opendir(dir_file);

    if (folder) { // Se Cartella aperta con succeso -> Inizia l'invio dei file con Selective Repeat
        #ifdef PRINT
            printf("listRequest: Sono entrato nella cartella per leggere i nomi, cong_win %d\n",*cong_win);
        #endif
        unsigned short payload_lenght = 0;                 // Contatore dei caratteri inseriti nel payload
        while((last_wind == '0') || (*cong_win > 0)){        // Finché la cartella è aperta E la finestra di congestione non è vuota.
            while((*cong_win < WINDOWSIZE) & (last_wind == '0')){ // Posso inviare dei pacchetti -> Selective Repeat

                // Costruisco i payload inserendo nomi dei File nel buff fin quando non è "pieno", poi invio.
                if((file = readdir(folder)) != NULL){ // Vado al File successivo e controllo se è NULL
                    if(file->d_type == DT_REG) {        // Controlla che il file sia di tipo regolare 
                    
                        if(payload_lenght + (strlen(file->d_name)+1) <= LONG_PAY ){ // Controlla che ci sia spazio nel buffer
                            strcat(packet + HEADER_DIM, file->d_name);              // Aggiungo nome file
                            strcat(packet + HEADER_DIM,"\n");
                            payload_lenght += (strlen(file->d_name)+1);             // Aggiorno la dimensione del pacchetto che sto inviando
                        }
                        else {  // ------------------------ INVIO PACCHETTO ------------------------
                            #ifdef PRINT
                                //printf("listRequest: Lunghezza payload nella funzione else(spazio pieno): %hu\n",payload_lenght);
                            #endif
                            
                            repeat1:
                            if(threadIds[(numOfPack % WINDOWSIZE)] != 0){ // Controllo se l'area di memoria che dovrà occupare il thread è libera
                                printf("REPEAT --------------\n"); 
                                goto repeat1;
                            }

                            // Imposto i campi dell' Header
                            (*(unsigned short*)(&packet[1])) = payload_lenght;
                            (*(unsigned*)(&packet[3])) = numOfPack;
                            args_vector[(numOfPack % WINDOWSIZE)]->buff = packet; // Salvo in arg->buff tutto il pacchetto da inviare

                            pthread_attr_t attr;
                            pthread_attr_init(&attr);
                            pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
                            if(pthread_create(&threadIds[(numOfPack % WINDOWSIZE)], &attr, (void*)gestisciPacchetto, args_vector[(numOfPack % WINDOWSIZE)]) != 0) {
                                perror("listRequest: errore nella pthread_create");
                                exit(1);
                            }
                            pthread_attr_destroy(&attr);

                            #ifdef PRINT
                                //printf("listRequest: Inviato pacchetto %u\n", numOfPack);
                                printf("listRequest: #thread: ");
                                for(int i = 0; i < WINDOWSIZE ; i++){
                                    printf("%ld, ", threadIds[i]);
                                }
                                printf("\n");
                                printf("listRequest: NumPack = %d, CongWin %d\n", numOfPack ,*cong_win);
                            #endif
                        
                            numOfPack += 1;

                            if (pthread_mutex_lock(&mutexAck)!=0) {	// per evitare accessi sovrapposti
                                perror("listRequest: errore nella mutex_lock");
                                exit(1);
                            }
                            *cong_win += 1;
                            if (pthread_mutex_unlock(&mutexAck)!=0) {	// per evitare accessi sovrapposti
                                perror("listRequest: errore nella mutex_unlock");
                                exit(1);
                            }
                            
                            packet = packetPointer[numOfPack % WINDOWSIZE]; // Buffer di dimensione long per inviare pacchetti con dati
                            
                            #ifdef PRINT
                                //printf("listRequest: Prossimo da pacchetto da inviare : %u\n", numOfPack);
                            #endif
                            
                            payload_lenght = strlen(file->d_name)+1; // Assegno la lunghezza del nome del file che mi aveva fatto sforare
                            strcpy(packet+HEADER_DIM,file->d_name);  // Inserisco nel pacchetto il nome 
                            strcat(packet+HEADER_DIM,"\n");
                        }
                    }
                }
                else if (payload_lenght > 0) { // C'è qualcosa nel buffer?
                    packet = packetPointer[numOfPack % WINDOWSIZE]; //!!!!!!!!!!!!!!!! Per sicurezza, verificare se necessario !!!!!!!!!!!!!!!!!!!!!!!!!!!!
                    
                    if (pthread_mutex_lock(&mutexBitManipulation)!=0) {	
                        perror("listRequest: errore nella mutex_lock");
                        exit(1);
                    }
                    memset((void*)packet, setBit(packet[0], 5), 1);	// Imposto [011 ... ] terzo bit per il flag ultimo pacchetto
                    if (pthread_mutex_unlock(&mutexBitManipulation)!=0) {	
                        perror("listRequest: errore nella mutex_unlock");
                        exit(1);
                    }
                    
                    repeat2:
                    if(threadIds[(numOfPack % WINDOWSIZE)] != 0){ // Controllo se l'area di memoria che dovrà occupare il thread è libera
                        printf("REPEAT --------------\n"); 
                        goto repeat2;
                    }

                    (*(unsigned short*)(&packet[1])) = payload_lenght;
                    (*(unsigned*)(&packet[3])) = numOfPack;
                    args_vector[(numOfPack % WINDOWSIZE)]->buff = packet; // Salvo in arg->buff tutto il pacchetto da inviare
                    pthread_attr_t attr;
                    pthread_attr_init(&attr);
                    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

                    if(pthread_create(&threadIds[numOfPack % WINDOWSIZE], &attr, (void*)gestisciPacchetto, args_vector[(numOfPack % WINDOWSIZE)]) != 0) {
                        perror("listRequest: errore nella pthread_create");
                        exit(1);
                    }
                    pthread_attr_destroy(&attr);
                    
                    #ifdef PRINT
                        printf("listRequest: Inviato ultimo pacchetto\n");
                    #endif

                    if (pthread_mutex_lock(&mutexAck)!=0) {	// per evitare accessi sovrapposti
                        perror("listRequest: errore nella mutex_lock");
                        exit(1);
                    }
                    *cong_win += 1;
                    if (pthread_mutex_unlock(&mutexAck)!=0) {	// per evitare accessi sovrapposti
                        perror("listRequest: errore nella mutex_unlock");
                        exit(1);
                    } 

                    numOfPack += 1;
                    last_wind = '1'; // Folder finita -> Non ho più nulla da leggere -> non posso più riempire cong_win
                    closedir(folder);
                }
            }
        }
    }
    else {
        #ifdef PRINT
            printf("listRequest: Impossibile accedere alla cartella Files, la richiesta del Client non verrà gestita\n");
        #endif
    }
    for (int i=0 ; i<WINDOWSIZE; i++) { 
        free(packetPointer[i]); 
        free(args_vector[i]);
    };

    if(pthread_cancel(thread_ack) != 0) {
        perror("getRequest: errore nella pthread_cancel dell'ack");
        exit(1); 
    } 
    close(sockReq);
    free(args);
    free(arg_ack);
    free(cong_win);
    free(threadIds);
    printf("Richiesta List servita\n");
    pthread_exit(0);
}

void getRequestManager(struct arg_struct* args){  /* ------- GET ------- */
    //----------------- INIZIALIZZAZIONE RICHIESTA -----------------
    int sockReq = args->socketDescriptor;
    struct arg_read_write* args_R = malloc(sizeof(struct arg_read_write)); // Creo l'aria di memoria della struct per i parametri della read 

    // Creo il path corretto per il raggiungimento del pacchetto che si desidera   
    char* fileName = &(args->buff)[HEADER_DIM];   // Prendo il nome del file richiesto, leggo da header in poi
    char current_file[sizeof(dir_file)+sizeof(fileName)];
    strcpy(current_file, dir_file);
    strcat(current_file, fileName);

    // Apro il file e ne prendo le dimensioni che andranno inviate nel payload del primo ack 
    struct stat file_stat;
    int file = open(current_file, O_RDWR);   // Non controllo se è stato aperto perchè è stato già fatto nella request control
    args_R->fileDescriptor = file;

    if (fstat(file, &file_stat) < 0){          // Ottengo le dimensioni del file da inviare
        fprintf(stderr, "getRequest: Error fstat: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    unsigned long size = file_stat.st_size;
    #ifdef PRINT
        printf("getRequest: Path: %s --- Numero caratteri nel file: %lu\n", current_file, size);                         
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
            perror("getRequest: errore nella mutex_lock");
            exit(1);
        }
        memset((void*)packetPointer[i], setBit(packetPointer[i][0], 6), 1);	// Imposto [01 ... ] secondo bit per la get
        if (pthread_mutex_unlock(&mutexBitManipulation)!=0) {	
            perror("getRequest: errore nella mutex_unlock");
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
        perror("getRequest: errore nella pthread_create ackReceiver");
        exit(1);
    }

    // ----- INVIO FILE CON SELECTIVE -----
    while(size > 0 || *cong_win > 0){             // Finchè c'è qualcosa da inviare || qualcosa da ricevere 
        while(*cong_win < WINDOWSIZE & size > 0){   // Se devo inviare un pacchetto e c'è spazio per farlo
        
        packet = packetPointer[*numPck % WINDOWSIZE]; // Assegno un nuovo buffer per il riempimento 
        memset(packet+1,0,PACKET_LEN-1);              // Resetto tutto il pacchetto, non servirebbe perchè scriviamo e leggiamo con i byte effettivi scritti nell'header
        
        // ----- CREO PACCHETTO DA INVIARE -----
        if(size <= LONG_PAY){ // Ultimo pacchetto da inviare, sono rimasti nel file meno di LONG_PAY 
            if (pthread_mutex_lock(&mutexBitManipulation)!=0) {	
                perror("getRequest: errore nella mutex_lock");
                exit(1);
            }
            memset((void*)packet, setBit(packet[0], 5), 1);	// Imposto [011 ... ] terzo bit per il flag ultimo pacchetto
            if (pthread_mutex_unlock(&mutexBitManipulation)!=0) {	
                perror("getRequest: errore nella mutex_unlock");
                exit(1);
            }
            
            args_R->buff = packet+HEADER_DIM;
            args_R->nBytes = size;
            size = 0; 

        } else {             // Caso in cui posso leggere dal file LONG_PAY
            args_R->buff = packet+HEADER_DIM;
            args_R->nBytes = LONG_PAY;
            size -= LONG_PAY;
        }
        
        // Creo l'header del pacchetto
        (*(unsigned short*)(&packet[1])) = (unsigned short)args_R->nBytes; // Assegno il numero di byte da leggere 
        (*(unsigned*)(&packet[3])) = (unsigned)*numPck;                    // Assegno il numero di pacchetto corrente 

        // Chiamo la readN e controllo il valore di ritorno 
        if(pthread_create(thread_read, NULL, (void *)readN, args_R) != 0) {
            perror("getRequest: errore nella pthread_create");
            exit(1);
        }
        if(pthread_join(*thread_read,(void*)&rtnValue) != 0) {
            perror("getRequest: errore nella pthread_join");
            exit(1);
        }
        if(rtnValue == -1){
            perror("getRequest: errore in readN");
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
            perror("getRequest: errore nella pthread_create");
            exit(1);
        }

        pthread_attr_destroy(&attr);

        if (pthread_mutex_lock(&mutexAck)!=0) {	// per evitare accessi sovrapposti
            perror("getRequest: errore nella mutex_lock");
            exit(1);
        }
        *cong_win += 1;
        if (pthread_mutex_unlock(&mutexAck)!=0) {	// per evitare accessi sovrapposti
            perror("getRequest: errore nella mutex_unlock");
            exit(1);
        }

        #ifdef PRINT
            printf("getRequest: #thread: ");
            for(int i = 0; i < WINDOWSIZE ; i++){
            printf("%ld, ", threadIds[i]);
            }
            printf("\n");
            printf("getRequest: NumPack = %d, CongWin %d\n", *numPck ,*cong_win);
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
        perror("getRequest: errore nella pthread_cancel dell'ack");
        exit(1); 
    }     

    close(file);
    close(sockReq);
    free(cong_win);
    free(numPck);
    free(args_R);
    free(arg_ack);
    free(args);
    free(thread_read);
    free(threadIds);
    printf("Richiesta Get servita\n");
    pthread_exit(0);
} 

void putRequestManager(struct arg_struct* args){ /* ------- PUT ------- */
    
    int	sockfd = args->socketDescriptor; // Descrittore della socket per la recvFrom
	
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
		printf("putRequest: vorrei aprire: %s\n", current_file);
	#endif

	if ((fileptr = open(current_file, O_CREAT|O_RDWR|O_APPEND, 0777)) < 0){
		perror("putRequest: errore in open");
		exit(1);
	}  
	#ifdef PRINT
		printf("putRequest: file aperto\n");
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
	struct arg_write *argomenti = (struct arg_write *)malloc(sizeof(struct arg_write));
	
	for(int i = 0; i < WINDOWSIZE ; i++){   // Inizializzazioni aree di memoria per pacchetti ricevuti e ack da inviare
		payloads_fill_vector[i] = '0';
		packetPointer[i] = (char*)calloc(PACKET_LEN,1);// Buffer di dimensione long per inviare pacchetti con dati
		argomenti->payloads_vector[i] = packetPointer[i];
		
		ack_vector[i] = (char*)calloc(HEADER_DIM,1);   // Buffer di dimensione long per inviare pacchetti con dati
		
		ackPacket = ack_vector[i];  				   // Assegno un nuovo buffer per il riempimento 
		if (pthread_mutex_lock(&mutexBitManipulation) != 0) {	    // per evitare accessi sovrapposti
			perror("putRequest: errore nella mutex_lock");
			exit(1);
		}
		memset(ackPacket, setBit(ackPacket[0], 6), 1);
		memset(ackPacket, setBit(ackPacket[0], 7), 1);	// 11
		if (pthread_mutex_unlock(&mutexBitManipulation) !=0) {	// per evitare accessi sovrapposti
			perror("putRequest: errore nella mutex_unlock");
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
		perror("putRequest: errore nella pthread_create");
		exit(1);
	}

	while(1){
		// Ricevo un pacchetto
		if(recvfrom(sockfd, recevedPack, PACKET_LEN, 0, NULL, NULL) < 0){
			perror("putRequest: errore in recvfrom");
			// Chiudere un sacco di thread e socket? 
			exit(1);
		}
		time_milli = time_milli + bonus;

		// Ho ricevuto un pacchetto correttamente 
		sequenceNumber = (*(unsigned*)(&recevedPack[3])); // Prendo il sequence del pacchetto ricevuto number ricevuto
		#ifdef PRINT
			printf("putRequest: Ho ricevuto %d\n", sequenceNumber);
            #endif
		
		// Imposta pacchetto di ack 
		ackPacket = ack_vector[sequenceNumber % WINDOWSIZE];
		memcpy(ackPacket+3,&sequenceNumber,sizeof(sequenceNumber)); // Scrivo il seqNumber nel pacchetto di ack 
		args_vector[(sequenceNumber % WINDOWSIZE)]->buff = ackPacket;	

		if( (sequenceNumber >= *rcv_base) & (sequenceNumber <= *rcv_base+WINDOWSIZE-1)){
			
			#ifdef PRINT
				//printf("putRequest: finestra [rcv_base,rcv_base+WINDOWSIZE-1]\n");
			#endif
			
			if(payloads_fill_vector[sequenceNumber % WINDOWSIZE] == '0'){	// Se ho spazio nel buffer
				memcpy(packetPointer[sequenceNumber % WINDOWSIZE],recevedPack,PACKET_LEN); // Bufferizzo il pacchetto ricevuto
				
				if (pthread_mutex_lock(&mutexBuffFill)!=0) {	//per evitare accessi sovrapposti
					perror("putRequest: errore nella mutex_lock");
					exit(1);
				}
				payloads_fill_vector[sequenceNumber % WINDOWSIZE] = '1';
				if (pthread_mutex_unlock(&mutexBuffFill)!=0) {	//per evitare accessi sovrapposti
					perror("putRequest: errore nella mutex_unlock");
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

				if (pthread_mutex_lock(&mutexBitManipulation) !=0 ) {	    //per evitare accessi sovrapposti
					perror("putRequest: errore nella mutex_lock");
					exit(1);
				}
				if(readBit(packetPointer[sequenceNumber % WINDOWSIZE][0], 5) == 1) {
					ultimo = 1; 
					last_packet = sequenceNumber;
				}
				if (pthread_mutex_unlock(&mutexBitManipulation)!=0) {	//per evitare accessi sovrapposti
					perror("putRequest: errore nella mutex_unlock");
					exit(1);
				}
			
				if((ultimo == 1) & (*rcv_base == last_packet)){	// Se ho ricevuto l'ultimo pacchetto
					// Mi aspettavo l'ultimo pacchetto, non ho più pacchetti da ricevere
					// Parte un timer, se non ricevo nulla nel frattempo, termino la get
									// Se ricevo un pacchetto, faccio ripartire il timer di nuovo 
					timer:
					if(pthread_create(&thread_id_timer, NULL, (void *)timer_close, (void*)pthread_self()) != 0) {
						perror("putRequest: errore nella pthread_create di timer");
						exit(1);
					} 

					if(recvfrom(sockfd, recevedPack, PACKET_LEN, 0, NULL, NULL) < 0){
						perror("putRequest: errore in recvfrom");
						// Chiudere un sacco di thread e socket? 
						exit(1);
					}
					time_milli = time_milli + bonus;

					// Se ho ricevuto qualcosa
					pthread_cancel(thread_id_timer); 
					printf("putRequest: Timer cancellato, il client non è sicuro di aver inviato tutto\n");

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
				//printf("putRequest: finestra [rcv_base-WINDOWSIZE,rcv_base-1] \n");
				printf("putRequest: invio ack %u \n", sequenceNumber);
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
				printf("putRequest: Ignora, rcv_base %d\n",*rcv_base);
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

void* ackReceiver(struct arg_ackRecv* arg_ack){ // ------------ ATTESA ACK DAL CLIENT ------------  
  
  // ----- ASSEGNAZIONI VARIABILI ------
  int sockReq = arg_ack->socketDescriptor;
  unsigned short* cong_win = arg_ack->cong_win;
  pthread_t* threadIds = arg_ack->threadIds;

  char acked[WINDOWSIZE];           // Vettore di 0/1 per identificare se un pacchetto della cong_win è stato ricevuto dal client o no
  memset(acked, '0', WINDOWSIZE);
  unsigned send_base = 0;     // Indice primo pacchetto in volo senza ack

  unsigned seqNum;                  // Sequence number dell'ack ricevuto
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
    int	sockfd = args->socketDescriptor;
    char* pack = args->buff;

    if (pthread_mutex_lock(&mutexScarta)!=0) {	// Per evitare accessi sovrapposti
        perror("gestisci ack: errore nella mutex_lock\n");
        exit(1);
    }
    int decisione = scarta();
    if (pthread_mutex_unlock(&mutexScarta)!=0) {	// Per evitare accessi sovrapposti
        perror("gestisci ack: errore nella mutex_unlock\n");
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
    }
    else {
        #ifdef PRINT
            printf("gestisci ack: ack scartato \n"); // il client non usa questa informazione
        #endif
    }
    pthread_exit(0);
}


void* writeOnFile(struct arg_write* arg){
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
            #endif

			// Chiamo la writeN e controllo il valore di ritorno 
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
				free(args_W);
                printf("Richiesta Put Servita\n");
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
				pthread_exit((void*)-1); /*errore */
		}
		else if (nread == 0) break;/* EOF: si interrompe il ciclo */  
		nleft -= nread;
		ptr += nread;
	}  /* end while */
  
	pthread_exit((void*)nleft);/* return >= 0 */
}


// Thread che dorme per TIMEOUT_ACK, se si sveglia elimina il thread passato come argomento e se stesso. 
void* timer(pthread_t* thread_id) {
    usleep(TIMEOUT_ACK*1000); 
    #ifdef PRINT
        printf("threadTimer: timer scaduto, non ho ricevuto l'ack\n");
    #endif

    if(pthread_cancel(*thread_id) != 0) {
        perror("threadTimer: errore nella pthread_cancel del timer");
        exit(1); 
    }    
    pthread_exit(0);
}

// Thread che dorme per TIMEOUT_CLOSE, se si sveglia elimina il thread passato come argomento e se stesso. 
void* timer_close(pthread_t* thread_id) {
    usleep(TIMEOUT_CLOSE*1000); // 3s * 100     
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

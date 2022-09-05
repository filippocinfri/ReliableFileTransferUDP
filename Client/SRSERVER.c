// SELECTIVE REPEAT SEND
// creo il buffer in cui metto i pacchetti, e quello in cui scrivo quali spazi sono liberi
char * bufferThread = calloc(WINDOW_SIZE, sizeof(int));
char * bufferFill = calloc(WINDOW_SIZE, 1);
char * sendPacket = calloc(WINDOW_SIZE, PACK_LEN);
	
while(1){
    // se ho qualche pacchetto da inviare
        // finchè ho degli slot liberi
            // invio il pacchetto con gestore pacchetto
        // attendo gli acks
    // altrimenti, se non ho pacchetti da inviare
        // ciclo attendi acks
        // esci dal while

    
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
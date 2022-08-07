/* daytime_serverUDP.c - code for example server program that uses UDP 
   Ultima revisione: 14 gennaio 2008 */

#include <sys/types.h> 
#include <sys/socket.h> 
#include <arpa/inet.h>

#include <unistd.h> 
#include <stdio.h> 
#include <stdlib.h>
#include <time.h>
#include <string.h>

#define SERV_PORT   5193 
#define MAXLINE     1024

int main(int argc, char **argv) {
  int sockfd;
  socklen_t len=sizeof(struct sockaddr_in);
  struct sockaddr_in addr;
  char buff[MAXLINE];
  time_t ticks;

  if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) { /* crea il socket */
    perror("errore in socket");
    exit(1);
  }

  memset((void *)&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY); /* il server accetta pacchetti su una qualunque delle sue interfacce di rete */
  addr.sin_port = htons(SERV_PORT); /* numero di porta del server */

  /* assegna l'indirizzo al socket */
  if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("errore in bind");
    exit(1);
  }

  while (1) {
    if ( (recvfrom(sockfd, buff, MAXLINE, 0, (struct sockaddr *)&addr, &len)) < 0) {
      perror("errore in recvfrom");
      exit(1);
    }

    ticks = time(NULL); /* legge l'orario usando la chiamata di sistema time */
    /* scrive in buff l'orario nel formato ottenuto da ctime; snprintf impedisce l'overflow del buffer. */
    snprintf(buff, sizeof(buff), "%.24s\r\n", ctime(&ticks)); /* ctime trasforma la data e l’ora da binario in ASCII. \r\n per carriage return e line feed*/
    /* scrive sul socket il contenuto di buff */
    if (sendto(sockfd, buff, strlen(buff), 0, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
      perror("errore in sendto");
      exit(1);
    }
  }
  exit(0);
}

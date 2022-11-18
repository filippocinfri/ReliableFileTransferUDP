/* funzione ausiliare per determinare se il pacchetto va scartato*/
/* ritorna 1 se il pacchetto va scartato, altrimenti 0 */

// STRUTTURA
/*	leggo 2 byte randomici da dev/urandom
	divido i due byte per il massimo unsigned short int, cos�
	da ottenere un numero compreso tra 0 e 1
	se il numero � < probPerdita allora scarto il pacchetto*/

#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <limits.h>
#include <time.h>

#define	probPerdita	0.15


int scarta2(void){
  int ifd;
  unsigned short int randomNum;
  
  // open the input file and check errors
  ifd = open("/dev/urandom",O_RDONLY);
  if (ifd == -1) {
  	printf("SCARTA: /dev/urandom open error\n");
  	exit(1);
  }
  
  /* read up to BUFSIZE from input file and check errors */
  if (read(ifd, &randomNum,2) == -1) {
  	printf("SCARTA: read error\n");
  	exit(1);
  }
  
  /* #ifdef PRINT
    printf("PRINT: randomNum:%hu percentuale:%f p:%f \n", randomNum, (double)randomNum/(double)USHRT_MAX,probPerdita);
  #endif */
  return ((double)randomNum/(double)USHRT_MAX < probPerdita);
}

int scarta(void) {
  srand(time(NULL));   // Initialization, should only be called once.
  int r = rand();      // Returns a pseudo-random integer between 0 and RAND_MAX
  float f = (float)r / (float)RAND_MAX;
  
  /*#ifdef PRINT
    printf("scarta: r: %d percentuale: %f, %d \n", r, f, (f < probPerdita));
  #endif */
  if (f < probPerdita){
    return 1;
  }
  return 0;
}

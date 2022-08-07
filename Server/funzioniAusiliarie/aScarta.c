/* funzione ausiliare per determinare se il pacchetto va scartato*/
/* ritorna 1 se il pacchetto va scartato, altrimenti 0 */

// ? verificare se è thread safe

// STRUTTURA
/* genero un intero tramite rand();
per ottenere un numero compreso tra 0 e 1 lo divido per RAND_MAX.
Confronto il risultato con la probabilità di perdità,
in questo modo riesco a scartare i pacchetti con la probabilità prefissata */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define	probPerdita	0.2

int scarta(void)
{
  double value;
  srand(time(NULL));	// inizializzo
  value = rand();	// estrae un numero pseudo-casuale
  
  //printf("p:%f v:%f rMAX:%d  ris:%f\n", probPerdita, value, RAND_MAX, value/(double)RAND_MAX);
  
  if(value/(double)RAND_MAX < probPerdita){
  	return 1;
  }
  return 0;
}

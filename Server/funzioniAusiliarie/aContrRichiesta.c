/* funzione ausiliare, confronta richiesta del client con richieste ammesse */
/* ritorna 0 se la stringa non è ammessa, >0 altrimenti */

// ? verificare se è thread safe

#include <string.h>
 
#define	MAXLINE		1024

int controllaRichiesta(char richiesta[MAXLINE])
{
  if(strcmp(richiesta, "list") == 0){
  	return 1;
  }
  if(strncmp(richiesta, "put_", 4) == 0){
  	return 2;
  }
  if(strncmp(richiesta, "get_", 4) == 0){
  	return 3;
  }
  return 0;
}

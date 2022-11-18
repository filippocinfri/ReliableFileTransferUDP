/* funzione ausiliare, confronta richiesta del client con richieste ammesse */
/* ritorna 0 se la stringa non è ammessa, > 0 altrimenti */

// non è thread safe

#include <string.h>

int controllaRichiesta(char* richiesta)
{
  if(strcmp(richiesta, "list") == 0){
  	return 1;
  }
  if(strncmp(richiesta, "get_", 4) == 0){
  	return 2;
  }
  if(strncmp(richiesta, "put_", 4) == 0){
  	return 3;
  }
  return 0;
}

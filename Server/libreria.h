/*  */

#define MAXLINE 1024


ssize_t writeN(int fd, const void *buf, size_t n);
// spiegazione

int readN(int fd, void *buf, size_t n);
// spiegazione

int controllaRichiesta(char richiesta[MAXLINE]);
// spiegazione

int scarta(void);
// per determinare se il pacchetto va scartato

char setBit(char n, int k);

int readBit(char n, int k);


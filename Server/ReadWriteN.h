/*  */

#include <errno.h>
#include <unistd.h>

ssize_t writeN(int fd, const void *buf, size_t n);
int readN(int fd, void *buf, size_t n);

ssize_t writeN(int fd, const void *buf, size_t n){
	size_t nLeft;
	ssize_t nWritten;
	const char *ptr;
	ptr = buf;
	nLeft = n;
	while (nLeft > 0) {
		if ( (nWritten = write(fd, ptr, nLeft)) <= 0) {
			if ((nWritten < 0) && (errno == EINTR)) nWritten = 0;
			else return(-1); /* errore */
		}
		nLeft -= nWritten;
		ptr += nWritten;
	} /* end while */
	return(nLeft);
}

int readN(int fd, void *buf, size_t n) {
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
				return(-1); /*errore */
		}
		else if (nread == 0) break;/* EOF: si interrompe il ciclo */
		nleft -= nread;
		ptr += nread;
	}  /* end while */
	return(nleft);/* return >= 0 */
}

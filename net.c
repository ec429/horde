#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>

#include "bits.h"
#include "libhorde.h"

int main(int argc, char **argv)
{
	const char *name=argc?argv[0]:"net";
	int newhandle;
	struct sockaddr remote;
	socklen_t addr_size=sizeof(remote);
	if((newhandle=accept(3, (struct sockaddr *)&remote, &addr_size))==-1)
	{
		fprintf(stderr, "%s[%d]: ", name, getpid());
		perror("accept");
		printf("(fin %d)\n", EXIT_FAILURE);
		return(EXIT_FAILURE);
	}
	fprintf(stderr, "%s[%d]: accepted\n", name, getpid());
	fprintf(stderr, "%s[%d]: closing conn\n", name, getpid());
	close(newhandle);
	printf("(fin %d)\n", EXIT_SUCCESS);
	return(EXIT_SUCCESS);
}

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include "global.h" 
#include "md5.h"

void error(const char *msg)
{
    perror(msg);
    exit(0);
}
#if 0
int main(int argc, char *argv[])
{
    int sockfd, portno, n;
    struct sockaddr_in serv_addr;
    struct hostent *server;

    char buffer[256];
    portno = 2113;
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) 
        error("ERROR opening socket");
    server = gethostbyname("localhost");
    if (server == NULL) {
        fprintf(stderr,"ERROR, no such host\n");
        exit(0);
    }
    bzero((char *) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    bcopy((char *)server->h_addr, 
         (char *)&serv_addr.sin_addr.s_addr,
         server->h_length);
    serv_addr.sin_port = htons(portno);
    if (connect(sockfd,(struct sockaddr *) &serv_addr,sizeof(serv_addr)) < 0) 
        error("ERROR connecting");
    bzero(buffer,256);
		strcpy(buffer, "REQSTATS\n");
    //fgets(buffer,255,stdin);
    n = write(sockfd,buffer,strlen(buffer));
    if (n < 0) 
         error("ERROR writing to socket");
    bzero(buffer,256);
    n = read(sockfd,buffer,255);
    if (n < 0) 
         error("ERROR reading from socket");
    printf("%s\n",buffer);
    close(sockfd);
    return 0;
}
#elif 1
int connectToServer(int sockfd, char* ip, int port ){
	struct sockaddr_in serv_addr;
	struct hostent *server;
	
    server = gethostbyname(ip);
    if (server == NULL) {
        fprintf(stderr,"ERROR, no such host\n");
        exit(0);
    }
	
	bzero((char *) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    bcopy((char *)server->h_addr, 
         (char *)&serv_addr.sin_addr.s_addr,
         server->h_length);
    serv_addr.sin_port = htons(port);

	if (connect(sockfd,(struct sockaddr *) &serv_addr,sizeof(serv_addr)) < 0){
		error("ERROR connecting");
		return 1;
	}
	return 0;
}

char* sendMessage( char* message, int sockfd){

	int n;
	char* response = (char*)malloc(256*sizeof(char));
	
	n = write(sockfd,message,strlen(message));
   
 	if (n < 0)
  	error("ERROR writing to socket");

 	bzero(response,256);
	n = read(sockfd,response,255);
  if (n < 0) 
  	error("ERROR reading from socket");
	
	return response;
}

/* Prints a message digest in hexadecimal.
 */
static void MDPrint (unsigned char digest[16]) {
  unsigned int i;

  for (i = 0; i < 16; i++)
 printf ("%02x", digest[i]);
}

/* Digests a string and prints the result.
 */
static void MDString (char* string) {
  MD5_CTX context;
  unsigned char digest[16];
  unsigned int len = strlen (string);

  MD5Init (&context);
  MD5Update (&context, string, len);
  MD5Final (digest, &context);

  printf ("MD%d (\"%s\") = ", 5, string);
  MDPrint (digest);
  printf ("\n");
}

int main(int argc, char *argv[]) {
	int sockfd = socket(AF_INET, SOCK_STREAM, 0);
	char* ip = strdup("localhost");
	int port = 2113;	
	char* message = strdup("REQSTATS\n");
	MDString(message);
	if(connectToServer(sockfd, ip, port)) return 1;
	message = sendMessage(message, sockfd);
  printf("%s\n",message);
  close(sockfd);
	return 0;
}
#endif

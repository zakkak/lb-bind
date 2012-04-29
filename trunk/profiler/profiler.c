#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <openssl/evp.h>

void error(const char *msg)
{
    perror(msg);
    exit(0);
}

void print2hex(unsigned const char* string, int size) {
	int i;
  for(i = 0; i < size; i++) printf("%02x", string[i]);
  printf("\n");
}

unsigned char *md5_digest(const char *input) {
	EVP_MD_CTX mdctx;
  const EVP_MD *md;
  //char input[] = "REQSTATS";
  unsigned char* output = (unsigned char*)malloc(sizeof(unsigned char)*16);
  int output_len, i;

  /* Initialize digests table */
  OpenSSL_add_all_digests();
  md = EVP_get_digestbyname("MD5");

  if(!md) {
  	printf("Unable to init MD5 digest\n");
    exit(1);
  }

  EVP_MD_CTX_init(&mdctx);
  EVP_DigestInit_ex(&mdctx, md, NULL);
  EVP_DigestUpdate(&mdctx, input, strlen(input));
  /* to add more data to hash, place additional calls to EVP_DigestUpdate here */
  EVP_DigestFinal_ex(&mdctx, output, &output_len);
  EVP_MD_CTX_cleanup(&mdctx);

  /* Now output contains the hash value, output_len contains length of output, which is 128 bit or 16 byte in case of MD5 */
	return output;
}

int check_response(char *response) {
	char *timestamp;
	double stats[3];
	char *message = strtok(response, "#");
	char *msg_digest = strtok(NULL, "#");
	int i;	
	//printf("message=%s\n", message);
	//printf("?=%s", msg_digest);
	//print2hex(msg_digest, 16);
	char *digest = md5_digest(message);
	//print2hex(digest, 16);
	for(i=0; i < 16; i++) {
		if(msg_digest[i] != digest[i])
			return -1;
	}
	printf("checksum OK!\n");
	stats[0] = atof(strtok(message, "$"));
	for(i=1; i < 3; i++) {
		stats[i] = atof(strtok(NULL, "$"));
	}
	timestamp = strtok(NULL, "$");	
	printf("io usages=%lf, cpu usage=%lf, network traffic=%lf\n", stats[0], stats[1], stats[2]);
	printf("timestamp=%s\n", timestamp);	
	return 0;
}

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

char* sendMessage(char* orig_message, int sockfd){

	int n;
	char* response = (char*)malloc(256*sizeof(char));
	char* digest = md5_digest(orig_message);
	char message[256];
	bzero(message, 256);
	strcpy(message, orig_message);
	strcat(message, "#");
	strcat(message, digest);
	n = write(sockfd,message,strlen(message));
   
 	if (n < 0)
  	error("ERROR writing to socket");

 	bzero(response,256);
	n = read(sockfd,response,255);
  if (n < 0) 
  	error("ERROR reading from socket");
	
	return response;
}

int main(int argc, char *argv[]) {
	int sockfd = socket(AF_INET, SOCK_STREAM, 0);
	char* ip = strdup("localhost");
	int port = 2113;	
	char *response, *message = strdup("REQSTATS");
	//MDString("REQSTATS");
	if(connectToServer(sockfd, ip, port)) return 1;
	response = sendMessage(message, sockfd);
	check_response(response);
  //printf("%s\n",message);
  close(sockfd);
	free(message);
	free(response);
	return 0;
}


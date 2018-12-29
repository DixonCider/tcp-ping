#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>

void ping_pong(int sockfd, char *client_ip, int client_port) {
	int numbytes, MAXDATASIZE = 100;
	char buf[MAXDATASIZE];
	while(1) {
		if ((numbytes=recv(sockfd, buf, MAXDATASIZE, 0)) <= 0) {
			break;
		}
		// buf[numbytes] = '\0';
		// printf("Received from pid=%d, text=: %d numbytes %d \n",getpid(), *buf, numbytes);
		printf("Received from %s:%d\n", client_ip, client_port);
		if (send(sockfd, buf, numbytes, 0) == -1) {
			break;
		}
		// sleep(1);
	}
}

int main (int argc, char *argv[]) {
	const char* hostname=0; /* wildcard */
	/*
	char hostname[1024];
	hostname[1023] = '\0';
	gethostname(hostname, 1023);
	*/
	if (argc < 2) {
		printf("Require specify port number\n");
		exit(1);
	}
	const char* portname=argv[1];

	struct addrinfo hints;
	memset(&hints,0,sizeof(hints));
	hints.ai_family=AF_UNSPEC;
	hints.ai_socktype=SOCK_STREAM;
	hints.ai_protocol=0;
	hints.ai_flags=AI_PASSIVE|AI_ADDRCONFIG;
	struct addrinfo* res=0;

	int err=getaddrinfo(hostname,portname,&hints,&res);
	if (err!=0) {
		printf("failed to resolve local socket address (err=%d)",err);
	}
#ifdef DEBUG
	else {
		printf("Success create local socket address\n");
		for(struct addrinfo *p = res; p != NULL; p = p->ai_next) {
			printf("hostname: %s\n", p->ai_canonname);
		}
	}
#endif

	int server_fd=socket(res->ai_family,res->ai_socktype,res->ai_protocol);
	if (server_fd==-1) {
		printf("%s",strerror(errno));
		exit(1);
	}
#ifdef DEBUG
	else {
		printf("server_fd = %d\n", server_fd);
	}
#endif

	int reuseaddr=1;
	if (setsockopt(server_fd,SOL_SOCKET,SO_REUSEADDR,&reuseaddr,sizeof(reuseaddr))==-1) {
		printf("%s",strerror(errno));
		exit(1);
	}

	if (bind(server_fd,res->ai_addr,res->ai_addrlen)==-1) {
		printf("%s",strerror(errno));
		exit(1);
	}
#ifdef DEBUG
	else {
		printf("Success binded socket and server\n");
	}
#endif
	freeaddrinfo(res);
	if (listen(server_fd,SOMAXCONN)) {
		printf("failed to listen for connections (errno=%d)",errno);
		exit(1);
	}
	else {
		printf("Start listening ...\n");
	}
	for (;;) {
		struct sockaddr_storage sa;
		socklen_t sa_len=sizeof(sa);
		if (sa.ss_family==AF_INET6) {
			struct sockaddr_in6* sa6=(struct sockaddr_in6*)&sa;
			if (IN6_IS_ADDR_V4MAPPED(&sa6->sin6_addr)) {
				struct sockaddr_in sa4;
				memset(&sa4,0,sizeof(sa4));
				sa4.sin_family=AF_INET;
				sa4.sin_port=sa6->sin6_port;
				memcpy(&sa4.sin_addr.s_addr,sa6->sin6_addr.s6_addr+12,4);
				memcpy(&sa,&sa4,sizeof(sa4));
				sa_len=sizeof(sa4);
			}
		}
		char buffer[INET6_ADDRSTRLEN];
		int err=getnameinfo((struct sockaddr*)&sa,sa_len,buffer,sizeof(buffer),0,0,NI_NUMERICHOST);
		if (err!=0) {
			snprintf(buffer,sizeof(buffer),"invalid address");
		}
		struct sockaddr_in *sin = (struct sockaddr_in *) &sa;

		int session_fd=accept(server_fd,(struct sockaddr*)&sa,&sa_len);
		if (session_fd==-1) {
			if (errno==EINTR) continue;
			printf("failed to accept connection (errno=%d)",errno);
			exit(1);
		}
		char *client_ip = inet_ntoa(sin->sin_addr);
		int client_port = ntohs(sin->sin_port);
#ifdef DEBUG
		else {
			printf("Connection accepted\n");
		}
#endif
		pid_t pid=fork();
		if (pid==-1) {
			printf("failed to create child process (errno=%d)",errno);
			exit(1);
		}
		else if (pid==0) {
			close(server_fd);
			ping_pong(session_fd, client_ip, client_port);
			// handle_session(session_fd);
			close(session_fd);
			_exit(0);
		} 
		else {
			close(session_fd);
		}
	}
	return 0;
}



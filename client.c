#include <stdio.h> 
#include <stdlib.h> 
#include <errno.h> 
#include <inttypes.h>
#include <string.h> 
#include <netdb.h> 
#include <sys/types.h> 
#include <netinet/in.h> 
#include <sys/socket.h> 
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/time.h>
#include <limits.h>
#include <signal.h>
#include <wait.h>

#define PORT 1024    /* the port client will be connecting to */

#define MAXDATASIZE 100 /* max number of bytes we can get at once */
#define MAXPINGITERATION 10000
#define MAXIPLENGTH 100
#define MAXSERVERPING 100

int pingloop = 1;
int procs[MAXSERVERPING] = {0};
int procs_n = 0;

struct ping_instance {
	int sockfd;
	int prev_message;
	struct sockaddr_in their_addr;
	struct ping_instance *next;
	struct ping_record *pr;
};

struct ping_record {
	struct timeval start;
	int message;
	struct ping_record *next;
};

void die (char *err) {
	printf("%s\n", err);
	exit(1);
}

void send_ping (int iter_n, struct ping_instance *pi) {
	struct ping_record *pr = malloc(sizeof(struct ping_record));
	pr->message = iter_n;
	gettimeofday(&(pr->start), NULL);
	if (send(pi->sockfd, &iter_n, 1, 0) == -1){
		printf("Error sending message\n");
	}
	pr->next = pi->pr;
	pi->pr = pr;
#ifdef DEBUG
	printf("start %lu\n", pr->start.tv_usec);
#endif
}

void receive_ping (int timeout_n, struct ping_instance *pi) {
	char buf[MAXDATASIZE];
	int numbytes = recv(pi->sockfd, buf, MAXDATASIZE, MSG_DONTWAIT);
	struct timeval stop, start;
	gettimeofday(&stop, NULL);
#ifdef DEBUG
	printf("numbytes %d\n", numbytes);
#endif

	if (numbytes == -1) return;

	int received_message = (int)buf[0];
#ifdef DEBUG
	printf("received_message %d\n", *buf);
#endif

	struct ping_record *pr = pi->pr;
	int message_in_queue = 0;
	while (pr != NULL) {
		if (pr->message == received_message) {
#ifdef DEBUG
			printf("start %lu %lu\n", pr->start.tv_usec, pr->start.tv_usec/1000);
#endif
			int ms = stop.tv_usec - pr->start.tv_usec;
			// int ms = (stop.tv_usec / 1000) - (pr->start.tv_usec / 1000);
			if (ms > timeout_n) {
				printf("timeout when connect to %s:%d\n", 
						inet_ntoa(pi->their_addr.sin_addr),
						ntohs(pi->their_addr.sin_port));
			}
			else {
				printf("recv from %s:%hu, RTT = %d msec\n", 
						inet_ntoa(pi->their_addr.sin_addr), 
						ntohs(pi->their_addr.sin_port), 
						ms);
			}
			message_in_queue = 1;
			break;
		}
		pr = pr->next;
	}
#ifdef DEBUG
	if (!message_in_queue) printf("message stacked\n");
	printf("receive\n");
#endif
}

void ping (int iter_n, int timeout_n, struct ping_instance *pi) {
	int numbytes;
	char buf[MAXDATASIZE];
	struct timeval stop, start;

	gettimeofday(&start, NULL);

	if (send(pi->sockfd, &iter_n, 1, 0) == -1){
		perror("send");
		exit (1);
	}

	numbytes=recv(pi->sockfd, buf, MAXDATASIZE, 0);
	gettimeofday(&stop, NULL);

	// None in the socket queue.
	if (numbytes == -1) return;

	int ms = (stop.tv_usec / 1000) - (start.tv_usec / 1000);
	if (ms > timeout_n) {
		printf("timeout when connect to %s:%d\n", 
				inet_ntoa(pi->their_addr.sin_addr),
				ntohs(pi->their_addr.sin_port));
	}
	else {
		printf("recv from %s:%hu, RTT = %d msec\n", 
				inet_ntoa(pi->their_addr.sin_addr), 
				ntohs(pi->their_addr.sin_port), 
				ms);
	}

#ifdef DEBUG
	printf("buf %d\n", *buf);
#endif
}

struct ping_instance* initialize_ping (char *ip, int port_n) {
	
	struct ping_instance *pi = malloc(sizeof(struct ping_instance));
	pi->pr = NULL;
	/*
	int sockfd, numbytes; 
	char buf[MAXDATASIZE];
	struct sockaddr_in their_addr; // connector's address information
	*/

	if ((pi->sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		perror("socket");
		exit(1);
	}

	struct hostent *he;
	he = gethostbyname(ip);

	pi->their_addr.sin_family = AF_INET;      /* host byte order */
	pi->their_addr.sin_port = htons(port_n);    /* short, network byte order */
	if (he == NULL) {
		pi->their_addr.sin_addr = *((struct in_addr *)ip);
	}
	else {
		pi->their_addr.sin_addr = *((struct in_addr *)he->h_addr);
	}
	bzero(&(pi->their_addr.sin_zero), 8);     /* zero the rest of the struct */

	if (connect(pi->sockfd, (struct sockaddr *)&(pi->their_addr), \
				sizeof(struct sockaddr)) == -1) {
		perror("connect");
		exit(1);
	}
	return pi;
}

void intHandler(int dummy) {
	for (int i = 0; i < procs_n; ++i) {
		kill(procs[i], SIGTERM);
	}
	exit(1);
}
int main(int argc, char *argv[])
{
	signal(SIGINT, intHandler);
	int iter_n = -1, timeout_n = 1000;
	struct ping_instance *pi;
	pid_t p;
	// Parsing.
	for (int i = 1; i < argc; ++i) {
		// printf("%s\n", argv[i]);
		if (strcmp(argv[i], "-n") == 0) {
			++i;
			iter_n = strtoimax(argv[i], 0, 10);
		}
		else if (strcmp(argv[i], "-t") == 0) {
			++i;
			timeout_n = strtoimax(argv[i], 0, 10);
		}
		else {
			int j = 0;
			while (argv[i][j] != ':') ++j;
			argv[i][j] = '\0';
			char *ip = argv[i];
			int port_n = strtoimax(&argv[i][j + 1], 0, 10);

			pid_t p = fork();
			if (p == -1) printf("error forking\n");
			else if (p == 0) {
				pi = initialize_ping(ip, port_n);
				send_ping(iter_n, pi);
				--iter_n;
				sleep(1);
				while (iter_n--) {
					receive_ping(timeout_n, pi);
					send_ping(iter_n, pi);
					sleep(1);
				}
				receive_ping(timeout_n, pi);
				close(pi->sockfd);
				_exit(0);
			}
			else {
				procs[procs_n] = p;
				++procs_n;
			}
		}
	}
	int status = 0;
	while (wait(&status) > 0);
	return 0;
}

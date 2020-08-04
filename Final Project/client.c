#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h> 
#include <sys/time.h>
#include <errno.h>

#define SIZE 2048

int main(int argc, char* argv[])
{
	int opt;
	char* ip;
	int port, node1, node2;

	// Check argument amount
	if(argc != 9)
	{
		printf("This usage is wrong. \n");
		printf("Usage: ./client -a 127.0.0.1 -p PORT -s NUMBER -d NUMBER\n");
		exit(EXIT_FAILURE);
	}

	// I used the getopt() library method for parsing commandline arguments.
	while((opt = getopt(argc, argv, "apsd")) != -1)
	{
		switch(opt)
		{
			case 'a':
				ip = argv[optind];
				break;
			case 'p':
				port = atoi(argv[optind]);
				if(argv[optind][0] != '0' && port == 0)
					port = -1;
				break;
			case 's':
				node1 = atoi(argv[optind]);
				if(argv[optind][0] != '0' && node1 == 0)
					node1 = -1;
				break;
			case 'd':
				node2 = atoi(argv[optind]);
				if(argv[optind][0] != '0' && node2 == 0)
					node2 = -1;
				break;
			default:
			// If the command line arguments are missing/invalid your program must print usage information and exit.
				printf("This usage is wrong. \n");
				printf("Usage: ./client -a 127.0.0.1 -p PORT -s NUMBER -d NUMBER\n");
				exit(EXIT_FAILURE);
		}
	}
	// Check values
	if(port < 0)
	{
		printf("-p(number of port) must be positive.\n");
		exit(EXIT_FAILURE);
	}

	if(node1 < 0)
	{
		printf("-s(source node of the requested path) must not negative.\n");
		exit(EXIT_FAILURE);
	}
	if(node2 < 0)
	{
		printf("-d(destination node of the requested path) must not negative.\n");
		exit(EXIT_FAILURE);
	}

	int socket1 = 0, size = 0; 
	struct sockaddr_in serverAddr; 
	char bufread[SIZE] = {' '};
	char bufwrite[SIZE] = {' '};
	struct timeval start, end;

	sprintf(bufwrite, "%d-%d", node1, node2);

	pid_t pid = getpid();
	printf("Client (%d) connecting to %s:%d\n", pid, ip, port);

	// Create socket file descriptor 
	if((socket1 = socket(AF_INET, SOCK_STREAM, 0)) < 0) 
	{
		fprintf(stderr, "\nerrno = %d: %s Socket creation error\n\n", errno, strerror(errno));
		exit(EXIT_FAILURE);
	}

	serverAddr.sin_family = AF_INET; 
	serverAddr.sin_port = htons(port);

	// Convert IPv4 and IPv6 addresses from text to binary form
	if(inet_pton(AF_INET, ip, &serverAddr.sin_addr)<=0)  
	{
		fprintf(stderr, "\nerrno = %d: %s Invalid address/ Address not supported\n\n", errno, strerror(errno));
		exit(EXIT_FAILURE);
	}

	if(connect(socket1, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0) 
	{
		fprintf(stderr, "\nerrno = %d: %s Connection Failed \n\n", errno, strerror(errno));
		exit(EXIT_FAILURE);
	}

	printf("Client (%d) connected and requesting a path from node %d to %d\n", pid, node1, node2);
	// Send nodes to server
	send(socket1, bufwrite, strlen(bufwrite), 0);

	// Calculate response time
	gettimeofday(&start, NULL);
	// Take result from server
	size = read(socket1, bufread, SIZE);
	gettimeofday(&end, NULL);
	long seconds = (end.tv_sec - start.tv_sec);
	long micros = ((seconds * 1000000) + end.tv_usec) - (start.tv_usec);
	double diff_t = (double) micros / 1000000;

	// Server response
	if(bufread[0] == 'N' || size == 0)
		printf("Server's response to (%d) : NO PATH, arrived in %lfseconds, shutting down\n", pid, diff_t);
	else
		printf("Server's response to (%d) : %s, arrived in %lfseconds.\n", pid, bufread, diff_t);

	close(socket1);

	return 0;
}
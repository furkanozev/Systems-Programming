#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/time.h>
#include <signal.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <pthread.h>
#include <sys/stat.h>

#define SIZE 2048

typedef struct node{
	int item;
	int index;
	struct node *next;
}node;

typedef struct{
	node *front;
	node *rear;
}Queue;

// For graph, Adjacency list (queue) is kept in queue structure.
typedef struct{
	int size;
	int edges;
	int nodes;
	int* nodelist;
	int a, b, c;
	Queue** adjList;
}Graph;

// For cache, paths found by Bfs are kept in the list.
// Indices with paths are kept in a separate list.
// Thus, the desired path is directly reached.
typedef struct{
	int sizea, sizeb, sizec;
	int count;
	Queue** paths;
	Queue** index;
}Catch;

// Queue functions.
void enqueue(Queue *q, int item);
void enqueue2(Queue *q, int item, int index);
int dequeue(Queue *q);
int empty(Queue *q);

// Initialize graph
void initGraph(Graph* graph);
// Initialize catch
void initCatch(Catch* catch, int a, int b, int c);

// Add an edge to graph
void addEdge(Graph* graph, int x, int y);
// Search item in queue
int existInList(Queue *q, int num);
// Search item in array that is in graph
int existInArray(Graph* graph, int num);

// Create daemon process
int becomeDaemon();
// Write message to log file
void printLog(char* str);

// It will load a graph from a text file
int loadGraph(Graph* graph, FILE* fd1);
// Breadth First Search (BFS) to find a path from i1 to i2
int bfs(Graph* graph, int x, int y, int* resArr);

// deallocate memory
void freeGraph();
void freeCatch();
void freeOther();
void freeAll();

// Signal handler
void sigHandler(int sig);

// Pool of POSIX threads
void *thread_function(void *args);
// For coordinate dynamic Pool
void *thread_coord_pool(void *args);

Graph graph1;
Catch catch1;
pthread_t* thread_id;
pthread_t thread_pool;
int** indexThread = NULL;
int threadNumber = 0, maxThread = 0;
Queue ports;

// mutex1 is used to synchronization for pool threads and main thread
pthread_mutex_t mutex1 = PTHREAD_MUTEX_INITIALIZER;
// mutex2 is used to reader-writer paradigm for pool threads
pthread_mutex_t mutex2 = PTHREAD_MUTEX_INITIALIZER;
// mutex3 is used to synchronization for main thread and dynamic coordinate thread
pthread_mutex_t mutex3 = PTHREAD_MUTEX_INITIALIZER;
// tmutex is used to forward connection to pool threads
pthread_mutex_t tmutex = PTHREAD_MUTEX_INITIALIZER;
// sendmutex is used to send message via ports for pool threads
pthread_mutex_t sendmutex = PTHREAD_MUTEX_INITIALIZER;
// printmutex is used to print message to log filer for all threads
pthread_mutex_t printmutex = PTHREAD_MUTEX_INITIALIZER;

// cempty and cfull condition variable for main thread and pool threads.
// For simple producer - consumer synchronization
pthread_cond_t cempty = PTHREAD_COND_INITIALIZER;
pthread_cond_t cfull = PTHREAD_COND_INITIALIZER;

// pempty and pfull condition variable for main thread and dynamic coordinate thread.
// For simple producer - consumer synchronization
pthread_cond_t pempty = PTHREAD_COND_INITIALIZER;
pthread_cond_t pfull = PTHREAD_COND_INITIALIZER;

// tempty and tfull condition variable for main thread and pool threads.
// For simple producer - consumer synchronization, for forward connection to pool threads
pthread_cond_t tempty = PTHREAD_COND_INITIALIZER;
pthread_cond_t tfull = PTHREAD_COND_INITIALIZER;

// Global counters
int pcount = 0, pcount2 = 0, pcount3 = 0, tcount = 0;

// For reader - writer paradigm
int AR = 0, AW = 0, WR = 0, WW = 0;
pthread_cond_t okToRead = PTHREAD_COND_INITIALIZER;
pthread_cond_t okToWrite = PTHREAD_COND_INITIALIZER;

int flagint = 0, socket1 = -1;
int fd, fd2;

int main(int argc, char* argv[])
{
	// Measures against double instantiation
	// When the program starts, it creates a temporary file.
	// If a second server tries to open this file, it will get an error.
	// Thus, double instantiation will be prevented.
	fd = open("daemonLock", O_CREAT | O_EXCL);
	if(fd == -1)
	{
		fprintf(stderr, "\nIt should not be possible to start 2 instances of the server process\n\n");
		exit(EXIT_FAILURE);
	}

	int opt;
	char* filePath;
	char* logPath;
	int port;
	int i;
	struct timeval start, end;
	char logbuf[SIZE*2] = {' '};
	// Check argument amount
	if(argc != 11)
	{
		unlink("daemonLock");
		close(fd);
		fprintf(stderr, "This usage is wrong. \n");
		fprintf(stderr, "Usage: ./server -i pathToFile -p PORT -o pathToLogFile -s NUMBER -x NUMBER\n");
		exit(EXIT_FAILURE);
	}

	// I used the getopt() library method for parsing commandline arguments.
	while((opt = getopt(argc, argv, "iposx")) != -1)
	{
		switch(opt)
		{
			case 'i':
				filePath = argv[optind];
				break;
			case 'p':
				port = atoi(argv[optind]);
				if(argv[optind][0] != '0' && port == 0)
					port = -1;
				break;
			case 'o':
				logPath = argv[optind];
				break;
			case 's':
				threadNumber = atoi(argv[optind]);
				if(argv[optind][0] != '0' && threadNumber == 0)
					threadNumber = -1;
				break;
			case 'x':
				maxThread = atoi(argv[optind]);
				if(argv[optind][0] != '0' && maxThread == 0)
					maxThread = -1;
				break;
			default:
			// If the command line arguments are missing/invalid your program must print usage information and exit.
				unlink("daemonLock");
				close(fd);
				fprintf(stderr, "This usage is wrong. \n");
				fprintf(stderr, "Usage: ./server -i pathToFile -p PORT -o pathToLogFile -s NUMBER -x NUMBER\n");
				exit(EXIT_FAILURE);
		}
	}
	// Check values
	if(port < 0)
	{
		unlink("daemonLock");
		close(fd);
		fprintf(stderr, "-p(number of port) mustn't be negative.\n");
		exit(EXIT_FAILURE);
	}

	if(threadNumber < 2)
	{
		unlink("daemonLock");
		close(fd);
		fprintf(stderr, "-s(number of threads) can be at least 2.\n");
		exit(EXIT_FAILURE);
	}

	if(threadNumber > maxThread)
	{
		unlink("daemonLock");
		close(fd);
		fprintf(stderr, "-s(number of threads) must not bigger than -x(maximum allowed number of threads)\n");
		exit(EXIT_FAILURE);
	}
	
	if (becomeDaemon() == -1)
	{
		unlink("daemonLock");
		close(fd);
		fprintf(stderr, "\nerrno = %d: %s becomeDaemon failed\n\n", errno, strerror(errno));
		exit(EXIT_FAILURE);
	}

	fd2 = open(logPath, O_RDWR | O_CREAT | O_TRUNC, 0666);
	if(fd2 == -1)
	{
		unlink("daemonLock");
		close(fd);
		fprintf(stderr, "errno = %d: %s open logPath\n\n", errno, strerror(errno));
		exit(EXIT_FAILURE);
	}
	
	// Open files
	FILE* fd1 = fopen(filePath, "r");
	if(fd1 == NULL)
	{
		sprintf(logbuf, "errno = %d: %s open filePath\n\n", errno, strerror(errno));
		printLog(logbuf);
		unlink("daemonLock");
		close(fd);
		exit(EXIT_FAILURE);
	}

	ports.front = NULL;
	ports.rear = NULL;

	// Handler for signals
	signal(SIGINT, sigHandler);
	signal(SIGSEGV, sigHandler);

	sprintf(logbuf, "Executing with parameters:");
	printLog(logbuf);
	sprintf(logbuf, "-i %s", filePath);
	printLog(logbuf);
	sprintf(logbuf, "-p %d", port);
	printLog(logbuf);
	sprintf(logbuf, "-o %s", logPath);
	printLog(logbuf);
	sprintf(logbuf, "-s %d", threadNumber);
	printLog(logbuf);
	sprintf(logbuf, "-x %d", maxThread);
	printLog(logbuf);

	initGraph(&graph1);
	sprintf(logbuf, "Loading graph...");
	printLog(logbuf);

	// It will load a graph from a text file
	// Calculate processing time
	gettimeofday(&start, NULL);
	if(loadGraph(&graph1, fd1) == -1)
	{
		freeAll();
		unlink("daemonLock");
		close(fd);
		exit(EXIT_FAILURE);
	}
	gettimeofday(&end, NULL);
	long seconds = (end.tv_sec - start.tv_sec);
	long micros = ((seconds * 1000000) + end.tv_usec) - (start.tv_usec);
	double time_taken = (double) micros / 1000000;
	sprintf(logbuf, "Graph loaded in %lf seconds with %d nodes and %d edges.", time_taken, graph1.nodes, graph1.edges);
	printLog(logbuf);

	initCatch(&catch1, graph1.a, graph1.b, graph1.c);
	thread_id = (pthread_t*) calloc(threadNumber, sizeof(pthread_t));
	indexThread = (int**) calloc(threadNumber, sizeof(int*));

	pthread_mutex_lock(&mutex1);
	// Create pool threads. Thread take number.
	for(i = 0; i < threadNumber; i++)
	{
		indexThread[i] = malloc(sizeof(int));
		*(indexThread[i]) = i;
		if(pthread_create(&(thread_id[i]), NULL, &thread_function, indexThread[i]) != 0)
		{
			sprintf(logbuf, "errno = %d: %s pthread_create\n", errno, strerror(errno));
			printLog(logbuf);
			freeAll();
			unlink("daemonLock");
			close(fd);
			exit(EXIT_FAILURE);
		}
	}
	// Create dynamic coordinate thread
	if(pthread_create(&thread_pool, NULL, &thread_coord_pool, NULL) != 0)
	{
		sprintf(logbuf, "errno = %d: %s pthread_create\n", errno, strerror(errno));
		printLog(logbuf);
		freeAll();
		unlink("daemonLock");
		close(fd);
		exit(EXIT_FAILURE);
	}
	
	sprintf(logbuf, "A pool of %d threads has been created", threadNumber);
	printLog(logbuf);
	pthread_mutex_unlock(&mutex1);

	struct sockaddr_in serverAddr;
	int socket2;
	int addrlen = sizeof(serverAddr);
	int opt1 = 1;

	// Create socket file descriptor 
	if((socket1 = socket(AF_INET, SOCK_STREAM, 0)) == 0) 
	{
		sprintf(logbuf, "errno = %d: %s socket failed\n", errno, strerror(errno));
		printLog(logbuf);
		freeAll();
		unlink("daemonLock");
		close(fd);
		exit(EXIT_FAILURE); 
	} 

	// Forcefully attaching socket to the port
	if(setsockopt(socket1, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt1, sizeof(opt1))) 
	{
		sprintf(logbuf, "errno = %d: %s setsockopt failed\n", errno, strerror(errno));
		printLog(logbuf);
		freeAll();
		unlink("daemonLock");
		close(fd);
		exit(EXIT_FAILURE); 
	}

	serverAddr.sin_family = AF_INET;; 
	serverAddr.sin_addr.s_addr = INADDR_ANY; 
	serverAddr.sin_port = htons(port);

	// Binds the socket to the address and port number
	if(bind(socket1, (struct sockaddr *)&serverAddr, sizeof(serverAddr))<0) 
	{
		sprintf(logbuf, "errno = %d: %s bind failed\n", errno, strerror(errno));
		printLog(logbuf);
		freeAll();
		unlink("daemonLock");
		close(fd);
		exit(EXIT_FAILURE); 
	}

	//  It waits for the client to approach the server to make a connection
	if (listen(socket1, 4096) < 0) 
	{
		sprintf(logbuf, "errno = %d: %s listen failed\n", errno, strerror(errno));
		printLog(logbuf);
		freeAll();
		unlink("daemonLock");
		close(fd);
		exit(EXIT_FAILURE); 
	}

	// Main Thread
	// Endless loop, it just check sigint signal arrives.
	while(flagint == 0)
	{
		// Wait for connection is established between client and server
		if(socket1 != -2 && (socket2 = accept(socket1, (struct sockaddr *)&serverAddr, (socklen_t*)&addrlen))<0)
		{
			if(socket1 != -2)
			{
				sprintf(logbuf, "errno = %d: %s accept failed\n", errno, strerror(errno));
				printLog(logbuf);
				freeAll();
				unlink("daemonLock");
				close(fd);
				exit(EXIT_FAILURE);
			} 
		}
		if(socket1 == -2)
			break;

		pthread_mutex_lock(&mutex3);
		pthread_mutex_lock(&mutex1);

		// Connection counter increased by 1
		pcount2 += 1;

		// Checks whether the capacity has reached 75%.
		// If it has reached, it waits for the pool coordinate thread to create new threads.
		while(flagint == 0 && (((double)pcount2) / ((double)threadNumber) * 100) >= 75 && threadNumber != maxThread)
		{
			pthread_cond_broadcast(&pempty);
			pthread_cond_wait(&pfull, &mutex3);
		}

		pthread_cond_broadcast(&pempty);
		pthread_mutex_unlock(&mutex3);
		if(flagint != 0)
		{
			pthread_mutex_unlock(&mutex1);
			break;
		}
		
		// If it reaches the maximum number of threads, the threads are expected to finish.
		if(pcount2 > maxThread || pcount3 >= threadNumber)
		{
			sprintf(logbuf, "No thread is available! Waiting for one.");
			printLog(logbuf);
		}

		while(flagint == 0 && (pcount2 > maxThread || pcount3 >= threadNumber))
			pthread_cond_wait(&cempty, &mutex1);

		// Enqueue the provided socket to queue, And increase counters
		enqueue(&ports, socket2);
		pcount += 1;
		pcount3 += 1;

		pthread_cond_broadcast(&cfull);
		pthread_mutex_unlock(&mutex1);
		if(flagint != 0)
			break;

		// It is ensured that the connection provided by the main thread is forwarded to any thread.
		pthread_mutex_lock(&tmutex);
		while(flagint == 0 && tcount == 1)
			pthread_cond_wait(&tempty, &tmutex);
		tcount = 1;
		pthread_cond_broadcast(&tfull);
		pthread_mutex_unlock(&tmutex);
		if(flagint != 0)
			break;
	}

	for(i = 0; i < threadNumber; i++)
	{
		if(pthread_join(thread_id[i], NULL) != 0)
		{
			sprintf(logbuf, "errno = %d: %s pthread_join\n", errno, strerror(errno));
			printLog(logbuf);
			freeAll();
			unlink("daemonLock");
			close(fd);
			exit(EXIT_FAILURE);
		}
	}

	// Join threads
	if(pthread_join(thread_pool, NULL) != 0)
	{
		sprintf(logbuf, "errno = %d: %s pthread_join\n", errno, strerror(errno));
		printLog(logbuf);
		freeAll();
		unlink("daemonLock");
		close(fd);
		exit(EXIT_FAILURE);
	}

	// Close sockets
	if(socket1 >= 0)
		close(socket1);

	// Deallocate all memory
	freeAll();

	sprintf(logbuf, "All threads have terminated, server shutting down. \n");
	printLog(logbuf);

	// Close file
	fclose(fd1);
	// Close logfile
	close(fd2);

	// unlink temprory file
	unlink("daemonLock");
	close(fd);

	return 0;
}

void *thread_function(void *args)
{
	int x =((int*)args)[0];
	char buffer[SIZE] = {' '};
	char buffer2[SIZE]  = {' '};
	char buffer3[SIZE]  = {' '};
	char buffer4[SIZE]  = {' '};
	char logbuf[SIZE*2] = {' '};

	int temp, node1 = 0, node2 = 0, rsize = 0;
	double capacity = 0;
	node* nodetemp;
	int* resArr = (int*)malloc(graph1.c * sizeof(int));

	int i, res, index;
	int cflag = 0;

	// Endless loop, it just check sigint signal arrives.
	while(flagint == 0)
	{
		pthread_mutex_lock(&mutex1);
		if(flagint == 0)
		{
			sprintf(logbuf, "Thread #%d: waiting for connection", x);
			printLog(logbuf);
		}
		
		// If any thread completes an loop
		if(cflag == 1)
		{
			// Decrase counters
			pcount3 -= 1;
			pcount2 -= 1;
			pthread_cond_signal(&cempty);
		}
		else
			cflag = 1;

		// Waits to be awakened by the main thread.
		while(flagint == 0 && pcount == 0)
			pthread_cond_wait(&cfull, &mutex1);
		if(flagint != 0)
		{
			pthread_cond_broadcast(&cempty);
			pthread_mutex_unlock(&mutex1);
			break;
		}

		// Take socket from queue
		temp = dequeue(&ports);
		pcount -= 1;
		capacity = ((double)pcount3 - pcount) / ((double)threadNumber) * 100;
		sprintf(logbuf, "A connection has been delegated to thread id #%d, system load %.2lf%% ", x, capacity);
		printLog(logbuf);

		strcpy(buffer, " ");
		rsize = read(temp , buffer, SIZE);
		buffer[rsize] = '\0';

		// Indicates that the connection was received by any thread.
		pthread_mutex_lock(&tmutex);
		while(flagint == 0 && tcount == 0)
			pthread_cond_wait(&tfull, &tmutex);
		tcount = 0;
		pthread_cond_broadcast(&tempty);
		pthread_mutex_unlock(&tmutex);

		pthread_cond_broadcast(&cempty);
		pthread_mutex_unlock(&mutex1);

		// Parse message, then get nodes.
		for(i = 0; buffer[i] != '-'; i++);
		strncpy(buffer4, buffer, i);
		buffer4[i] = '\0';
		node1 = atoi(buffer4);
		strncpy(buffer4, &(buffer[i+1]), strlen(buffer)-i);
		buffer4[strlen(buffer)-i] = '\0';
		node2 = atoi(buffer4);

		// Readers are the threads attempting to search the data structure to find out whether a certain path is in it or not
		pthread_mutex_lock(&mutex2);
		while((AW + WW) > 0)
		{
			WR++;
			pthread_cond_wait(&okToRead, &mutex2);
			WR--;
		}
		AR++;
		pthread_mutex_unlock(&mutex2);

		sprintf(logbuf, "Thread #%d: searching database for a path from node %d to node %d", x, node1, node2);
		printLog(logbuf);

		res = 0;
		// Determine indexs, It is checked whether the path is in catch or not.
		if(node1 >= catch1.sizea || node2 >= catch1.sizeb)
			index = -2;
		else
		{
			index = -1;
			nodetemp = catch1.index[node1]->front;
			while(nodetemp != NULL)
			{
				if(nodetemp->item == node2)
				{
					index = nodetemp->index;
					break;
				}
				nodetemp = nodetemp->next;
			}
		}

		// If index is negative, path does not exist in cache
		// If not, it is the index where the path is located.
		if(index != -1 && index != -2)
		{
			// Converts path to string
			strcpy(buffer2, " ");
			strcpy(buffer3, " ");
			nodetemp = catch1.paths[index]->front;
			i = 0;
			while(nodetemp != NULL)
			{
				sprintf(buffer3, "%d", nodetemp->item);
				if(i == 0)
					strcpy(buffer2, buffer3);
				else if(nodetemp->next != NULL)
				{
					strcat(buffer2, "->");
					strcat(buffer2, buffer3);
				}
				else
				{
					strcat(buffer2, "->");
					strcat(buffer2, buffer3);
				}

				nodetemp = nodetemp->next;
				i += 1;
			}

			// The path is sent to the client.
			pthread_mutex_lock(&sendmutex);
			sprintf(logbuf, "Thread #%d: path found in database: %s", x, buffer2);
			printLog(logbuf);
			send(temp, buffer2, strlen(buffer2), 0);
			close(temp);
			pthread_mutex_unlock(&sendmutex);
		}
		else
		{
			sprintf(logbuf, "Thread #%d: no path in database, calculating %d->%d ", x, node1, node2);
			printLog(logbuf);
		}

		// Reading process completed.
		// It awakens the waiting threads.
		pthread_mutex_lock(&mutex2);
		AR--;
		if(AR == 0 && WW > 0)
			pthread_cond_signal(&okToWrite);
		pthread_mutex_unlock(&mutex2);

		// Search path with bfs algroithm
		if(index == -1)
		{
			// If bfs return -1, there is no path
			// If not, there is a path and it will fill resArr array
			res = bfs(&graph1, node1, node2, resArr);

			// Writers are the threads that have calculated a certain path and now want to add it into the data structure
			pthread_mutex_lock(&mutex2);
			while((AW + AR) > 0)
			{
				WW++;
				pthread_cond_wait(&okToWrite, &mutex2);
				WW--;
			}
			AW++;
			pthread_mutex_unlock(&mutex2);

			if(res != -1)
			{
				// Converts path to string
				strcpy(buffer2, " ");
				strcpy(buffer3, " ");
				for(i = 0; i <= res; i++)
				{
					sprintf(buffer3, "%d", resArr[i]);
					if(i == 0)
						strcpy(buffer2, buffer3);
					else if(i != res)
					{
						strcat(buffer2, "->");
						strcat(buffer2, buffer3);
					}
					else
					{
						strcat(buffer2, "->");
						strcat(buffer2, buffer3);
					}
				}

				// The path is sent to the client.
				pthread_mutex_lock(&sendmutex);
				sprintf(logbuf, "Thread #%d: path calculated: %s", x, buffer2);
				printLog(logbuf);
				send(temp, buffer2, strlen(buffer2), 0);
				close(temp);
				pthread_mutex_unlock(&sendmutex);

				// Path found in BFS is added to cache
				enqueue2(catch1.index[node1], node2, catch1.count);
				catch1.count += 1;
				if(catch1.count == 1)
					catch1.paths = (Queue**) calloc(catch1.count, sizeof(Queue*));
				else
					catch1.paths = (Queue**) realloc(catch1.paths, catch1.count * sizeof(Queue*));

				catch1.paths[catch1.count - 1] = (Queue*) malloc(sizeof(Queue));
				catch1.paths[catch1.count - 1]->front = NULL;
				catch1.paths[catch1.count - 1]->rear = NULL;

				for(i = 0; i <= res; i++)
					enqueue(catch1.paths[catch1.count - 1], resArr[i]);
			}

			// Writting process completed.
			// It awakens the waiting threads.
			pthread_mutex_lock(&mutex2);
			AW--;
			if(WW > 0)
				pthread_cond_signal(&okToWrite);
			else if (WR > 0)
				pthread_cond_broadcast(&okToRead);
			pthread_mutex_unlock(&mutex2);
		}

		if(res == -1 || index == -2)
		{
			// Path not found with bfs or in cache
			pthread_mutex_lock(&sendmutex);
			sprintf(logbuf, "Thread #%d: path not possible from node %d to %d ", x, node1, node2);
			printLog(logbuf);
			send(temp, "N", 1, 0);
			close(temp);
			pthread_mutex_unlock(&sendmutex);
		}
	}

	free(resArr);
	return NULL;
}

void *thread_coord_pool(void *args)
{
	int newamount;
	int temp, temp2;
	double capacity;
	char logbuf[SIZE*2] = {' '};

	//Endless loop, it just check sigint signal arrives.
	while(flagint == 0)
	{
		pthread_mutex_lock(&mutex3);
		// Checks whether the capacity has reached 75%.
		// If it has reached, it will start for the extend pool
		// If not, it will wait
		while(flagint == 0 && (((double)pcount2) / ((double)threadNumber) * 100) < 75 && threadNumber != maxThread)
			pthread_cond_wait(&pempty, &mutex3);
		if(flagint != 0)
		{
			pthread_cond_broadcast(&pfull);
			pthread_mutex_unlock(&mutex3);
			break;
		}

		temp2 = threadNumber;
		capacity = ((double)pcount2) / ((double)threadNumber) * 100;

		// the pool size will be enlarged by 25%
		temp = threadNumber;
		newamount = round(threadNumber * 1.25);

		// If 25% excess of the current thread is still equal to it and 1 excess is possible, 1 is increased.
		if(threadNumber == newamount && (newamount + 1) <= maxThread)
			threadNumber = newamount + 1;
		// If 25% excess of the current thread is not equal to it and pool will enlarged with new thread number.
		else if(threadNumber != newamount && newamount <= maxThread)
			threadNumber = newamount;
		// Otherwise, Thread has reached the maximum number.
		else
			threadNumber = maxThread;

		// Create new thread for enlarge pool.
		if(temp != threadNumber)
		{
			thread_id = (pthread_t*) realloc(thread_id, threadNumber * sizeof(pthread_t));
			indexThread = (int**) realloc(indexThread, threadNumber * sizeof(int*));

			for( ; temp < threadNumber; temp++)
			{
				indexThread[temp] = malloc(sizeof(int));
				*(indexThread[temp]) = temp;
				if(pthread_create(&(thread_id[temp]), NULL, &thread_function, indexThread[temp]) != 0)
				{
					sprintf(logbuf, "errno = %d: %s pthread_create\n", errno, strerror(errno));
					printLog(logbuf);
					unlink("daemonLock");
					close(fd);
					exit(EXIT_FAILURE);
				}
			}
		}

		// Print new system load and thread number in pool
		if(temp2 != threadNumber)
		{
			sprintf(logbuf, "System load %lf%%, pool extended to %d threads", capacity, threadNumber);
			printLog(logbuf);
		}

		pthread_cond_broadcast(&pfull);
		pthread_mutex_unlock(&mutex3);
	}
	return NULL;
}

// Become daemon process
// It was made by looking at the 770 th page of the book.
int becomeDaemon()
{
	int maxfd, fd;

	switch(fork())
	{
		case -1:
			return -1;
		case 0:
			break;
		default:
			exit(EXIT_SUCCESS);
	}

	if(setsid() == -1)
		return -1;

	switch(fork())
	{
		case -1:
			return -1;
		case 0:
			break;
		default:
			exit(EXIT_SUCCESS);
	}

	umask(0);

	maxfd = sysconf(_SC_OPEN_MAX);
	if(maxfd == -1)
		maxfd = 8192;
	
	// Close all of its inherited open files
	for(fd = 0; fd < maxfd; fd++)
		close(fd);
	
	return 0;
}

void enqueue(Queue *q, int item)
{
	node* tempNode = (node *) malloc(sizeof(node));
	tempNode->next = NULL;
	tempNode->item = item;

	if(empty(q)!=0)
		q->rear->next = tempNode;
	else
		q->front = tempNode;
	
	q->rear = tempNode;
}

void enqueue2(Queue *q, int item, int index)
{
	node* tempNode = (node *) malloc(sizeof(node));
	tempNode->next = NULL;
	tempNode->item = item;
	tempNode->index = index;
	if(empty(q)!=0)
		q->rear->next = tempNode;
	else
		q->front = tempNode;
	
	q->rear = tempNode;
}

int empty(Queue *q)
{
	if(q->front==NULL)
		return 0;
	else
		return 1;
}

int dequeue(Queue *q)
{										
	int client;
	node *tempNode;

	if(empty(q)!=0)
	{
		client = q->front->item;
		tempNode = q->front;
		q->front = q->front->next;
		
		free(tempNode);
		
		if(q->front==NULL)
			q->rear=NULL;
	}
	return client;
}

void initGraph(Graph* graph)
{
	graph->size = 0;
	graph->edges = 0;
	graph->nodes = 0;
	graph->adjList = NULL;
	graph->nodelist = NULL;
	graph->a = 0;
	graph->b = 0;
	graph->c = 0;
}
void initCatch(Catch* catch, int a, int b, int c)
{
	int i;
	catch->sizea = a;
	catch->sizeb = b;
	catch->sizec = c;
	catch->count = 0;

	catch1.index = (Queue**) calloc(catch->sizea, sizeof(Queue*));
	for(i = 0; i < catch->sizea; i++)
	{
		catch->index[i] = (Queue*) malloc(sizeof(Queue));
		catch->index[i]->front = NULL;
		catch->index[i]->rear = NULL;
	}
	catch->paths = NULL;
}

void addEdge(Graph* graph, int x, int y)
{
	if(existInList(graph->adjList[x], y) == -1)
	{
		enqueue(graph->adjList[x], y);
		graph->edges += 1;
	}

	graph->nodelist[x] = 1;
	graph->nodelist[y] = 1;
}

int bfs(Graph* graph, int x, int y, int* resArr)
{
	int i, j, num, tempx = x;
	node* temp;
	int flag = 0;
	Queue queue1;

	i = existInArray(graph, x);

	if(i == -1)
		return -1;

	int* visit = (int*) malloc(graph->b * sizeof(int));
	int* parent = (int*) malloc(graph->b * sizeof(int));
	for(j = 0; j < graph->b; j++)
	{
		visit[j] = -1;
		parent[j] = -1;
	}
	queue1.front = NULL;
	queue1.rear = NULL;
	temp = graph->adjList[i]->front;
	visit[x] = 1;
	parent[x] = x;
	do{
		while(temp != NULL)
		{
			num = temp->item;
			if((num == x && x == y) || visit[num] != 1)
			{
				enqueue(&queue1, num);
				visit[num] = 1;
				parent[num] = tempx;

				if(num == y)
				{
					flag = 1;
					break;
				}
			}
			temp = temp->next;
		}
		if(flag == 1)
			break;
		if(empty(&queue1) == 1)
			tempx = dequeue(&queue1);
		else
			break;
		i = existInArray(graph, tempx);
		if(i != -1)
		{
			temp = graph->adjList[i]->front;
		}
		else
			temp = NULL;
	}while(flag != 1 || empty(&queue1) == 1);

	while(empty(&queue1))
		dequeue(&queue1);

	if(flag == 1)
	{
		int* arr = (int*) malloc(graph->b * sizeof(int));
		int index = 0, tempint = y;

		do
		{
			arr[index] = tempint;
			tempint = parent[tempint];
			index += 1;	
		}while(tempint != x);
		arr[index] = tempint;
		for(i = 0; i <= index; i++)
			resArr[i] = arr[index-i];
		free(arr);
		free(visit);
		free(parent);
		return index;
	}
	else
	{
		free(visit);
		free(parent);
		return -1;
	}
}

int loadGraph(Graph* graph, FILE* fd1)
{
	char buf1[SIZE] = {' '};
	char temp;
	int x, y;
	int temp1 = 0, temp2 = 0;
	int i = 0;
	double current = 0;

	while(fscanf(fd1, "%s", buf1) != EOF)
	{
		if(buf1[0] == '#')
		{
			temp = '#';
			while(temp != '\n')
			{
				fscanf(fd1, "%c", &temp);
			}
			continue;
		}
		else
			break;
	}
	fseek(fd1, -1, SEEK_CUR);
	current = ftell(fd1);
	while(fscanf(fd1, "%d %d", &x, &y) != EOF)
	{
		if(temp1 == x)
			temp2++;
		else
		{
			if(temp2 > graph->c)
			{
				graph->c = temp2;
			}
			temp1 = x;
			temp2 = 0;
		}
		if(graph->a < x)
			graph->a = x;
		if(graph->b < x)
			graph->b = x;
		if(graph->b < y)
			graph->b = y;
	}
	graph->a += 2;
	graph->b += 2;
	graph->c += 2;
	graph->size = graph->a;
	graph->adjList = (Queue**) calloc(graph->size, sizeof(Queue*));
	for(i = 0; i < graph->size; i++)
	{
		graph->adjList[i] = (Queue*) malloc(sizeof(Queue));
		graph->adjList[i]->front = NULL;
		graph->adjList[i]->rear = NULL;
	}
	graph->nodelist = (int *) calloc(graph->b, sizeof(int));
	fseek(fd1, current, SEEK_SET);
	while(fscanf(fd1, "%d %d", &x, &y) != EOF)
	{
		addEdge(graph, x, y);
	}

	for(i = 0; i < graph->b; i++)
	{
		if(graph->nodelist[i] == 1)
			graph->nodes += 1;
	}
	
	return 0;
}

int existInList(Queue *q, int num)
{
	int i = 0;
	node* tempNode = q->front;

	while(tempNode != NULL)
	{
		if(tempNode->item == num)
			return i;
		tempNode = tempNode->next;
		i += 1;
	}

	return -1;
}

int existInArray(Graph* graph, int num)
{
	if(num >= 0 && num < graph->size)
		return num;
	else
		return -1;
}

void freeGraph()
{
	int i;
	if(graph1.nodelist != NULL)
		free(graph1.nodelist);

	for(i = 0; i < graph1.size; i++)
	{
		while(graph1.adjList != NULL && graph1.adjList[i] != NULL && empty(graph1.adjList[i]))
			dequeue(graph1.adjList[i]);
		if(graph1.adjList != NULL && graph1.adjList[i] != NULL)
			free(graph1.adjList[i]);
	}
	if(graph1.adjList != NULL)
		free(graph1.adjList);
}

void freeCatch()
{
	int i;

	for(i = 0; i < catch1.count; i++)
	{
		while(catch1.paths != NULL && catch1.paths[i] != NULL && empty(catch1.paths[i]))
			dequeue(catch1.paths[i]);
		if(catch1.paths[i] != NULL)
			free(catch1.paths[i]);
	}
	if(catch1.paths != NULL)
		free(catch1.paths);

	for(i = 0; i < catch1.sizea; i++)
	{
		while(catch1.index != NULL && catch1.index[i] != NULL && empty(catch1.index[i]))
			dequeue(catch1.index[i]);
		if(catch1.index[i] != NULL)
			free(catch1.index[i]);
	}
	if(catch1.index != NULL)
		free(catch1.index);

}

void freeOther()
{
	int i;
	for(i = 0; i < threadNumber; i++)
	{
		if(indexThread != NULL && indexThread[i] != NULL)
			free(indexThread[i]);
	}
	if(indexThread != NULL)
		free(indexThread);

	if(thread_id != NULL)
		free(thread_id);
}

void freeAll()
{
	freeGraph();
	freeCatch();
	freeOther();
}

void printLog(char* str)
{
	pthread_mutex_lock(&printmutex);
	time_t ltime;
	struct tm lresult;
	char stime[32];
	size_t n;
	char printbuf[SIZE*5];
	char* bp;
	int bytesread, byteswritten;

	ltime = time(NULL);
	localtime_r(&ltime, &lresult);
	asctime_r(&lresult, stime);
	n = strlen(stime);
	if(n && stime[n-1] == '\n') stime[n-1] = '\0';
	sprintf(printbuf, "[%s] %s\n", stime, str);

	bytesread = strlen(printbuf);
	bp = printbuf;
	while(bytesread > 0)
	{
		while(((byteswritten = write(fd2, bp, bytesread)) == -1) && (errno = EINTR));
		if(byteswritten < 0)
			break;
		bytesread -= byteswritten;
		bp += byteswritten;
	}

	pthread_mutex_unlock(&printmutex);
}

// SSIGCHLD Handler
void sigHandler(int sig)
{
	// Keep errno
	int savedErrno = errno;
	char logbuf[SIZE*2] = {' '};
	if(sig == SIGINT)
	{
		sprintf(logbuf, "Termination signal received, waiting for ongoing threads to complete.");
		printLog(logbuf);
		// Replaces the value of the global variable with 1.
		flagint = 1;
		if(socket1 != -1)
		{
			shutdown(socket1, SHUT_RD);
			close(socket1);
		}
		socket1 = -2;

		// It will wait for its ongoing threads to complete. So it will wakeup ongoing threads.
		pthread_cond_broadcast(&cfull);
		pthread_cond_broadcast(&pfull);
		pthread_cond_broadcast(&tfull);
	    pthread_cond_broadcast(&cempty);
		pthread_cond_broadcast(&pempty);
		pthread_cond_broadcast(&tempty);
	}
	else if(sig == SIGSEGV)
	{
		freeAll();
		sprintf(logbuf, "Out of memory: The program was terminated by deallocating all the resources.");
		printLog(logbuf);
		unlink("daemonLock");
		close(fd);
		exit(EXIT_SUCCESS);
	}

	// Restore errno
	errno = savedErrno;

	return;
}
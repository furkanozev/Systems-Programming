#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <math.h>

// A struct for keeping information of each florist.
typedef struct{
   char* floristName;
   double x;
   double y;
   double speed;
   int typeAmount;
   char** flowers;
   int sales;
   int time;
}floristStruct;

//A struct for keeping information of each client.
typedef struct{
   char* clientName;
   double x;
   double y;
   char* flower;
   double distance;
}clientStruct;

// Queue structure for keeping flower orders.
typedef struct node{
	clientStruct* item;
	struct node *next;
}node;

typedef struct{
	node *front;
	node *rear;
}Queue;

// Queue functions.
void enqueue(Queue *q, clientStruct* item);
clientStruct* dequeue(Queue *q);
int empty(Queue *q);

// Create a floristStruct object and fill florist information in this object.
void fillFlorist(char* buf);
// Create 1 clientStruct object and fill client information in this object.
void fillClient(char* buf, clientStruct* client);
void *thread_function(void *args);
// Unlock mutexes of florists to avoid deadlock when program was finished.
void notify();
// Unlock other mutexes to avoid deadlock when program was finished.
void unlockMutexes();
// For case Control C (SIGINT signal handling)
void sigintHandler(int sig);

// Deallocate memories.
void freeFlorist();
void freeClient();
void freeQueues();
void freeMutexs();
void freeOther();
void freeAll();

int flag = 1, size1 = 1024, size2 = 1024;

floristStruct* florists = NULL;
clientStruct** clients = NULL;
int floristAmount = 0;
int clientAmount = 0;
int finish = 0;

Queue** queues = NULL;
// Dynamic florists mutexes.
pthread_mutex_t* floristMutexes = NULL;
pthread_mutex_t mutex1, mutex2, mutex3;

// To keep some number, string, id, etc.
int** indexThread = NULL;
char* buf1 = NULL;
char* tempbuf1 = NULL;
char* tempbuf2 = NULL;
pthread_t* thread_id;

int main(int argc, char* argv[])
{
	int opt;
	char* filePath;
	int i, j, z, select;
	int size = 1024, total = 0;
	int bytesread = 0;
	char temp = ' ';
	buf1 = (char*) calloc(1024, sizeof(char));
	clientStruct* client;

	// Handler for SIGINT
	signal(SIGINT, sigintHandler);

	// Check argument amount
	if(argc != 3)
	{
		printf("This usage is wrong. \n");
		printf("Usage: ./floristApp -i dataFile.dat\n");
		exit(EXIT_FAILURE);
	}

	// I used the getopt() library method for parsing commandline arguments.
	while((opt = getopt(argc, argv, "i")) != -1)
	{
		switch(opt)
		{
			case 'i':
				filePath = argv[optind];
				break;
			default:
			// If the command line arguments are missing/invalid your program must print usage information and exit.
				printf("This usage is wrong. \n");
				printf("Usage: ./floristApp -i dataFile.dat\n");
				exit(EXIT_FAILURE);
		}
	}

	// Open dataPath
	int fd = open(filePath, O_RDONLY);
	if(fd == -1)
	{
		fprintf(stderr, "\nerrno = %d: %s open dataFile\n\n", errno, strerror(errno));
		exit(EXIT_FAILURE);
	}

	// To temporarily keep strings.
	tempbuf1 = calloc(size1, sizeof(char));
	tempbuf2 = calloc(size2, sizeof(char));

	srand(time(NULL));

	printf("Florist application initializing from file: %s\n", filePath);

	// Read florist informations.
	for( ; ; )
	{
		i = 0;
		while(temp != '\n')
		{
			while(((bytesread = read(fd, &temp, 1)) == -1) && (errno == EINTR));
			if(bytesread <= 0)
			{
				printf("\nThere are error when read byte from dataPath\n");
				exit(EXIT_FAILURE);
			}

			i += 1;
		}
		if(i >= size)
		{
			size *= 2;
			buf1 = (char *) realloc(buf1, size);
		}

		lseek(fd, total, SEEK_SET);
		while(((bytesread = read(fd, buf1, i)) == -1) && (errno == EINTR));
		if(bytesread <= 0)
		{
			printf("\nThere are error when read byte from dataPath\n");
			exit(EXIT_FAILURE);
		}

		buf1[i] = '\0';
		// Fills the information of the florist that are read into the struct.
		fillFlorist(buf1);
		
		total += i;
		i = 0;

		while(((bytesread = read(fd, &temp, 1)) == -1) && (errno == EINTR));
		if(bytesread <= 0)
		{
			printf("\nThere are error when read byte from dataPath\n");
			exit(EXIT_FAILURE);
		}
		if(temp == '\n')
		{
			temp = ' ';
			total += 1;
			break;
		}
		else
			lseek(fd, -1, SEEK_CUR);

		temp = ' ';
	}

	queues = (Queue**) calloc(floristAmount, sizeof(Queue*));
	for(i = 0; i < floristAmount; i++)
	{
		queues[i] = (Queue*) malloc(sizeof(Queue));
		queues[i]->front = NULL;
		queues[i]->rear = NULL;
	}

	double values[floristAmount];

	// As many as the number of florists, florist mutexes are created.
	floristMutexes = (pthread_mutex_t*) calloc(floristAmount, sizeof(pthread_mutex_t));

	// Inıt these mutex, and locked it first.
	for(i = 0; i < floristAmount; i++)
	{
		// Init thread mutex
		if(pthread_mutex_init(&(floristMutexes[i]), NULL) != 0)
		{
			fprintf(stderr, "\nerrno = %d: %s mutex init has failed\n\n", errno, strerror (errno));
			exit(EXIT_FAILURE);
		}
		pthread_mutex_lock(&(floristMutexes[i]));
	}

	// Init posix other mutexes
	if(pthread_mutex_init(&mutex1, NULL) != 0)
	{
		fprintf(stderr, "\nerrno = %d: %s mutex init has failed\n\n", errno, strerror (errno));
		exit(EXIT_FAILURE);
	}
	pthread_mutex_lock(&mutex1);
	
	if(pthread_mutex_init(&mutex2, NULL) != 0)
	{
		fprintf(stderr, "\nerrno = %d: %s mutex init has failed\n\n", errno, strerror (errno));
		exit(EXIT_FAILURE);
	}

	if(pthread_mutex_init(&mutex3, NULL) != 0)
	{
		fprintf(stderr, "\nerrno = %d: %s mutex init has failed\n\n", errno, strerror (errno));
		exit(EXIT_FAILURE);
	}
	pthread_mutex_lock(&mutex3);


	thread_id = (pthread_t*) calloc(floristAmount, sizeof(pthread_t));

	indexThread = (int**) calloc(floristAmount, sizeof(int*));

	// Create thread. Thread take number.
	for(i = 0; i < floristAmount; i++)
	{
		indexThread[i] = malloc(sizeof(int));
		*(indexThread[i]) = i;
		pthread_create(&(thread_id[i]), NULL, &thread_function, indexThread[i]);
	}

	printf("%d florists have been created\n", floristAmount);
	// ???
	printf("Processing requests\n");

	// Read client informations.
	for( ; ; )
	{
		i = 0;
		while(temp != '\n')
		{
			while(((bytesread = read(fd, &temp, 1)) == -1) && (errno == EINTR));
			if(bytesread <= 0)
			{
				break;
			}
			i += 1;
		}
		if(i >= size)
		{
			size *= 2;
			buf1 = (char *) realloc(buf1, size);
		}

		lseek(fd, total, SEEK_SET);
		while(((bytesread = read(fd, buf1, i)) == -1) && (errno == EINTR));
		if(bytesread <= 0)
		{
			printf("\nThere are error when read byte from dataPath\n");
			exit(EXIT_FAILURE);
		}

		buf1[i] = '\0';

		if(clientAmount == 0)
			clients = (clientStruct**) malloc(sizeof(clientStruct*));
		else
			clients = (clientStruct**) realloc(clients, (clientAmount + 1) * sizeof(clientStruct*));

		clients[clientAmount] = (clientStruct *) malloc(sizeof(clientStruct));
		client = clients[clientAmount];
		// Fills the information of the client that are read into the struct.
		fillClient(buf1, clients[clientAmount]);

		// Distances between the client and florists are calculated and stored in the array.
		for(j = 0; j < floristAmount; j++)
		{
			if(fabs(client->x - florists[j].x) > fabs(client->y - florists[j].y))
				values[j] = fabs(client->x - florists[j].x);
			else
				values[j] = fabs(client->y - florists[j].y);
		}

		// The closest florist with the flower desired by the client is determined.
		select = -1;
		for(j = 0; j < floristAmount; j++)
		{
			if(select == -1)
			{
				for(z = 0; z < florists[j].typeAmount; z++)
				{
					if(strcmp(florists[j].flowers[z], client->flower) == 0)
					{
						select = j;
						break;
					}
				}
			}
			else
			{
				if(values[select] > values[j])
				{
					for(z = 0; z < florists[j].typeAmount; z++)
					{
						if(strcmp(florists[j].flowers[z], client->flower) == 0)
						{
							select = j;
							break;
						}
					}
				}
			}
		}

		// If the florists does not have the flower that client wants:
		if(select == -1)
		{
			printf("There are no florists selling %s to %s.\n", client->flower, client->clientName);
		}
		else
		{
			// The desired flower is added to the florist's request queue and the florist's mutex is unlocked.
			client->distance = values[select];
			enqueue(queues[select], client);

			if(empty(queues[select]) == 0)
				pthread_mutex_unlock(&(floristMutexes[select]));
		}
		
		total += i;
		i = 0;

		while(((bytesread = read(fd, &temp, 1)) == -1) && (errno == EINTR));
		if(bytesread <= 0)
		{
			break;
		}
		if(temp == '\n')
			break;
		else
			lseek(fd, -1, SEEK_CUR);

		temp = ' ';
	}

	// Once the main thread is done, it need a way to notify the florists that there won’t be any more requests.
	flag = 0;
	notify();

	pthread_mutex_lock(&mutex1);

	printf("All requests processed.\n");

	pthread_mutex_unlock(&mutex3);

	// Join threads
	for(i = 0; i < floristAmount; i++)
	{
		if(pthread_join(thread_id[i], NULL) != 0)
		{
			fprintf(stderr, "\nerrno = %d: %s pthread_join\n\n", errno, strerror(errno));
			exit(EXIT_FAILURE);
		} 
	}

	// Print statistics
	printf("Sale statistics for today:\n");
	printf("-------------------------------------------------\n");
	printf("Florist\t\t # of sales \t Total time\n");
	printf("-------------------------------------------------\n");
	for(i = 0; i < floristAmount; i++)
	{
		printf("%s \t\t %d \t\t %dms\n", florists[i].floristName, florists[i].sales, florists[i].time);
	}
	printf("-------------------------------------------------\n");
	
	// Deallocate all memories
	freeAll();

	// Close file
	if(close(fd) == -1)
	{
		fprintf(stderr, "\nerrno = %d: %s close dataPath\n\n", errno, strerror(errno));
		exit(EXIT_FAILURE);
	}

	return 0;
}

//Fills the information of the florist that are read into the struct.
void fillFlorist(char* buf)
{
	int i, temp, temp2;

	floristAmount += 1;

	// Allocate new memory for new florist
	if(floristAmount == 1)
		florists = (floristStruct*) calloc(floristAmount, sizeof(floristStruct));
	else
		florists = (floristStruct*) realloc(florists, floristAmount * sizeof(floristStruct));

	florists[floristAmount - 1].sales = 0;
	florists[floristAmount - 1].time = 0;

	// Parse name and write struct.
	for(i = 0; buf[i] != ' '; i++);
	florists[floristAmount-1].floristName = calloc(i+1, sizeof(char));
	strncpy(florists[floristAmount-1].floristName, buf, i);
	florists[floristAmount-1].floristName[i] = '\0';

	// Parse coordinate x and write struct.
	for(i += 2, temp = i; buf[i] != ','; i++);
	if(i - temp >= size1)
	{
		size1 *= 2;
		tempbuf1 = realloc(tempbuf1, size1);
	}
	strncpy(tempbuf1, &buf[temp], i - temp);
	tempbuf1[i-temp] = '\0';
	florists[floristAmount-1].x = atof(tempbuf1);

	strcpy(tempbuf1, " ");

	// Parse coordinate y and write struct.
	for(i += 1, temp = i; buf[i] != ';'; i++);
	if(i - temp >= size1)
	{
		size1 *= 2;
		tempbuf1 = realloc(tempbuf1, size1);
	}
	strncpy(tempbuf1, &buf[temp], i - temp);
	tempbuf1[i-temp] = '\0';
	florists[floristAmount-1].y = atof(tempbuf1);

	strcpy(tempbuf1, " ");

	// Parse speed and write struct.
	for(i += 2, temp = i; buf[i] != ')'; i++);
	if(i - temp >= size1)
	{
		size1 *= 2;
		tempbuf1 = realloc(tempbuf1, size1);
	}
	strncpy(tempbuf1, &buf[temp], i - temp);
	tempbuf1[i-temp] = '\0';
	florists[floristAmount-1].speed = atof(tempbuf1);

	florists[floristAmount-1].typeAmount = 0;

	i += 2;
	// Parse flowers and write struct.
	while(buf[i] != '\n')
	{
		for(i += 2, temp = i; buf[i] != ',' && buf[i] != '\n'; i++);
		
		florists[floristAmount-1].typeAmount += 1;
		temp2 = florists[floristAmount-1].typeAmount;
		if(temp2 == 1)
			florists[floristAmount-1].flowers = (char**) calloc(temp2, sizeof(char *));
		else
			florists[floristAmount-1].flowers = (char**) realloc(florists[floristAmount-1].flowers, temp2 * sizeof(char *));

		florists[floristAmount-1].flowers[temp2 - 1] = (char*) calloc((i - temp + 1), sizeof(char));
		strncpy(florists[floristAmount-1].flowers[temp2 - 1], &buf[temp], i - temp);
		florists[floristAmount-1].flowers[temp2 - 1][i - temp] = '\0';
	}
	
}

//Fills the information of the client that are read into the struct.
void fillClient(char* buf, clientStruct* client)
{
	int i, temp;

	clientAmount += 1;

	// Parse name and write struct.
	for(i = 0; buf[i] != ' '; i++);
	client->clientName = calloc(i+1, sizeof(char));
	strncpy(client->clientName, buf, i);
	client->clientName[i] = '\0';

	// Parse coordinate x and write struct.
	for(i += 2, temp = i; buf[i] != ','; i++);
	if(i - temp >= size2)
	{
		size2 *= 2;
		tempbuf2 = realloc(tempbuf2, size2);
	}
	strncpy(tempbuf2, &buf[temp], i - temp);
	tempbuf2[i-temp] = '\0';
	client->x = atof(tempbuf2);

	strcpy(tempbuf2, " ");

	// Parse coordinate y and write struct.
	for(i += 1, temp = i; buf[i] != ')'; i++);
	if(i - temp >= size2)
	{
		size2 *= 2;
		tempbuf2 = realloc(tempbuf2, size2);
	}
	strncpy(tempbuf2, &buf[temp], i - temp);
	tempbuf2[i-temp] = '\0';
	client->y = atof(tempbuf2);

	strcpy(tempbuf2, " ");

	// Parse flower and write struct.
	for(i += 3, temp = i; buf[i] != '\n' && buf[i] != '\0'; i++);

	client->flower = (char*) calloc((i - temp + 1), sizeof(char));
	strncpy(client->flower, &buf[temp], i - temp);

}

void *thread_function(void *args)
{
	int x =((int*)args)[0];
	int time;
	
	// If the main thread has not ended or the request list is not empty:
	while(flag == 1 || empty(queues[x]) == 1)
	{
		pthread_mutex_lock(&(floristMutexes[x]));

		if(flag == 0 && empty(queues[x]) == 0)
			break;

		// Take requst from queue.
		clientStruct* client = dequeue(queues[x]);

		// Calculate preparation and delivery time
		time = (client->distance/florists[x].speed) + (rand()%250 + 1);
		florists[x].sales += 1;
		florists[x].time += time;
		// convert ms to micro second, and sleep
		usleep(time*1000);
		printf("Florist %s has delivered a %s to %s in %dms\n", florists[x].floristName, client->flower, client->clientName, time);

		// If florist have an request in queue, unlock mutex.
		if(empty(queues[x]) == 1)
				pthread_mutex_unlock(&(floristMutexes[x]));
		// Otherwise, the florist waits for the main thread to unlock this mutex.
	}

	pthread_mutex_lock(&mutex2);

	finish += 1;
	if(finish == floristAmount)
	{
		pthread_mutex_unlock(&mutex1);
	}

	pthread_mutex_unlock(&mutex2);

	pthread_mutex_lock(&mutex3);

	printf("%s closing shop.\n", florists[x].floristName);

	pthread_mutex_unlock(&mutex3);
	
	return NULL;
}

void enqueue(Queue *q, clientStruct* item)
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

int empty(Queue *q)
{
	if(q->front==NULL)
		return 0;
	else
		return 1;
}

clientStruct* dequeue(Queue *q)
{										
	clientStruct* client;
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
	else
		printf("It can not dequeue\n");
	return client;
}

// It ensures that all pending threads are awakened.
void notify()
{
	int i;
	
	for(i = 0; i < floristAmount; i++)
	{
		pthread_mutex_unlock(&(floristMutexes[i]));
	}
	
}

// Unlock other mutexes to avoid deadlock.
void unlockMutexes()
{
	int i;
	
	for(i = 0; i < floristAmount; i++)
	{
		pthread_mutex_unlock(&(floristMutexes[i]));
	}

	pthread_mutex_unlock(&mutex1);
	pthread_mutex_unlock(&mutex2);
	pthread_mutex_unlock(&mutex3);
	
}

// Free florist structs and their dynamic ingredients.
void freeFlorist()
{
	int i, j;
	for(i = 0; i < floristAmount; i++)
	{
		free(florists[i].floristName);

		for(j = 0; j < florists[i].typeAmount; j++)
		{
			free(florists[i].flowers[j]);
		}
		free(florists[i].flowers);
	}

	free(florists);
}

// Free client structs and their dynamic ingredients.
void freeClient()
{
	int i;
	for(i = 0; i < clientAmount; i++)
	{
		free(clients[i]->clientName);
		free(clients[i]->flower);
		free(clients[i]);
	}
	free(clients);
}
// Free queues.
void freeQueues()
{
	int i;
	for(i = 0; i < floristAmount; i++)
	{
		free(queues[i]);
	}
	free(queues);
}

// Free florists mutexes.
void freeMutexs()
{
	int i;
	for(i = 0; i < floristAmount; i++)
	{
		pthread_mutex_destroy(&(floristMutexes[i]));
	}
	free(floristMutexes);

	pthread_mutex_destroy(&mutex1);
	pthread_mutex_destroy(&mutex2);
	pthread_mutex_destroy(&mutex3);
}

// Free other mutexes
void freeOther()
{
	int i;
	for(i = 0; i < floristAmount; i++)
	{
		free(indexThread[i]);
	}
	free(indexThread);

	free(buf1);
	free(tempbuf1);
	free(tempbuf2);
	free(thread_id);
}

// Call alll free functions.
void freeAll()
{
	freeFlorist();
	freeQueues();
	freeClient();
	freeMutexs();
	freeOther();
}

// SSIGCHLD Handler
void sigintHandler(int sig)
{
	// Keep errno
	int savedErrno = errno;
	int i;

	for(i = 0; i < floristAmount; i++)
	{
		while(empty(queues[i]))
			dequeue(queues[i]);
	}

	unlockMutexes();

	for(i = 0; i < floristAmount; i++)
		pthread_cancel(thread_id[i]);

	for(i = 0; i < floristAmount; i++)
		pthread_join(thread_id[i], NULL);

    freeAll();

    printf("\nCase CTRL-C: The program was terminated by deallocating all the resources.\n\n");

    // Restore errno
    errno = savedErrno;

    exit(EXIT_SUCCESS);
}
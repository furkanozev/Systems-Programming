#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <semaphore.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/sem.h>

#define NTHREADS 6
#define AMOUNT_INGRE 4
#define LACK_ITEMS 2

void *thread_function(void *args);
void notify();
void ExitFailure();

// The ingredients will be delivered this array.
// This array located at the heap(because global), thus it shared among all involved threads.
char ingredients[LACK_ITEMS];
// flag indicate wholesaler status
// flag2 used to free memory
int flag = 1, flag2 = 0;
// Indicates the number of the chefs.
int* indexThread[NTHREADS];
// For ftok
char temp[] = "tempXXXXXX";

// sem_sync is sys-V semaphores to syncronize between wholesaler and chefs.
int sem_sync;

// sem_mut is a posix semaphore used to wait for the wholesaler to prepare the dessert.
sem_t sem_mut;

// lack two ingredients combination for each chef.
int comb[NTHREADS][LACK_ITEMS] = {{0, 1}, {0, 2}, {0, 3}, {1, 2}, {1, 3}, {2, 3}};
char* types[NTHREADS][LACK_ITEMS] = {{"milk", "flour"}, {"milk", "walnuts"}, {"milk", "sugar"}, {"flour", "walnuts"}, {"flour", "sugar"}, {"walnuts", "sugar"}};

int main(int argc, char* argv[])
{
	int opt;
	int i, j, x;
	int bytesread;

	char* filePath;
	char buf[3];

	key_t s_key;
	pthread_t thread_id[NTHREADS];

	union semun  
	{
		int val;
		struct semid_ds *buf;
		ushort array [1];
	} sem_attr;

	struct sembuf ingreSems[AMOUNT_INGRE];

	srand(time(NULL)); 

	// Check amount
	if(argc != 3)
	{
		printf("This usage is wrong. \n");
		printf("Usage: ./program -i filePath\n");
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
				printf("Usage: ./program -i filePath\n");
				exit(EXIT_FAILURE);
		}
	}

	// Open filePath
	int fd = open(filePath, O_RDONLY);
	if(fd == -1)
	{
		fprintf(stderr, "\nerrno = %d: %s open filePath\n\n", errno, strerror(errno));
		exit(EXIT_FAILURE);
	}

	// Create temporary file via mkstemp
	int fd2 = mkstemp(temp);
	if(fd2 == -1)
	{
		fprintf(stderr, "\nerrno = %d: %s mkstemp could not create file\n\n", errno, strerror(errno));
		exit(EXIT_FAILURE);
	}

	// Init posix semaphore
	if(sem_init(&sem_mut, 0, 0) == -1)
	{
		fprintf(stderr, "\nerrno = %d: %s sem_init\n\n", errno, strerror (errno));
		ExitFailure();
	}
	// Ftok for sys-V semaphore. convert a pathname and a project identifier to a System V
	if((s_key = ftok(temp, 'a')) == -1)
	{
		fprintf(stderr, "\nerrno = %d: %s ftok\n\n", errno, strerror(errno));
		ExitFailure();
	}
	// semget for sys-V semaphore. get a System V semaphore set identifier
	if((sem_sync = semget(s_key, AMOUNT_INGRE, 0660 | IPC_CREAT)) == -1)
	{
		fprintf(stderr, "\nerrno = %d: %s semget\n\n", errno, strerror(errno));
		ExitFailure();
	}
	// semctl for sys-V semaphore. System V semaphore control operations
	// initialize for each ingredients. initial value is 0.
	sem_attr.val = 0;
	for(i = 0; i < AMOUNT_INGRE; i++)
	{
		if(semctl(sem_sync, i, SETVAL, sem_attr) == -1)
		{
			fprintf(stderr, "\nerrno = %d: %s semctl SETVAL\n\n", errno, strerror(errno));
			ExitFailure();
		}
	}
	// The flag is set to the default value.
	for(i = 0; i < AMOUNT_INGRE; i++)
		ingreSems[i].sem_flg = 0;

	for(i = 0; i < NTHREADS; i++)
	{
		indexThread[i] = malloc(sizeof(int));
	}

	flag2 = 1;
	// Create thread. Thread take number.
	for(i = 0; i < NTHREADS; i++)
	{
		*(indexThread[i]) = i;
		pthread_create( &thread_id[i], NULL, thread_function, indexThread[i]);
	}

	for( ; ; )
	{
		// Read 3 byte from file.
		// First and Second bytes indicate ingredients. Third byte indicates newline '\n'
		// For end of file, there may be no newline '\n'
		while(((bytesread = read(fd, buf, 3)) == -1) &&(errno == EINTR));
		if(bytesread <= 0)
		{
			while(((bytesread = read(fd, buf, 2)) == -1) &&(errno == EINTR));
			if(bytesread <= 0)
			{
				flag = 0;
				break;
			}
		}

		// The ingredients will be delivered to a data structure(array).
		ingredients[0] = buf[0];
		ingredients[1] = buf[1];

		if((ingredients[0] == 'M' && ingredients[1] == 'F') ||(ingredients[0] == 'F' && ingredients[1] == 'M'))
			x = 0;
		else if((ingredients[0] == 'M' && ingredients[1] == 'W') ||(ingredients[0] == 'W' && ingredients[1] == 'M'))
			x = 1;
		else if((ingredients[0] == 'M' && ingredients[1] == 'S') ||(ingredients[0] == 'S' && ingredients[1] == 'M'))
			x = 2;
		else if((ingredients[0] == 'F' && ingredients[1] == 'W') ||(ingredients[0] == 'W' && ingredients[1] == 'F'))
			x = 3;
		else if((ingredients[0] == 'F' && ingredients[1] == 'S') ||(ingredients[0] == 'S' && ingredients[1] == 'F'))
			x = 4;
		else if((ingredients[0] == 'W' && ingredients[1] == 'S') ||(ingredients[0] == 'S' && ingredients[1] == 'W'))
			x = 5;


		// The values of the related ingredients are increased by 1.
		printf("the wholesaler delivers %s and %s\n", types[x][0], types[x][1]);
		ingreSems[0].sem_num = comb[x][0];
		ingreSems[0].sem_op = 0;
		ingreSems[1].sem_num = comb[x][1];
		ingreSems[1].sem_op = 0;

		ingreSems[2].sem_num = comb[x][0];
		ingreSems[2].sem_op = 1;
		ingreSems[3].sem_num = comb[x][1];
		ingreSems[3].sem_op = 1;
		if(semop(sem_sync, ingreSems, AMOUNT_INGRE) == -1)
		{
			fprintf(stderr, "\nerrno = %d: %s semop: sem_sync\n\n", errno, strerror(errno));
			ExitFailure();
	    }

		printf("the wholesaler is waiting for the dessert\n");

		// The dessert is expected to be ready.
		if(sem_wait(&sem_mut) == -1)
		{
			fprintf(stderr, "\nerrno = %d: %s sem_wait\n\n", errno, strerror (errno));
			ExitFailure();
		}

		printf("the wholesaler has obtained the dessert and left to sell it\n");
	}

	// Once the wholesaler is done, she/he’ll need a way to notify the chefs that there won’t be any more ingredients.
	notify();

	// Join threads
	for(j = 0; j < NTHREADS; j++)
	{
		if(pthread_join(thread_id[j], NULL) != 0)
		{
			fprintf(stderr, "\nerrno = %d: %s pthread_join\n\n", errno, strerror(errno));
			ExitFailure();
		} 
	}

	// Close files
	if(close(fd) == -1)
	{
		fprintf(stderr, "\nerrno = %d: %s close filePath\n\n", errno, strerror(errno));
		ExitFailure();
	}
	if(close(fd2) == -1)
	{
		fprintf(stderr, "\nerrno = %d: %s close temporary file\n\n", errno, strerror(errno));
		ExitFailure();
	}

	// unlink temproray file.
	unlink(temp);

	printf("\n----- Program was successfully completed. -----\n\n");
	return 0;
}

// All chef threads will execute the same function.
void *thread_function(void *args)
{
	int random, i;
	int x =((int*)args)[0];
	char item;

	struct sembuf ingreSems[LACK_ITEMS];
	ingreSems[0].sem_flg = 0;
	ingreSems[1].sem_flg = 0;

	while(flag == 1)
	{
		// If there are, the related ingredients decreases their values by 1, otherwise it waits until it happens.
		printf("chef%d is waiting for %s and %s\n", x+1, types[x][0], types[x][1]);
		ingreSems[0].sem_num = comb[x][0];
		ingreSems[0].sem_op = -1;
		ingreSems[1].sem_num = comb[x][1];
		ingreSems[1].sem_op = -1;
		
		if(semop(sem_sync, ingreSems, LACK_ITEMS) == -1)
		{
			fprintf(stderr, "\nerrno = %d: %s semop: sem_sync\n\n", errno, strerror(errno));
			ExitFailure();
	    }
	    // Check wholesaler is done.
	    // If it is done, chefs will be done.
	    if(flag == 0)
	    	break;

	    // Take ingredients from array.
	    for(i = 0; i < LACK_ITEMS; i++)
	    {
	    	item = ingredients[i];
	    	if(item == 'M')
				printf("chef%d has taken the milk\n", x+1);
			else if(item == 'F')
				printf("chef%d has taken the flour\n", x+1);
			else if(item == 'W')
				printf("chef%d has taken the walnuts\n", x+1);
			else if(item == 'S')
				printf("chef%d has taken the sugar\n", x+1);

	    }
	
		printf("chef%d is preparing the dessert\n", x+1);

		// The chef can simulate dessert preparation by sleeping for a random number of seconds (1 to 5 inclusive).
		random =(rand() % 5) + 1;
		sleep(random);

		printf("chef%d has delivered the dessert to the wholesaler\n", x+1);
		// The dessert is ready, wake wholesaler.
		if(sem_post(&sem_mut) == -1)
		{
			fprintf(stderr, "\nerrno = %d: %s sem_post\n\n", errno, strerror (errno));
			ExitFailure();
		}
		
	}

	printf("chef %d finished preparing desserts.\n", x+1);

	free(args);

	pthread_exit(NULL);
}

// It ensures that all pending threads are awakened.
void notify()
{
	int i;
	struct sembuf ingreSems[AMOUNT_INGRE];

	// By increasing each ingredients by 3 each, all kinds of threads are released without waiting.
	for(i = 0; i < AMOUNT_INGRE; i++)
	{
		ingreSems[i].sem_flg = 0;
		ingreSems[i].sem_num = i;
		ingreSems[i].sem_op = 3;
	}

	printf("wholesaler finished supplying ingredients. \n");

	if(semop(sem_sync, ingreSems, AMOUNT_INGRE) == -1)
	{
		fprintf(stderr, "\nerrno = %d: %s semop: sem_sync\n\n", errno, strerror(errno));
		ExitFailure();
    }
}

// When there is an error, it releases the allocated memory and deletes the temporary file.
void ExitFailure()
{
	if(flag2 == 1)
	{
		int i;
		for(i = 0; i < NTHREADS; i++)
			free(indexThread[i]);
	}
	
	unlink(temp);

	exit(EXIT_FAILURE);
}
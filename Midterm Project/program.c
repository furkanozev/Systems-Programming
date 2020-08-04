#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <semaphore.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>

#define SupCook "/supplier-cooks3"
#define CookStud "/cooks-students3"
#define StudTable "/students-table3"
#define StudTables "/students-tables3"
#define common "/common-mutex3"

#define SIZE 1024

/*
	For relation between supplier and cooks.
	SemPP, SemPC, semPD are semaphores for each plate type.
	plateP, plateC and plateD are keep amount of each type plates.
	total keeps amount of plate that supplied from supplier.
	totaltake keeps amount of plate that took from cooks.
*/
typedef struct
{
	sem_t mutex, empty, full;
	sem_t semPP, semPC, semPD;
	int plateP, plateC, plateD;
	int total;
	int totaltake;
}structSC;

/*
	For relation between cooks and student.
	Meal keeps the number of each meal provided from cooks.
	Meal increases by 1 with every 3 types of plates provided.
	Student keeps number of student that waiting at counter.
	plateP, plateC and plateD are keep amount of each type plates.
	tempP, tempC and tempD used for setting the meals.
	total keeps amount of plate that supplied from supplier.
	totaltake keeps amount of plate that took from cooks.
*/
typedef struct
{
	sem_t mutex, empty, full;
	sem_t meal;
	int student;
	int plateP, plateC, plateD;
	int tempP, tempC, tempD;
	int total;
	int totaltake;
}structCSt;

/*
	For relation between students.  It is also used for its relationship with the table.
	UGmutex is used for the relationship between undergraduate and graduate students.
	countG is used for keeping number of graduate students that waiting at counter.
	numberG and numberU used for keeping amount of each type students.
	emptyTables is used for keeping amount of empty tables.
*/
typedef struct
{
	sem_t mutex, empty, full;
	sem_t UGmutex;
	int countG;
	int numberG, numberU;
	int emptyTables;
}structStTable;


void cook(int number);
void studentU(int number);
void studentG(int number);
void supplier();
void createSupCook();
void closeSupCook();
void createCookStu();
void closeCookStu();
void createStuTable();
void closeStuTable();
void createTableArray();
void closeTableArray();
void createCommon();
void closeCommon();
void sigchldHandler(int sig);
void ExitFailure(char* mes);
void printMessage(char* mes);
void printMessage2(char* mes);

// Input values
int varN = 0, varM = 0, varT = 0, varS = 0, varL = 0, varU = 0, varG = 0, varK = 0;
int flagSC = 0;

// Semaphores and their file descriptor.
structSC *semaphores;
int fd;

structCSt *sem_cookstu;
int fd2;

structStTable *sem_stutable;
int fd3;

// I used an array shared among students for table.
int *tableArray;
int fd4;

// I used this mutex for write operation. This mutex is used in any printing process.
sem_t* commonMutex;
int fd5;

// Input path
char* filePath;
int fdin;

// For avoiding zombie processes.
// Number of children started but not yet waited on.
static volatile int numLiveChild = 0;

int main(int argc, char* argv[])
{
	int opt;
	int flag = 0;

	// Check amount
	if(argc != 15)
	{
		char buf[SIZE];
		sprintf(buf, "This usage is wrong. \nUsage: ./program -N NUMBER -T NUMBER -S NUMBER -L NUMBER -U NUMBER -G NUMBER -F filePath\n");
		printMessage2(buf);
		exit(EXIT_FAILURE);
	}

	// I used the getopt() library method for parsing commandline arguments.
	while ((opt = getopt (argc, argv, "NTSLUGF")) != -1)
  	{
	    switch (opt)
	    {
			case 'N':
				varN = atoi(argv[optind]);
				break;
			case 'T':
				varT = atoi(argv[optind]);
				break;
			case 'S':
				varS = atoi(argv[optind]);
				break;
			case 'L':
				varL = atoi(argv[optind]);
				break;
			case 'U':
				varU = atoi(argv[optind]);
				break;
			case 'G':
				varG = atoi(argv[optind]);
				break;
			case 'F':
				filePath = argv[optind];
				break;
			// If the command line arguments are missing/invalid your program must print usage information and exit.
			default:
			{
				char buf[SIZE];
				sprintf(buf, "This usage is wrong. \nUsage: ./program -N NUMBER -T NUMBER -S NUMBER -L NUMBER -U NUMBER -G NUMBER -F filePath\n");
				printMessage2(buf);
				exit(EXIT_FAILURE);
			}
	    }
	}

	// calculate number of students
	varM = varU + varG;

	// Check all constraints.
	// if any, it prints the restrictions, and ends the program.
	if(varN <= 2)
	{
		char buf[SIZE];
		sprintf(buf, "Obey this constraint: (N > 2)\n");
		printMessage2(buf);
		flag = 1;
	}
	if(varS <= 3)
	{
		char buf[SIZE];
		sprintf(buf, "Obey this constraint: (S > 3)\n");
		printMessage2(buf);
		flag = 1;
	}
	if(varT < 1)
	{
		char buf[SIZE];
		sprintf(buf, "Obey this constraint: (T >= 1)\n");
		printMessage2(buf);
		flag = 1;
	}
	if(varM <= varN || varM <= 2)
	{
		char buf[SIZE];
		sprintf(buf, "Obey this constraint: (M > N > 2)\n");
		printMessage2(buf);
		flag = 1;
	}
	if(varM <= varT || varT < 1)
	{
		char buf[SIZE];
		sprintf(buf, "Obey this constraint: (M > T >= 1)\n");
		printMessage2(buf);
		flag = 1;
	}
	if(varL < 3)
	{
		char buf[SIZE];
		sprintf(buf, "Obey this constraint: (L >= 3)\n");
		printMessage2(buf);
		flag = 1;
	}
	if(varG < 1)
	{
		char buf[SIZE];
		sprintf(buf, "Obey this constraint: (G >= 1)\n");
		printMessage2(buf);
		flag = 1;
	}
	if(varU <= varG && varU <= 1)
	{
		char buf[SIZE];
		sprintf(buf, "Obey this constraint: (varU > varG >= 1)\n");
		printMessage2(buf);
		flag = 1;
	}
	if(flag == 1)
		exit(EXIT_FAILURE);

	// If there is no error, open filePath.
	fdin = open(filePath, O_RDONLY);
	if(fdin == -1)
	{
		char buf[SIZE];
		sprintf(buf, "\nerrno = %d: %s open filePath\n\n", errno, strerror(errno));
		printMessage2(buf);
		exit(EXIT_FAILURE);
	}
	// Calculate kitchen capacity.
	varK = (2 * varL * varM) + 1;
	int process;
	int totalprocess = varN + varM + 1;

	numLiveChild = totalprocess;

	sigset_t blockMask, emptyMask;
	struct sigaction sa;

	/* 
		For handle SIGCHLD, Change SIGCHDL action.
		Because default action of SIGCHLD is immediately termination
		But it must be wait all child terminate.
	*/
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	sa.sa_handler = sigchldHandler;
	if (sigaction(SIGCHLD, &sa, NULL) == -1)
	{
		char buf[SIZE];
		sprintf(buf, "\nerrno = %d: %s sigaction\n\n", errno, strerror(errno));
		printMessage2(buf);
		exit(EXIT_FAILURE);
	}

	/*
		Block SIGCHLD to prevent its delivery if a child terminates
		before the parent commences the sigsuspend() loop below
	*/

	sigemptyset(&blockMask);
	sigaddset(&blockMask, SIGCHLD);
	if (sigprocmask(SIG_SETMASK, &blockMask, NULL) == -1)
	{
		char buf[SIZE];
		sprintf(buf, "\nerrno = %d: %s sigprocmask\n\n", errno, strerror(errno));
		printMessage2(buf);
		exit(EXIT_FAILURE);
	}

	// Share memory and init semaphores.
	createSupCook();
	createCookStu();
	createStuTable();
	createTableArray();
	createCommon();

	// Fork child process
	for(process = 0; process < totalprocess; process++)
	{
		switch(fork())
		{
			// If there is an error, print message and kill all parent and child processes.
			case -1:
				sem_wait(commonMutex);
				char buf[SIZE];
				sprintf(buf, "\nerrno = %d: %s fork\n\n", errno, strerror(errno));
				ExitFailure(buf);

			case 0:
				if(process == 0)
				{
					supplier();
					exit(EXIT_SUCCESS);
				}
				else if(process <= varN)
				{
					cook(process);
					exit(EXIT_SUCCESS);
				}
				else if(process - varN <= varU)
				{
					studentU(process - varN);
					exit(EXIT_SUCCESS);
				}
				else
				{
					studentG(process - varN - varU);
					exit(EXIT_SUCCESS);
				}
		}
	}

	// Parent comes here: wait for SIGCHLD until all children are dead.
	sigemptyset(&emptyMask);
	while(numLiveChild > 0)
	{
		if(sigsuspend(&emptyMask) == -1 && errno != EINTR)
		{
			sem_wait(commonMutex);
			char buf[SIZE];
			sprintf(buf, "\nerrno = %d: %s sigsuspend\n\n", errno, strerror(errno));
			ExitFailure(buf);
		}
	}

	// Close shared memory, and unlink operations.
	closeSupCook();
	closeCookStu();
	closeStuTable();
	closeTableArray();
	closeCommon();

	// Close filePath.
	if(close(fdin) == -1)
	{
		char buf[SIZE];
		sprintf(buf, "\nerrno = %d: %s close filePath\n\n", errno, strerror(errno));
		printMessage2(buf);
		exit(EXIT_FAILURE);
	}

	// Program was successfully completed.
	{
		char buf[SIZE];
		sprintf(buf, "\n----- The program was successfully completed. -----\n\n");
		printMessage(buf);
	}

	return 0;
}

// Undergraduate students. It takes the student number as a parameter.
void studentU(int number)
{
	int value, round = 1;
	int i;
	int currentTable;

	// Check if it have reached the maximum number of rounds.
  	while(round <= varL)
  	{
  		/*
  			Graduate students have priority at the counter over undergraduates.
  			As long as there are graduates in front of the counter waiting for their food, no undergraduate is allowed to be serviced.
  		*/

  		// At this stage, since the number of items in the counter and number of students at the counter will be printed, so this region is used as the critical area.
  		if(sem_wait(&sem_cookstu->mutex) == -1)
		{
			sem_wait(commonMutex);
			char buf[SIZE];
			sprintf(buf, "\nerrno = %d: %s sem_wait\n\n", errno, strerror(errno));
			ExitFailure(buf);
		}
		sem_stutable->numberU += 1;
		sem_cookstu->student += 1;
  		value = sem_cookstu->plateP + sem_cookstu->plateC + sem_cookstu->plateD;
  		
  		//  necessary printing, decreasing are done.
  		{
  			sem_wait(commonMutex);
			char buf[SIZE];
  			sprintf(buf, "Undergraduate Student %d is going to the counter (round %d) - # of students at counter: U:%d,G:%d=%d and counter items P:%d,C:%d,D:%d=%d\n", number, round, sem_stutable->numberU, sem_stutable->numberG, sem_cookstu->student, sem_cookstu->plateP, sem_cookstu->plateC, sem_cookstu->plateD, value);
  			printMessage(buf);
  			sem_post(commonMutex);
  		}

  		// This stage is over and exits from the critical region.
		if(sem_post(&sem_cookstu->mutex) == -1)
  		{
  			sem_wait(commonMutex);
  			char buf[SIZE];
			sprintf(buf, "\nerrno = %d: %s sem_post\n\n", errno, strerror(errno));
			ExitFailure(buf);
  		}

  		// If there is no graduate student, it will continue. Otherwise will wait until unlock mutex.
  		if(sem_wait(&sem_stutable->UGmutex) == -1)
		{
			sem_wait(commonMutex);
			char buf[SIZE];
			sprintf(buf, "\nerrno = %d: %s sem_wait\n\n", errno, strerror(errno));
			ExitFailure(buf);
		}

		// It expects to be provided with 3 types of food or it takes it if any.
		if(sem_wait(&sem_cookstu->meal) == -1)
		{
			sem_wait(commonMutex);
			char buf[SIZE];
			sprintf(buf, "\nerrno = %d: %s sem_wait\n\n", errno, strerror(errno));
			ExitFailure(buf);
		}

		// At this stage, since the student got food, then final situation of counter and amount of empty tables will be printed.
		// this section is used as a critical region between cooks and students.
		if(sem_wait(&sem_cookstu->mutex) == -1)
		{
			sem_wait(commonMutex);
			char buf[SIZE];
			sprintf(buf, "\nerrno = %d: %s sem_wait\n\n", errno, strerror(errno));
			ExitFailure(buf);
		}

		//  necessary printing, decreasing are done.
		sem_stutable->numberU -= 1;
		sem_cookstu->student -= 1;
		value = sem_cookstu->plateP + sem_cookstu->plateC + sem_cookstu->plateD;
		
		{
			sem_wait(commonMutex);
			char buf[SIZE];
			sprintf(buf, "Undergraduate Student %d got food and is going to get a table (round %d) - # of students at counter: U:%d,G:%d=%d and # of empty tables:%d\n", number, round, sem_stutable->numberU, sem_stutable->numberG, sem_cookstu->student, sem_stutable->emptyTables);
			printMessage(buf);
			sem_post(commonMutex);
		}

		sem_cookstu->plateP -= 1;
		sem_cookstu->plateC -= 1;
		sem_cookstu->plateD -= 1;

		// Since 3 plates are taken from the counter, 3 places are opened.
		for(i = 0; i < 3; i++)
		{
			if(sem_wait(&sem_cookstu->full) == -1)
	  		{
	  			sem_wait(commonMutex);
	  			char buf[SIZE];
				sprintf(buf, "\nerrno = %d: %s sem_wait\n\n", errno, strerror(errno));
				ExitFailure(buf);
	  		}
	  		if(sem_post(&sem_cookstu->empty) == -1)
	  		{
	  			sem_wait(commonMutex);
	  			char buf[SIZE];
				sprintf(buf, "\nerrno = %d: %s sem_post\n\n", errno, strerror(errno));
				ExitFailure(buf);
	  		}
		}
		
		// After receiving the meal, it unlocks so other students can take it.
		if(sem_post(&sem_stutable->UGmutex) == -1)
		{
			sem_wait(commonMutex);
			char buf[SIZE];
			sprintf(buf, "\nerrno = %d: %s sem_post\n\n", errno, strerror(errno));
			ExitFailure(buf);
		}

		// This stage is over and exits from the critical region.
		if(sem_post(&sem_cookstu->mutex) == -1)
  		{
  			sem_wait(commonMutex);
  			char buf[SIZE];
			sprintf(buf, "\nerrno = %d: %s sem_post\n\n", errno, strerror(errno));
			ExitFailure(buf);
  		}

  		// It waits until there is a free table.
  		if(sem_wait(&sem_stutable->full) == -1)
  		{
  			sem_wait(commonMutex);
  			char buf[SIZE];
			sprintf(buf, "\nerrno = %d: %s sem_wait\n\n", errno, strerror(errno));
			ExitFailure(buf);
  		}

  		// At this stage, since the student sit table, then final situation of tables will be printed.
		// this section is used as a critical region between students.
  		if(sem_wait(&sem_stutable->mutex) == -1)
  		{
  			sem_wait(commonMutex);
  			char buf[SIZE];
			sprintf(buf, "\nerrno = %d: %s sem_wait\n\n", errno, strerror(errno));
			ExitFailure(buf);
  		}

  		// Determine number of tables. If it is free, it's flag is 0. If it is full, it's flag is 1.
  		for(i = 0; i < varT && tableArray[i] != 0; i++);

  		//  necessary printing, and increasing are done.

  		tableArray[i] = 1;
  		currentTable = i + 1;
  		sem_stutable->emptyTables -= 1;

  		{
			sem_wait(commonMutex);
			char buf[SIZE];
			sprintf(buf, "Undergraduate Student %d sat at table %d to eat (round %d) - empty tables:%d\n", number, currentTable, round, sem_stutable->emptyTables);
			printMessage(buf);
			sem_post(commonMutex);
		}
  		// This stage is over and exits from the critical region.
  		if(sem_post(&sem_stutable->mutex) == -1)
  		{
  			sem_wait(commonMutex);
  			char buf[SIZE];
			sprintf(buf, "\nerrno = %d: %s sem_post\n\n", errno, strerror(errno));
			ExitFailure(buf);
  		}

  		// Reduces the number of empty tables by 1.
  		if(sem_post(&sem_stutable->empty) == -1)
  		{
  			sem_wait(commonMutex);
  			char buf[SIZE];
			sprintf(buf, "\nerrno = %d: %s sem_post\n\n", errno, strerror(errno));
			ExitFailure(buf);
  		}


  		if(sem_wait(&sem_stutable->empty) == -1)
  		{
  			sem_wait(commonMutex);
  			char buf[SIZE];
			sprintf(buf, "\nerrno = %d: %s sem_wait\n\n", errno, strerror(errno));
			ExitFailure(buf);
  		}

  		// At this stage, since the student left table, then final situation of tables will be printed.
		// this section is used as a critical region between students.
  		if(sem_wait(&sem_stutable->mutex) == -1)
  		{
  			sem_wait(commonMutex);
  			char buf[SIZE];
			sprintf(buf, "\nerrno = %d: %s sem_wait\n\n", errno, strerror(errno));
			ExitFailure(buf);
  		}

  		tableArray[i] = 0;
  		sem_stutable->emptyTables += 1;
  		//  necessary printing, and increasing are done.
  		{
			sem_wait(commonMutex);
			char buf[SIZE];
			sprintf(buf, "Undergraduate Student %d left table %d to eat again (round %d) - empty tables:%d\n", number, currentTable, round, sem_stutable->emptyTables);
			printMessage(buf);
			sem_post(commonMutex);
		}
		
		// This stage is over and exits from the critical region.
	  	if(sem_post(&sem_stutable->mutex) == -1)
  		{
  			sem_wait(commonMutex);
			char buf[SIZE];
			sprintf(buf, "\nerrno = %d: %s sem_post\n\n", errno, strerror(errno));
			ExitFailure(buf);
  		}

  		// One more table was free.
  		if(sem_post(&sem_stutable->full) == -1)
  		{
  			sem_wait(commonMutex);
  			char buf[SIZE];
			sprintf(buf, "\nerrno = %d: %s sem_post\n\n", errno, strerror(errno));
			ExitFailure(buf);
  		}

  		round += 1;
  	}

  	// The undergraduate student has finished its job.
  	{
		sem_wait(commonMutex);
		char buf[SIZE];
		sprintf(buf, "Undergraduate Student %d is done eating L=%d times - going home - GOODBYE!!!\n", number, round-1);
		printMessage(buf);
		sem_post(commonMutex);
	}

	exit(EXIT_SUCCESS);
}

// Graduate students. It takes the student number as a parameter.
void studentG(int number)
{
	int value, round = 1;
	int i;
	int currentTable;

	// Check if it have reached the maximum number of rounds.
  	while(round <= varL)
  	{	
  		/*
  			Graduate students have priority at the counter over undergraduates.
  			As long as there are graduates in front of the counter waiting for their food, no undergraduate is allowed to be serviced.
  		*/

  		// At this stage, since the number of items in the counter and number of students at the counter will be printed, so this region is used as the critical area.
  		if(sem_wait(&sem_cookstu->mutex) == -1)
		{
			sem_wait(commonMutex);
			char buf[SIZE];
			sprintf(buf, "\nerrno = %d: %s sem_wait\n\n", errno, strerror(errno));
			ExitFailure(buf);
		}
		/*
			If there are no graduate students waiting at the counter, UGmutex locks in. And enter the critical section again.
			If there is, mutex is already locked. If it locks again, it causes deadlock.
		*/
		if(sem_stutable->countG == 0)
		{
			if(sem_post(&sem_cookstu->mutex) == -1)
			{
				sem_wait(commonMutex);
				char buf[SIZE];
				sprintf(buf, "\nerrno = %d: %s sem_post\n\n", errno, strerror(errno));
				ExitFailure(buf);
			}

			if(sem_wait(&sem_stutable->UGmutex) == -1)
			{
				sem_wait(commonMutex);
				char buf[SIZE];
				sprintf(buf, "\nerrno = %d: %s sem_wait\n\n", errno, strerror(errno));
				ExitFailure(buf);
			}
			if(sem_wait(&sem_cookstu->mutex) == -1)
			{
				sem_wait(commonMutex);
				char buf[SIZE];
				sprintf(buf, "\nerrno = %d: %s sem_wait\n\n", errno, strerror(errno));
				ExitFailure(buf);
			}
		}

		//  necessary printing, increasing are done.
		sem_stutable->numberG += 1;
		sem_stutable->countG += 1;
		
		sem_cookstu->student += 1;
  		value = sem_cookstu->plateP + sem_cookstu->plateC + sem_cookstu->plateD;

  		{
			sem_wait(commonMutex);
			char buf[SIZE];
			sprintf(buf, "Graduate Student %d is going to the counter (round %d) - # of students at counter: U:%d,G:%d=%d and counter items P:%d,C:%d,D:%d=%d\n", number, round, sem_stutable->numberU, sem_stutable->numberG, sem_cookstu->student, sem_cookstu->plateP, sem_cookstu->plateC, sem_cookstu->plateD, value);
			printMessage(buf);
			sem_post(commonMutex);
		}
  		// This stage is over and exits from the critical region.
		if(sem_post(&sem_cookstu->mutex) == -1)
  		{
  			sem_wait(commonMutex);
  			char buf[SIZE];
			sprintf(buf, "\nerrno = %d: %s sem_post\n\n", errno, strerror(errno));
			ExitFailure(buf);
  		}

  		// It expects to be provided with 3 types of food or it takes it if any.
		if(sem_wait(&sem_cookstu->meal) == -1)
		{
			sem_wait(commonMutex);
			char buf[SIZE];
			sprintf(buf, "\nerrno = %d: %s sem_wait\n\n", errno, strerror(errno));
			ExitFailure(buf);
		}

		// At this stage, since the student got food, then final situation of counter and amount of empty tables will be printed.
		// this section is used as a critical region between cooks and students.
		if(sem_wait(&sem_cookstu->mutex) == -1)
		{
			sem_wait(commonMutex);
			char buf[SIZE];
			sprintf(buf, "\nerrno = %d: %s sem_wait\n\n", errno, strerror(errno));
			ExitFailure(buf);
		}

		//  necessary printing, decreasing are done.
		sem_stutable->numberG -= 1;
		sem_cookstu->student -= 1;
		value = sem_cookstu->plateP + sem_cookstu->plateC + sem_cookstu->plateD;

		{
			sem_wait(commonMutex);
			char buf[SIZE];
			sprintf(buf, "Graduate Student %d got food and is going to get a table (round %d) - # of students at counter: U:%d,G:%d=%d and # of empty tables:%d\n", number, round, sem_stutable->numberU, sem_stutable->numberG, sem_cookstu->student, sem_stutable->emptyTables);
			printMessage(buf);
			sem_post(commonMutex);
		}
				
		sem_cookstu->plateP -= 1;
		sem_cookstu->plateC -= 1;
		sem_cookstu->plateD -= 1;

		// Since 3 plates are taken from the counter, 3 places are opened.
		for(i = 0; i < 3; i++)
		{
			if(sem_wait(&sem_cookstu->full) == -1)
	  		{
	  			sem_wait(commonMutex);
	  			char buf[SIZE];
				sprintf(buf, "\nerrno = %d: %s sem_wait\n\n", errno, strerror(errno));
				ExitFailure(buf);
	  		}
	  		if(sem_post(&sem_cookstu->empty) == -1)
	  		{
	  			sem_wait(commonMutex);
	  			char buf[SIZE];
				sprintf(buf, "\nerrno = %d: %s sem_post\n\n", errno, strerror(errno));
				ExitFailure(buf);
	  		}
		}

		// If there are no other graduate students at the counter, UGmutex is unlocked so undergraduate students can start receiving meals.
		sem_stutable->countG -= 1;
		if(sem_stutable->countG == 0)
		{
			if(sem_post(&sem_stutable->UGmutex) == -1)
			{
				sem_wait(commonMutex);
				char buf[SIZE];
				sprintf(buf, "\nerrno = %d: %s sem_post\n\n", errno, strerror(errno));
				ExitFailure(buf);
			}
		}
		// This stage is over and exits from the critical region.
		if(sem_post(&sem_cookstu->mutex) == -1)
  		{
  			sem_wait(commonMutex);
  			char buf[SIZE];
			sprintf(buf, "\nerrno = %d: %s sem_post\n\n", errno, strerror(errno));
			ExitFailure(buf);
  		}

  		// It waits until there is a free table.
  		if(sem_wait(&sem_stutable->full) == -1)
  		{
  			sem_wait(commonMutex);
  			char buf[SIZE];
			sprintf(buf, "\nerrno = %d: %s sem_wait\n\n", errno, strerror(errno));
			ExitFailure(buf);
  		}
  		// At this stage, since the student sit table, then final situation of tables will be printed.
		// this section is used as a critical region between students.
  		if(sem_wait(&sem_stutable->mutex) == -1)
  		{
  			sem_wait(commonMutex);
  			char buf[SIZE];
			sprintf(buf, "\nerrno = %d: %s sem_wait\n\n", errno, strerror(errno));
			ExitFailure(buf);
  		}

  		// Determine number of tables. If it is free, it's flag is 0. If it is full, it's flag is 1.
  		for(i = 0; i < varT && tableArray[i] != 0; i++);

  		tableArray[i] = 1;
  		currentTable = i + 1;
  		sem_stutable->emptyTables -= 1;

  		//  necessary printing is done.
  		{
			sem_wait(commonMutex);
			char buf[SIZE];
			sprintf(buf, "Graduate Student %d sat at table %d to eat (round %d) - empty tables:%d\n", number, currentTable, round, sem_stutable->emptyTables);
			printMessage(buf);
			sem_post(commonMutex);
		}
  		
  		// This stage is over and exits from the critical region.
  		if(sem_post(&sem_stutable->mutex) == -1)
  		{
  			sem_wait(commonMutex);
  			char buf[SIZE];
			sprintf(buf, "\nerrno = %d: %s sem_post\n\n", errno, strerror(errno));
			ExitFailure(buf);
  		}
  		// Reduces the number of empty tables by 1.
  		if(sem_post(&sem_stutable->empty) == -1)
  		{
  			sem_wait(commonMutex);
  			char buf[SIZE];
			sprintf(buf, "\nerrno = %d: %s sem_post\n\n", errno, strerror(errno));
			ExitFailure(buf);
  		}


  		if(sem_wait(&sem_stutable->empty) == -1)
  		{
  			sem_wait(commonMutex);
  			char buf[SIZE];
			sprintf(buf, "\nerrno = %d: %s sem_wait\n\n", errno, strerror(errno));
			ExitFailure(buf);
  		}
  		// At this stage, since the student left table, then final situation of tables will be printed.
		// this section is used as a critical region between students.
  		if(sem_wait(&sem_stutable->mutex) == -1)
  		{
  			sem_wait(commonMutex);
  			char buf[SIZE];
			sprintf(buf, "\nerrno = %d: %s sem_wait\n\n", errno, strerror(errno));
			ExitFailure(buf);
  		}

  		//  necessary printing, and increasing are done.
  		tableArray[i] = 0;
  		sem_stutable->emptyTables += 1;

  		{
			sem_wait(commonMutex);
			char buf[SIZE];
			sprintf(buf, "Graduate Student %d left table %d to eat again (round %d) - empty tables:%d\n", number, currentTable, round, sem_stutable->emptyTables);
			printMessage(buf);
			sem_post(commonMutex);
		}
  		
  		// This stage is over and exits from the critical region.		
	  	if(sem_post(&sem_stutable->mutex) == -1)
  		{
  			sem_wait(commonMutex);
  			char buf[SIZE];
			sprintf(buf, "\nerrno = %d: %s sem_post\n\n", errno, strerror(errno));
			ExitFailure(buf);
  		}
  		// One more table was free.
  		if(sem_post(&sem_stutable->full) == -1)
  		{
  			sem_wait(commonMutex);
  			char buf[SIZE];
			sprintf(buf, "\nerrno = %d: %s sem_post\n\n", errno, strerror(errno));
			ExitFailure(buf);
  		}

  		round += 1;
  	}

  	// The graduate student has finished its job.
  	{
		sem_wait(commonMutex);
		char buf[SIZE];
		sprintf(buf, "Graduate Student %d is done eating L=%d times - going home - GOODBYE!!!\n", number, round-1);
		printMessage(buf);
		sem_post(commonMutex);
	}
  	
	exit(EXIT_SUCCESS);
}

// Cook. It takes the cook number as a parameter.
void cook(int number)
{
	int value, totalPlates = 0, flag = 0;
	int plate = 1;

  	totalPlates = 3 * varL * varM;

  	/*
  		It is checked whether all items are supplied by the supplier.
		If supplied, the number of items in the kitchen is checked.
		Thus, it is checked whether there are any plates left.
  	*/
  	while(semaphores->total < totalPlates || (semaphores->plateP + semaphores->plateC + semaphores->plateD) > 0)
  	{
  		// Since the cook is guaranteed to bring plate from kitchen to the counter, so 1 place is reserved from counter.
		if(sem_wait(&sem_cookstu->empty) == -1)
		{
			sem_wait(commonMutex);
			char buf[SIZE];
			sprintf(buf, "\nerrno = %d: %s sem_wait\n\n", errno, strerror(errno));
			ExitFailure(buf);
		}

		// At this stage, since the number of items in the counter will be printed and type of plate will be selected, so this region is used as the critical area.
  		if(sem_wait(&semaphores->mutex) == -1)
  		{
  			sem_wait(commonMutex);
  			char buf[SIZE];
			sprintf(buf, "\nerrno = %d: %s sem_wait\n\n", errno, strerror(errno));
			ExitFailure(buf);
  		}

  		// Increase amount of total plates that taken from kitchen.
  		semaphores->totaltake += 1;

  		// If there is a plate to be brought, it prints the required message.
  		if(semaphores->totaltake <= totalPlates)
  		{
  			value = semaphores->plateP + semaphores->plateC + semaphores->plateD;

  			{
				sem_wait(commonMutex);
				char buf[SIZE];
				sprintf(buf, "Cook %d is going to the kitchen to wait for/get a plate - kitchen items P:%d,C:%d,D:%d=%d\n", number, semaphores->plateP, semaphores->plateC, semaphores->plateD, value);
				printMessage(buf);
				sem_post(commonMutex);
			}
  			
			/*
				Plate type is determined.
				There is a simple tour logic when determining the plate to be taken from the kitchen.
				The same type of plate is brought from the kitchen every 3 times.
				Thus, even if the counter is full, at least 3 varieties will be found.
			*/
  			plate = ((semaphores->totaltake - 1) % 3) + 1;
  		}
  		// Otherwise, it means other cooks already took plates.
  		// So, the cook does not go to the kitchen to prevent deadlock. It's done.
  		else
  		{
  			semaphores->totaltake -= 1;
  			flag = 1;
  			if(sem_post(&sem_cookstu->empty) == -1)
			{
				sem_wait(commonMutex);
				char buf[SIZE];
				sprintf(buf, "\nerrno = %d: %s sem_post\n\n", errno, strerror(errno));
				ExitFailure(buf);
			}
  		}
  		// This stage is over and exits from the critical region.
  		if(sem_post(&semaphores->mutex) == -1)
  		{
  			sem_wait(commonMutex);
  			char buf[SIZE];
			sprintf(buf, "\nerrno = %d: %s sem_post\n\n", errno, strerror(errno));
			ExitFailure(buf);
  		}

  		if(flag == 1) break;

  		/*
  			The plate to be brought in this round is waited.
  			Waiting is not done in the critical region in order not to cause deadlock.
  		*/
  		if(plate == 1)
  		{
  			if(sem_wait(&semaphores->semPP) == -1)
	  		{
	  			sem_wait(commonMutex);
	  			char buf[SIZE];
				sprintf(buf, "\nerrno = %d: %s sem_wait\n\n", errno, strerror(errno));
				ExitFailure(buf);
	  		}
  		}
  		else if(plate == 2)
  		{
  			if(sem_wait(&semaphores->semPC) == -1)
	  		{
	  			sem_wait(commonMutex);
	  			char buf[SIZE];
				sprintf(buf, "\nerrno = %d: %s sem_wait\n\n", errno, strerror(errno));
				ExitFailure(buf);
	  		}
  		}
  		else if(plate == 3)
  		{
  			if(sem_wait(&semaphores->semPD) == -1)
	  		{
	  			sem_wait(commonMutex);
	  			char buf[SIZE];
				sprintf(buf, "\nerrno = %d: %s sem_wait\n\n", errno, strerror(errno));
				ExitFailure(buf);
	  		}
  		}
	  	
  		// 1 plate will taken from the kitchen. The number of plates that can be taken is reduced by 1.
		if(sem_wait(&semaphores->full) == -1)
		{
			sem_wait(commonMutex);
			char buf[SIZE];
			sprintf(buf, "\nerrno = %d: %s sem_wait\n\n", errno, strerror(errno));
			ExitFailure(buf);
		}

		// At this stage, since the 1 item will be reduced to the kitchen and the final situation will be printed,
		// this section is used as a critical region between supplier and cooks.
  		if(sem_wait(&semaphores->mutex) == -1)
  		{
  			sem_wait(commonMutex);
  			char buf[SIZE];
			sprintf(buf, "\nerrno = %d: %s sem_wait\n\n", errno, strerror(errno));
			ExitFailure(buf);
  		}

  		// At this stage, since the 1 item will be reduced to the kitchen and the final situation will be printed,
		// this section is used as a critical region between cooks and students.
  		if(sem_wait(&sem_cookstu->mutex) == -1)
  		{
  			sem_wait(commonMutex);
  			char buf[SIZE];
			sprintf(buf, "\nerrno = %d: %s sem_wait\n\n", errno, strerror(errno));
			ExitFailure(buf);
  		}

  		// Depending on the type of plate, necessary printing, decreasing are done.
  		if(plate == 1)
  		{
  			semaphores->plateP -= 1;
  			value = sem_cookstu->plateP + sem_cookstu->plateC + sem_cookstu->plateD;

  			{
				sem_wait(commonMutex);
				char buf[SIZE];
				sprintf(buf, "Cook %d is going to the counter to deliver soup – counter items P:%d,C:%d,D:%d=%d\n", number, sem_cookstu->plateP, sem_cookstu->plateC, sem_cookstu->plateD, value);
				printMessage(buf);
				sem_post(commonMutex);
			}
  		}
  		else if(plate == 2)
  		{
  			semaphores->plateC -= 1;
  			value = sem_cookstu->plateP + sem_cookstu->plateC + sem_cookstu->plateD;

  			{
				sem_wait(commonMutex);
				char buf[SIZE];
				sprintf(buf, "Cook %d is going to the counter to deliver main course – counter items P:%d,C:%d,D:%d=%d\n", number, sem_cookstu->plateP, sem_cookstu->plateC, sem_cookstu->plateD, value);
				printMessage(buf);
				sem_post(commonMutex);
			}
  		}
  		else
  		{
  			semaphores->plateD -= 1;
  			value = sem_cookstu->plateP + sem_cookstu->plateC + sem_cookstu->plateD;

  			{
				sem_wait(commonMutex);
				char buf[SIZE];
				sprintf(buf, "Cook %d is going to the counter to deliver desert – counter items P:%d,C:%d,D:%d=%d\n", number, sem_cookstu->plateP, sem_cookstu->plateC, sem_cookstu->plateD, value);
				printMessage(buf);
				sem_post(commonMutex);
			}
  		}
  		// This stage is over and exits from the critical region (cooks - students).
  		if(sem_post(&sem_cookstu->mutex) == -1)
  		{
  			sem_wait(commonMutex);
  			char buf[SIZE];
			sprintf(buf, "\nerrno = %d: %s sem_post\n\n", errno, strerror(errno));
			ExitFailure(buf);
  		}	
  		// This stage is over and exits from the critical region (supplier - cooks).
  		if(sem_post(&semaphores->mutex) == -1)
  		{
  			sem_wait(commonMutex);
  			char buf[SIZE];
			sprintf(buf, "\nerrno = %d: %s sem_post\n\n", errno, strerror(errno));
			ExitFailure(buf);
  		}

  		// 1 plate was taken from the kitchen. Therefore, 1 place is opened. 
		if(sem_post(&semaphores->empty) == -1)
		{
			sem_wait(commonMutex);
			char buf[SIZE];
			sprintf(buf, "\nerrno = %d: %s sem_post\n\n", errno, strerror(errno));
			ExitFailure(buf);
		}

		// At this stage, since the new item will be added to the counter and the final situation will be printed,
		// this section is used as a critical region between cooks and students.
  		if(sem_wait(&sem_cookstu->mutex) == -1)
  		{
  			sem_wait(commonMutex);
  			char buf[SIZE];
			sprintf(buf, "\nerrno = %d: %s sem_wait\n\n", errno, strerror(errno));
			ExitFailure(buf);
  		}

  		// Depending on the type of plate, necessary printing, increasing are done.
  		if(plate == 1)
  		{
  			sem_cookstu->plateP += 1;
  			sem_cookstu->tempP += 1;
  			value = sem_cookstu->plateP + sem_cookstu->plateC + sem_cookstu->plateD;

  			{
				sem_wait(commonMutex);
				char buf[SIZE];
				sprintf(buf, "Cook %d placed soup on the counter - counter items P:%d,C:%d,D:%d=%d\n", number, sem_cookstu->plateP, sem_cookstu->plateC, sem_cookstu->plateD, value);
				printMessage(buf);
				sem_post(commonMutex);
			}
  		}
  		else if(plate == 2)
  		{
  			sem_cookstu->plateC += 1;
  			sem_cookstu->tempC += 1;
  			value = sem_cookstu->plateP + sem_cookstu->plateC + sem_cookstu->plateD;
  			
  			{
				sem_wait(commonMutex);
				char buf[SIZE];
				sprintf(buf, "Cook %d placed main course on the counter - counter items P:%d,C:%d,D:%d=%d\n", number, sem_cookstu->plateP, sem_cookstu->plateC, sem_cookstu->plateD, value);
				printMessage(buf);
				sem_post(commonMutex);
			}
  		}
  		else
  		{
  			sem_cookstu->plateD += 1;
  			sem_cookstu->tempD += 1;
  			value = sem_cookstu->plateP + sem_cookstu->plateC + sem_cookstu->plateD;
  			
  			{
				sem_wait(commonMutex);
				char buf[SIZE];
				sprintf(buf, "Cook %d placed desert on the counter - counter items P:%d,C:%d,D:%d=%d\n", number, sem_cookstu->plateP, sem_cookstu->plateC, sem_cookstu->plateD, value);
				printMessage(buf);
				sem_post(commonMutex);
			}
  		}

  		/*
  			If 3 types of plates are supplied to counter, meal semaphore is increased by 1.
  			Thus, the student can consume the meal.
  			The temp variables are reduced by 1 so that the separated meals are not taken into account again.
  		*/
  		if(sem_cookstu->tempP > 0 && sem_cookstu->tempC > 0 && sem_cookstu->tempD > 0)
  		{
  			sem_cookstu->tempP -= 1;
  			sem_cookstu->tempC -= 1;
  			sem_cookstu->tempD -= 1;

  			if(sem_post(&sem_cookstu->meal) == -1)
			{
				sem_wait(commonMutex);
				char buf[SIZE];
				sprintf(buf, "\nerrno = %d: %s sem_post\n\n", errno, strerror(errno));
				ExitFailure(buf);
			}
  		}
  		
  		// The number of items on the counter is increased by 1.
  		sem_cookstu->total += 1;

  		if(sem_post(&sem_cookstu->mutex) == -1)
  		{
  			sem_wait(commonMutex);
  			char buf[SIZE];
			sprintf(buf, "\nerrno = %d: %s sem_post\n\n", errno, strerror(errno));
			ExitFailure(buf);
  		}
  		// The item can now be consumed because it is placed in the counter.
  		if(sem_post(&sem_cookstu->full) == -1)
  		{
  			sem_wait(commonMutex);
  			char buf[SIZE];
			sprintf(buf, "\nerrno = %d: %s sem_post\n\n", errno, strerror(errno));
			ExitFailure(buf);
  		}
  	}

  	// The cook has finished its job.
  	if(sem_wait(&semaphores->mutex) == -1)
	{
		sem_wait(commonMutex);
		char buf[SIZE];
		sprintf(buf, "\nerrno = %d: %s sem_wait\n\n", errno, strerror(errno));
		ExitFailure(buf);
	}

	value = semaphores->plateP + semaphores->plateC + semaphores->plateD;

	{
		sem_wait(commonMutex);
		char buf[SIZE];
		sprintf(buf, "Cook %d finished serving - items at kitchen: %d - going home - GOODBYE!!!\n", number, value);
		printMessage(buf);
		sem_post(commonMutex);
	}

	if(sem_post(&semaphores->mutex) == -1)
	{
		sem_wait(commonMutex);
		char buf[SIZE];
		sprintf(buf, "\nerrno = %d: %s sem_post\n\n", errno, strerror(errno));
		ExitFailure(buf);
	}

	exit(EXIT_SUCCESS);
}

// Supplier
void supplier()
{
	int value = 0, totalPlates, bytesread = 0;
	char plate;

  	totalPlates = 3 * varL * varM;

  	// It checks whether it has reached the maximum number of plates.
  	while(semaphores->total < totalPlates)
  	{
  		// Since the supplier is guaranteed to bring plate to the kitchen, 1 place is reserved.
  		if(sem_wait(&semaphores->empty) == -1)
  		{
  			sem_wait(commonMutex);
  			char buf[SIZE];
			sprintf(buf, "\nerrno = %d: %s sem_wait\n\n", errno, strerror(errno));
			ExitFailure(buf);
  		}
  		// At this stage, since the number of items in the kitchen will be printed, so this region is used as the critical area.
  		if(sem_wait(&semaphores->mutex) == -1)
  		{
  			sem_wait(commonMutex);
  			char buf[SIZE];
			sprintf(buf, "\nerrno = %d: %s sem_wait\n\n", errno, strerror(errno));
			ExitFailure(buf);
  		}
  		
  		// Read 1 byte from file.
  		while(((bytesread = read(fdin, &plate, 1)) == -1) && (errno == EINTR));
		// If there are not enough bytes in the file, it suppresses the error message and terminates the program.
		if(bytesread <= 0)
		{
			{
				sem_wait(commonMutex);
				char buf[SIZE];
				sprintf(buf, "\nThere are error when read byte from filePath\n");
				ExitFailure(buf);
			}
			
			exit(EXIT_FAILURE);
		}
  		
  		// Total items in kitchen.
  		value = semaphores->plateP + semaphores->plateC + semaphores->plateD;
  		// Depending on the type of plate, necessary printing is done.
  		if(plate == 'P' || plate == 'p')
		{
			sem_wait(commonMutex);
			char buf[SIZE];
			sprintf(buf, "The supplier is going to the kitchen to deliver soup: kitchen items P:%d,C:%d,D:%d=%d\n", semaphores->plateP, semaphores->plateC, semaphores->plateD, value);
			printMessage(buf);
			sem_post(commonMutex);
		}
  		else if(plate == 'C' || plate == 'c')
		{
			sem_wait(commonMutex);
			char buf[SIZE];
			sprintf(buf, "The supplier is going to the kitchen to deliver main course: kitchen items P:%d,C:%d,D:%d=%d\n", semaphores->plateP, semaphores->plateC, semaphores->plateD, value);
			printMessage(buf);
			sem_post(commonMutex);
		}
  		else if(plate == 'D' || plate == 'd')
  		{
			sem_wait(commonMutex);
			char buf[SIZE];
			sprintf(buf, "The supplier is going to the kitchen to deliver desert: kitchen items P:%d,C:%d,D:%d=%d\n", semaphores->plateP, semaphores->plateC, semaphores->plateD, value);
			printMessage(buf);
			sem_post(commonMutex);
		}

		// This stage is over and exits from the critical region.
  		if(sem_post(&semaphores->mutex) == -1)
  		{
  			sem_wait(commonMutex);
  			char buf[SIZE];
			sprintf(buf, "\nerrno = %d: %s sem_post\n\n", errno, strerror(errno));
			ExitFailure(buf);
  		}
  		
  		// At this stage, since the new item will be added to the kitchen and the final situation will be printed, this section is used as a critical region.
  		if(sem_wait(&semaphores->mutex) == -1)
  		{
  			sem_wait(commonMutex);
  			char buf[SIZE];
			sprintf(buf, "\nerrno = %d: %s sem_wait\n\n", errno, strerror(errno));
			ExitFailure(buf);
  		}
  		
  		// Depending on the type of plate, necessary printing and increasing are done.
  		if(plate == 'P')
  		{
  			semaphores->plateP += 1;
  			value = semaphores->plateP + semaphores->plateC + semaphores->plateD;

  			{
				sem_wait(commonMutex);
				char buf[SIZE];
				sprintf(buf, "The supplier delivered soup - after delivery: kitchen items P:%d,C:%d,D:%d=%d\n", semaphores->plateP, semaphores->plateC, semaphores->plateD, value);
				printMessage(buf);
				sem_post(commonMutex);
			}

			// Increase plate P semaphores 
  			if(sem_post(&semaphores->semPP) == -1)
	  		{
	  			sem_wait(commonMutex);
	  			char buf[SIZE];
				sprintf(buf, "\nerrno = %d: %s sem_post\n\n", errno, strerror(errno));
				ExitFailure(buf);
	  		}
  		}
  		else if(plate == 'C')
  		{
  			semaphores->plateC += 1;
  			value = semaphores->plateP + semaphores->plateC + semaphores->plateD;

  			{
				sem_wait(commonMutex);
				char buf[SIZE];
				sprintf(buf, "The supplier delivered main course - after delivery: kitchen items P:%d,C:%d,D:%d=%d\n", semaphores->plateP, semaphores->plateC, semaphores->plateD, value);
				printMessage(buf);
				sem_post(commonMutex);
			}

			// Increase plate C semaphores 
  			if(sem_post(&semaphores->semPC) == -1)
	  		{
	  			sem_wait(commonMutex);
	  			char buf[SIZE];
				sprintf(buf, "\nerrno = %d: %s sem_post\n\n", errno, strerror(errno));
				ExitFailure(buf);
	  		}
  		}
  		else if(plate == 'D')
  		{
  			semaphores->plateD += 1;
  			value = semaphores->plateP + semaphores->plateC + semaphores->plateD;
  			
  			{
				sem_wait(commonMutex);
				char buf[SIZE];
				sprintf(buf, "The supplier delivered desert - after delivery: kitchen items P:%d,C:%d,D:%d=%d\n", semaphores->plateP, semaphores->plateC, semaphores->plateD, value);
				printMessage(buf);
				sem_post(commonMutex);
			}

			// Increase plate D semaphores 
  			if(sem_post(&semaphores->semPD) == -1)
	  		{
	  			sem_wait(commonMutex);
	  			char buf[SIZE];
				sprintf(buf, "\nerrno = %d: %s sem_post\n\n", errno, strerror(errno));
				ExitFailure(buf);
	  		}
  		}
  		
  		// Increase total amount supplied items.
  		semaphores->total += 1;

  		// This stage is over and exits from the critical region.
  		if(sem_post(&semaphores->mutex) == -1)
  		{
  			sem_wait(commonMutex);
  			char buf[SIZE];
			sprintf(buf, "\nerrno = %d: %s sem_post\n\n", errno, strerror(errno));
			ExitFailure(buf);
  		}
  		// The item can now be consumed because it is placed in the kitchen.
  		if(sem_post(&semaphores->full) == -1)
  		{
  			sem_wait(commonMutex);
  			char buf[SIZE];
			sprintf(buf, "\nerrno = %d: %s sem_post\n\n", errno, strerror(errno));
			ExitFailure(buf);
  		}
  	}

  	// The supplier has finished its job.
  	{
		sem_wait(commonMutex);
		char buf[SIZE];
		sprintf(buf, "The supplier finished supplying - GOODBYE!\n");
		printMessage(buf);
		sem_post(commonMutex);
	}
  	
  	exit(EXIT_SUCCESS);
}

// Share memory and initialize (between supplier and cooks)
void createSupCook()
{
	fd = shm_open(SupCook, O_CREAT | O_RDWR, 0666);
	if (fd < 0)
	{
		char buf[SIZE];
		sprintf(buf, "\nerrno = %d: %s shm_open\n\n", errno, strerror(errno));
		printMessage2(buf);
		exit(EXIT_FAILURE);
	}

	if(ftruncate(fd, sizeof(structSC)) == -1)
	{
		char buf[SIZE];
		sprintf(buf, "\nerrno = %d: %s ftruncate\n\n", errno, strerror(errno));
		printMessage2(buf);
		exit(EXIT_FAILURE);
  	}

	semaphores = (structSC *)mmap(NULL, sizeof(structSC), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if(semaphores == MAP_FAILED)
	{
		char buf[SIZE];
		sprintf(buf, "\nerrno = %d: %s mmap\n\n", errno, strerror(errno));
		printMessage2(buf);
		exit(EXIT_FAILURE);
	}
	if(sem_init(&semaphores->mutex, 1, 1) == -1)
	{
		char buf[SIZE];
		sprintf(buf, "\nerrno = %d: %s sem_init\n\n", errno, strerror(errno));
		printMessage2(buf);
		exit(EXIT_FAILURE);
	}
	if(sem_init(&semaphores->empty, 1, varK) == -1)
	{
		char buf[SIZE];
		sprintf(buf, "\nerrno = %d: %s sem_init\n\n", errno, strerror(errno));
		printMessage2(buf);
		exit(EXIT_FAILURE);
	}
	if(sem_init(&semaphores->full, 1, 0) == -1)
	{
		char buf[SIZE];
		sprintf(buf, "\nerrno = %d: %s sem_init\n\n", errno, strerror(errno));
		printMessage2(buf);
		exit(EXIT_FAILURE);
	}
	if(sem_init(&semaphores->semPP, 1, 0) == -1)
	{
		char buf[SIZE];
		sprintf(buf, "\nerrno = %d: %s sem_init\n\n", errno, strerror(errno));
		printMessage2(buf);
		exit(EXIT_FAILURE);
	}
	if(sem_init(&semaphores->semPC, 1, 0) == -1)
	{
		char buf[SIZE];
		sprintf(buf, "\nerrno = %d: %s sem_init\n\n", errno, strerror(errno));
		printMessage2(buf);
		exit(EXIT_FAILURE);
	}
	if(sem_init(&semaphores->semPD, 1, 0) == -1)
	{
		char buf[SIZE];
		sprintf(buf, "\nerrno = %d: %s sem_init\n\n", errno, strerror(errno));
		printMessage2(buf);
		exit(EXIT_FAILURE);
	}

	semaphores->plateP = 0;
	semaphores->plateC = 0;
	semaphores->plateD = 0;
	semaphores->total = 0;
	semaphores->totaltake = 0;
}

// Close share memory and unlink (between supplier and cooks)
void closeSupCook()
{
	if(munmap(semaphores, sizeof(structSC)) == -1)
  	{
  		char buf[SIZE];
		sprintf(buf, "\nerrno = %d: %s munmap\n\n", errno, strerror(errno));
		printMessage2(buf);
		exit(EXIT_FAILURE);
  	}

  	if(close(fd) == -1)
  	{
  		char buf[SIZE];
		sprintf(buf, "\nerrno = %d: %s close\n\n", errno, strerror(errno));
		printMessage2(buf);
		exit(EXIT_FAILURE);
  	}

  	if(shm_unlink(SupCook) == -1)
  	{
  		char buf[SIZE];
		sprintf(buf, "\nerrno = %d: %s shm_unlink\n\n", errno, strerror(errno));
		printMessage2(buf);
		exit(EXIT_FAILURE);
  	}
}

// Share memory and initialize (between cooks and students)
void createCookStu()
{	
	fd2 = shm_open(CookStud, O_CREAT | O_RDWR, 0666);
	if (fd2 < 0)
	{
		char buf[SIZE];
		sprintf(buf, "\nerrno = %d: %s shm_open\n\n", errno, strerror(errno));
		printMessage2(buf);
		exit(EXIT_FAILURE);
	}

	if(ftruncate(fd2, sizeof(structCSt)) == -1)
	{
	    char buf[SIZE];
		sprintf(buf, "\nerrno = %d: %s ftruncate\n\n", errno, strerror(errno));
		printMessage2(buf);
		exit(EXIT_FAILURE);
  	}

	sem_cookstu = (structCSt *)mmap(NULL, sizeof(structCSt), PROT_READ | PROT_WRITE, MAP_SHARED, fd2, 0);
	if(sem_cookstu == MAP_FAILED)
	{
		char buf[SIZE];
		sprintf(buf, "\nerrno = %d: %s mmap\n\n", errno, strerror(errno));
		printMessage2(buf);
		exit(EXIT_FAILURE);
	}
	if(sem_init(&sem_cookstu->mutex, 1, 1) == -1)
	{
		char buf[SIZE];
		sprintf(buf, "\nerrno = %d: %s sem_init\n\n", errno, strerror(errno));
		printMessage2(buf);
		exit(EXIT_FAILURE);
	}
	if(sem_init(&sem_cookstu->empty, 1, varS) == -1)
	{
		char buf[SIZE];
		sprintf(buf, "\nerrno = %d: %s sem_init\n\n", errno, strerror(errno));
		printMessage2(buf);
		exit(EXIT_FAILURE);
	}
	if(sem_init(&sem_cookstu->full, 1, 0) == -1)
	{
		char buf[SIZE];
		sprintf(buf, "\nerrno = %d: %s sem_init\n\n", errno, strerror(errno));
		printMessage2(buf);
		exit(EXIT_FAILURE);
	}
	if(sem_init(&sem_cookstu->meal, 1, 0) == -1)
	{
		char buf[SIZE];
		sprintf(buf, "\nerrno = %d: %s sem_init\n\n", errno, strerror(errno));
		printMessage2(buf);
		exit(EXIT_FAILURE);
	}

	sem_cookstu->plateP = 0;
	sem_cookstu->plateC = 0;
	sem_cookstu->plateD = 0;
	sem_cookstu->tempP = 0;
	sem_cookstu->tempC = 0;
	sem_cookstu->tempD = 0;
	sem_cookstu->total = 0;
	sem_cookstu->totaltake = 0;
	sem_cookstu->student = 0;
}

// Close share memory and unlink (between cooks and students)
void closeCookStu()
{
	if(munmap(sem_cookstu, sizeof(structCSt)) == -1)
  	{
  		char buf[SIZE];
		sprintf(buf, "\nerrno = %d: %s munmap\n\n", errno, strerror(errno));
		printMessage2(buf);
		exit(EXIT_FAILURE);
  	}

  	if(close(fd2) == -1)
  	{
  		char buf[SIZE];
		sprintf(buf, "\nerrno = %d: %s close\n\n", errno, strerror(errno));
		printMessage2(buf);
		exit(EXIT_FAILURE);
  	}

  	if(shm_unlink(CookStud) == -1)
  	{
  		char buf[SIZE];
		sprintf(buf, "\nerrno = %d: %s shm_unlink\n\n", errno, strerror(errno));
		printMessage2(buf);
		exit(EXIT_FAILURE);
  	}
}

// Share memory and initialize (between students)
void createStuTable()
{
	fd3 = shm_open(StudTable, O_CREAT | O_RDWR, 0666);
	if (fd3 < 0)
	{
		char buf[SIZE];
		sprintf(buf, "\nerrno = %d: %s shm_open\n\n", errno, strerror(errno));
		printMessage2(buf);
		exit(EXIT_FAILURE);
	}

	if(ftruncate(fd3, sizeof(structStTable)) == -1)
	{
	    char buf[SIZE];
		sprintf(buf, "\nerrno = %d: %s ftruncate\n\n", errno, strerror(errno));
		printMessage2(buf);
		exit(EXIT_FAILURE);
  	}

	sem_stutable = (structStTable *)mmap(NULL, sizeof(structStTable), PROT_READ | PROT_WRITE, MAP_SHARED, fd3, 0);
	if(sem_stutable == MAP_FAILED)
	{
		char buf[SIZE];
		sprintf(buf, "\nerrno = %d: %s mmap\n\n", errno, strerror(errno));
		printMessage2(buf);
		exit(EXIT_FAILURE);
	}
	if(sem_init(&sem_stutable->mutex, 1, 1) == -1)
	{
		char buf[SIZE];
		sprintf(buf, "\nerrno = %d: %s sem_init\n\n", errno, strerror(errno));
		printMessage2(buf);
		exit(EXIT_FAILURE);
	}
	if(sem_init(&sem_stutable->empty, 1, 0) == -1)
	{
		char buf[SIZE];
		sprintf(buf, "\nerrno = %d: %s sem_init\n\n", errno, strerror(errno));
		printMessage2(buf);
		exit(EXIT_FAILURE);
	}
	if(sem_init(&sem_stutable->full, 1, varT) == -1)
	{
		char buf[SIZE];
		sprintf(buf, "\nerrno = %d: %s sem_init\n\n", errno, strerror(errno));
		printMessage2(buf);
		exit(EXIT_FAILURE);
	}
	if(sem_init(&sem_stutable->UGmutex, 1, 1) == -1)
	{
		char buf[SIZE];
		sprintf(buf, "\nerrno = %d: %s sem_init\n\n", errno, strerror(errno));
		printMessage2(buf);
		exit(EXIT_FAILURE);
	}

	sem_stutable->countG = 0;
	sem_stutable->numberG = 0;
	sem_stutable->numberU = 0;
	sem_stutable->emptyTables = varT;
}

// close hare memory and unlink (between students)
void closeStuTable()
{
	if(munmap(sem_stutable, sizeof(structStTable)) == -1)
  	{
  		char buf[SIZE];
		sprintf(buf, "\nerrno = %d: %s munmap\n\n", errno, strerror(errno));
		printMessage2(buf);
		exit(EXIT_FAILURE);
  	}

  	if(close(fd3) == -1)
  	{
  		char buf[SIZE];
		sprintf(buf, "\nerrno = %d: %s close\n\n", errno, strerror(errno));
		printMessage2(buf);
		exit(EXIT_FAILURE);
  	}

  	if(shm_unlink(StudTable) == -1)
  	{
  		char buf[SIZE];
		sprintf(buf, "\nerrno = %d: %s shm_unlink\n\n", errno, strerror(errno));
		printMessage2(buf);
		exit(EXIT_FAILURE);
  	}
}

// Share array for table and initialize (for students)
void createTableArray()
{
	int i;
	fd4 = shm_open(StudTables, O_CREAT | O_RDWR, 0666);
	if (fd4 < 0)
	{
		char buf[SIZE];
		sprintf(buf, "\nerrno = %d: %s shm_open\n\n", errno, strerror(errno));
		printMessage2(buf);
		exit(EXIT_FAILURE);
	}

	if(ftruncate(fd4, varT * sizeof(int)) == -1)
	{
	    char buf[SIZE];
		sprintf(buf, "\nerrno = %d: %s ftruncate\n\n", errno, strerror(errno));
		printMessage2(buf);
		exit(EXIT_FAILURE);
  	}

	tableArray = (int *)mmap(NULL, varT * sizeof(int), PROT_READ | PROT_WRITE, MAP_SHARED, fd4, 0);

	for(i = 0; i < varT; i++)
	{
		tableArray[i] = 0;
	}
}

// Close array for table and unlink (for students)
void closeTableArray()
{
	if(munmap(tableArray, varT * sizeof(int)) == -1)
  	{
  		char buf[SIZE];
		sprintf(buf, "\nerrno = %d: %s munmap\n\n", errno, strerror(errno));
		printMessage2(buf);
		exit(EXIT_FAILURE);
  	}

  	if(close(fd4) == -1)
  	{
  		char buf[SIZE];
		sprintf(buf, "\nerrno = %d: %s close\n\n", errno, strerror(errno));
		printMessage2(buf);
		exit(EXIT_FAILURE);
  	}

  	if(shm_unlink(StudTables) == -1)
  	{
  		char buf[SIZE];
		sprintf(buf, "\nerrno = %d: %s shm_unlink\n\n", errno, strerror(errno));
		printMessage2(buf);
		exit(EXIT_FAILURE);
  	}
}

// Share mutex to print (for all processes)
void createCommon()
{
	fd5 = shm_open(common, O_CREAT | O_RDWR, 0666);
	if (fd5 < 0)
	{
		char buf[SIZE];
		sprintf(buf, "\nerrno = %d: %s shm_open\n\n", errno, strerror(errno));
		printMessage2(buf);
		exit(EXIT_FAILURE);
	}

	if(ftruncate(fd5, sizeof(sem_t)) == -1)
	{
		char buf[SIZE];
		sprintf(buf, "\nerrno = %d: %s ftruncate\n\n", errno, strerror(errno));
		printMessage2(buf);
		exit(EXIT_FAILURE);
  	}

	commonMutex = (sem_t *)mmap(NULL, sizeof(sem_t), PROT_READ | PROT_WRITE, MAP_SHARED, fd5, 0);
	if(commonMutex == MAP_FAILED)
	{
		char buf[SIZE];
		sprintf(buf, "\nerrno = %d: %s mmap\n\n", errno, strerror(errno));
		printMessage2(buf);
		exit(EXIT_FAILURE);
	}
	if(sem_init(commonMutex, 1, 1) == -1)
	{
		char buf[SIZE];
		sprintf(buf, "\nerrno = %d: %s sem_init\n\n", errno, strerror(errno));
		printMessage2(buf);
		exit(EXIT_FAILURE);
	}
}

// close mutex and ublink (for all processes)
void closeCommon()
{
	if(munmap(commonMutex, sizeof(sem_t)) == -1)
  	{
  		char buf[SIZE];
		sprintf(buf, "\nerrno = %d: %s munmap\n\n", errno, strerror(errno));
		printMessage2(buf);
		exit(EXIT_FAILURE);
  	}

  	if(close(fd5) == -1)
  	{
  		char buf[SIZE];
		sprintf(buf, "\nerrno = %d: %s close\n\n", errno, strerror(errno));
		printMessage2(buf);
		exit(EXIT_FAILURE);
  	}

  	if(shm_unlink(common) == -1)
  	{
  		char buf[SIZE];
		sprintf(buf, "\nerrno = %d: %s shm_unlink\n\n", errno, strerror(errno));
		printMessage2(buf);
		exit(EXIT_FAILURE);
  	}
}

// SSIGCHLD Handler
void sigchldHandler(int sig)
{
	// Keep errno
	int savedErrno = errno;

    // P1 catch the SIGCHLD signal and perform a synchronous wait for each of its children.
    // NOTE: The section for handling the SIGCHLD signal is taken from page 557-558 of the textbook.

	int status;
	pid_t cpid;

	while ((cpid = waitpid(-1, &status, WNOHANG)) > 0)
		numLiveChild--;

	if (cpid == -1 && errno != ECHILD)
	{
		char buf[SIZE];
		sprintf(buf, "\nerrno = %d: %s waitpid\n\n", errno, strerror(errno));
		ExitFailure(buf);
	}
    
    // Restore errno
    errno = savedErrno;
}

// When an error occurs, it prints the error and kills all processes.
void ExitFailure(char* mes)
{
	while(((write(STDERR_FILENO, mes, strlen(mes))) == -1) && (errno == EINTR));

	char text[] = "\n--- The program was terminated because there was an error. ---\n";
	while(((write(STDERR_FILENO, text, strlen(text))) == -1) && (errno == EINTR));
	
	killpg(0, SIGINT);
}

// Write output message
void printMessage(char* mes)
{
	while(((write(STDOUT_FILENO, mes, strlen(mes))) == -1) && (errno == EINTR));
	
}

// Write error message
void printMessage2(char* mes)
{
	while(((write(STDERR_FILENO, mes, strlen(mes))) == -1) && (errno == EINTR));	
}
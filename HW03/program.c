#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <signal.h>
#include "defs_and_types.h"

void convert(int** matrix, char* input, int number);
void writer(int fd, char* matrixA, char* matrixB);
void multiply(int* a, int* b, int x, int* res);
void writer2(int fd, char* matrix);
void reader(int fd, int number2, int quart, int** matrix);
void catcher(int sig);
void print(int** matrix, int number);
void cexit();

// Keep child process id in array.
pid_t cpids[4];

// Number of children started but not yet waited on.
static volatile int numLiveChild = 0;

int main(int argc, char* argv[])
{
	char* inputPathA;
	char* inputPathB;
	int number, number2, size, i;
	int opt;

	// Check amount
	if(argc != 7)
	{
		printf("This usage is wrong. \n");
		printf("Usage: ./program -i inputPathA -j inputPathB -n NUMBER\n");
		exit(1);
	}

	// I used the getopt() library method for parsing commandline arguments.
	while ((opt = getopt (argc, argv, "ijn:")) != -1)
  	{
	    switch (opt)
	      {
	      case 'i':
	        inputPathA = argv[optind];
	        break;
	      case 'j':
	        inputPathB = argv[optind];
	        break;
	      case 'n':
	        number = atoi(optarg);
	        break;
	      default:
	      	// If the command line arguments are missing/invalid your program must print usage information and exit.
	        printf("This usage is wrong. \n");
			printf("Usage: ./program -i inputPathA -j inputPathB -n NUMBER\n");
			exit(1);
	      }
	}

	// Check if the entered number is positive
	if(number < 1)
	{
		printf("number must be positive integer number.\n");
		return 1;
	}

	// There will be 4 child process.
	numLiveChild = 4;
	sigset_t blockMask, emptyMask;
	struct sigaction sa;

	/* 
		For handle SIGCHLD, Change SIGCHDL action.
		Because default action of SIGCHLD is immediately termination
		But it must be wait all child terminate.
	*/
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	sa.sa_handler = catcher;
	if (sigaction(SIGCHLD, &sa, NULL) == -1)
	{
		fprintf (stderr, "\nerrno = %d: %s sigaction\n\n", errno, strerror (errno));
		exit(1);
	}

	/*
		Block SIGCHLD to prevent its delivery if a child terminates
		before the parent commences the sigsuspend() loop below
	*/

	sigemptyset(&blockMask);
	sigaddset(&blockMask, SIGCHLD);
	if (sigprocmask(SIG_SETMASK, &blockMask, NULL) == -1)
	{
		fprintf (stderr, "\nerrno = %d: %s sigprocmask\n\n", errno, strerror (errno));
		exit(1);
	}

	// Calculate row and column
	number2 = pow(2, number);
	// Calculate total byte size
	size = (int)(number2 * number2);

	/*
		I used pipes to establish communication between parent and a child process.
		I used 1 pipe for communication from a parent to a child,
		and another pipe for communication from a child to the parent.
		So I made a bidirectional pipe.
		Since there will be 4 children in total, I will have made 4 pipes for both directions.
	*/
	int fd1[4][2];
	int fd2[4][2];
	for(i = 0; i < 4; i++)
	{
		if(pipe(fd1[i]) == -1)
		{
			fprintf (stderr, "\nerrno = %d: %s pipe\n\n", errno, strerror (errno));
			exit(1);
		}
		if(pipe(fd2[i]) == -1)
		{
			fprintf (stderr, "\nerrno = %d: %s pipe\n\n", errno, strerror (errno));
			exit(1);
		}
	}

	// Fork child process
	int process;
	for(process = 0; process < 4; process++)
	{
		// Keep child process id in array
		switch(cpids[process] = fork())
		{
			case -1:
				fprintf (stderr, "\nerrno = %d: %s fork\n\n", errno, strerror (errno));
				exit(1);
			// If fork return value is 0, then we know it is child.
			// All child process will do same operations, just their input context different.
			case 0:
				/*
					Child will read from fd1[0]
					Child will write ito fd2[1]
				*/
				if(process < 4)
				{
					//printf("\nChild Process - %d is start.\n", process + 2);
					// Close unused ends for child process.
					if(close(fd2[process][0]) == -1)
					{
						fprintf (stderr, "\nerrno = %d: %s (close unused ends)\n\n", errno, strerror (errno));
						exit(1);
					}
					if(close(fd1[process][1]) == -1)
					{
						fprintf (stderr, "\nerrno = %d: %s (close unused ends)\n\n", errno, strerror (errno));
						exit(1);
					}

					int A[size/2];
					int B[size/2];
					int result[size/4];
					int count = 0;
					char ch;
					char temp[1024] = "";
					int x;
					char* resMatrix;
					int bytesRead = 1;
					/* 
						Child receives a message from parent.
						It reads bytes and convert into 2 integer matrix.
						Matrix A takes one half of the first matrix,
						B takes one half of the second matrix,
						and 2 integer (2D array) matrixes are created.
					*/ 
					while(bytesRead && count < size)
					{
						bytesRead = read(fd1[process][0], &ch, 1);
						if(ch != ',')
							strncat(temp, &ch, 1);
						else
						{
							if(count < size/2)
								A[count] = atoi(temp);
							else
								B[count - size/2] = atoi(temp);
							count++;
							strcpy(temp,"");
						}
					}

					// Multiply these 2 matrixs.
					multiply(A,B,number2,result);

					/*
						To write a pipe,
						convert the integer matrix resulting from the product into a string of characters.
					*/
					x = 1024;
					strcpy(temp,"");
					resMatrix = (char *)malloc(x);
					strcpy(resMatrix,"");
					for(i = 0; i < number2*number2/4; i++)
					{
						sprintf(temp, "%d,", result[i]);
						if(strlen(resMatrix) + strlen(temp) >= x)
						{
							x *= 2;
							resMatrix = (char *) realloc(resMatrix, x);
						}
						strcat(resMatrix, temp);
					}

					// Child sends a response to parent with using writer2()
					writer2(fd2[process][1], resMatrix);

					// free memory
					free(resMatrix);
					//printf("\nChild Process - %d is finish.\n", process + 2);
					_exit(EXIT_SUCCESS);
				}

			default:
				// Close unused ends for parent process.
				if(close(fd2[process][1]) == -1)
				{
					cexit();
					fprintf (stderr, "\nerrno = %d: %s (close unused ends)\n\n", errno, strerror (errno));
					exit(1);
				}
				if(close(fd1[process][0]) == -1)
				{
					cexit();
					fprintf (stderr, "\nerrno = %d: %s (close unused ends)\n\n", errno, strerror (errno));
					exit(1);
				}
				break;
		}
	}

	int bytesread = 0;
	char* inputA;
	char* inputB;
	int** matrixA;
	int** matrixB;
	int** matrixResult;
	char* matrixA1;
	char* matrixA2;
	char* matrixB1;
	char* matrixB2;
	int x;

	// Open input file
	int fdinA = open(inputPathA, O_RDONLY);
	if(fdinA == -1)
	{
		cexit();
		fprintf (stderr, "\nerrno = %d: %s open inputPathA\n\n", errno, strerror (errno));
		exit(1);
	}

	// Open input file
	int fdinB = open(inputPathB, O_RDONLY);
	if(fdinB == -1)
	{
		cexit();
		fprintf (stderr, "\nerrno = %d: %s open inputPathB\n\n", errno, strerror (errno));
		exit(1);
	}

	// For read bytes from input files, It used dynamic arrays.
	inputA = (char*) malloc(size);
	inputB = (char*) malloc(size);

	// Read (2^n)x(2^n) characters from inputPathA.
	while(((bytesread = read(fdinA, inputA, size)) == -1) && (errno == EINTR));
	// If there are not sufficient characters in the files then it will print an error message and exit gracefully.
	if(bytesread != size)
	{
		cexit();
		fprintf (stderr, "\nerrno = %d: %s there are not sufficient characters in the inputPathA file\n\n", errno, strerror (errno));
		exit(1);
	}

	// These characters will be converted into its ASCII code integer equivalent in 2D array.
	matrixA = (int **)calloc(number2, sizeof(int *));
	for(i = 0; i < number2; i++)
	{ 
		matrixA[i] = (int *)calloc(number2, sizeof(int));
	}
	// Convert operation
	convert(matrixA, inputA, number2);
	free(inputA);

	bytesread = 0;
	// Read (2^n)x(2^n) characters from inputPathB.
	while(((bytesread = read(fdinB, inputB, size)) == -1) && (errno == EINTR));
	// If there are not sufficient characters in the files then it will print an error message and exit gracefully.
	if(bytesread != size)
	{
		cexit();
		fprintf (stderr, "\nerrno = %d: %s there are not sufficient characters in the inputPathB file\n\n", errno, strerror (errno));
		exit(1);
	}

	// These characters will be converted into its ASCII code integer equivalent in 2D array.
	matrixB = (int **)calloc(number2, sizeof(int *));
	for(i = 0; i < number2; i++)
	{ 
		matrixB[i] = (int *)calloc(number2, sizeof(int));
	}
	// Convert operation
	convert(matrixB, inputB, number2);
	free(inputB);

	char buf[1024] = "";
	int j;

	/*
		The parts needed for the calculation of the result quarters will be sent.
		So, I divided the matrices into 2 part
	*/
	x = 1024;
	// This section includes the A11 and A12 quarters.
	matrixA1 = (char *)malloc(x);
	strcpy(matrixA1,"");
	for(i = 0; i < number2/2; i++)
	{
		for(j = 0; j < number2; j++)
		{
			sprintf(buf, "%d,", matrixA[i][j]);
			if(strlen(matrixA1) + strlen(buf) >= x)
			{
				x *= 2;
				matrixA1 = (char *) realloc(matrixA1, x);
			}
			strcat(matrixA1, buf);
		}
	}

	// This section includes the A21 and A22 quarters.
	x = 1024;
	matrixA2 = (char *)malloc(x);
	strcpy(matrixA2,"");
	for(i = number2/2; i < number2; i++)
	{
		for(j = 0; j < number2; j++)
		{
			sprintf(buf, "%d,", matrixA[i][j]);
			if(strlen(matrixA2) + strlen(buf) >= x)
			{
				x *= 2;
				matrixA2 = (char *) realloc(matrixA2, x);
			}
			strcat(matrixA2, buf);
		}
	}

	// This section includes the B11 and B21 quarters.
	x = 1024;
	matrixB1 = (char *)malloc(x);
	strcpy(matrixB1,"");
	for(i = 0; i < number2; i++)
	{
		for(j = 0; j < number2/2; j++)
		{
			sprintf(buf, "%d,", matrixB[i][j]);
			if(strlen(matrixB1) + strlen(buf) >= x)
			{
				x *= 2;
				matrixB1 = (char *) realloc(matrixB1, x);
			}
			strcat(matrixB1, buf);
		}
	}

	// This section includes the B12 and B22 quarters.
	x = 1024;
	matrixB2 = (char *)malloc(x);
	strcpy(matrixB2,"");
	for(i = 0; i < number2; i++)
	{
		for(j = number2/2; j < number2; j++)
		{
			sprintf(buf, "%d,", matrixB[i][j]);
			if(strlen(matrixB2) + strlen(buf) >= x)
			{
				x *= 2;
				matrixB2 = (char *) realloc(matrixB2, x);
			}
			strcat(matrixB2, buf);
		}
	}

	// It will hold the product of 2 matrices.
	matrixResult = (int **)calloc(number2, sizeof(int *));
	for(i = 0; i < number2; i++)
	{ 
		matrixResult[i] = (int *)calloc(number2, sizeof(int));
	}

	// A11, A12, B11 and B21 quarters will be sent to Process 2 as input.
	writer(fd1[0][1], matrixA1, matrixB1);
	/*
		The C11 quarter of the resulting matrix calculated in Process 2 is read from Process 2
		and written to the C11 quarter of matrixResult.
	*/
	reader(fd2[0][0], number2, 2, matrixResult);

	// A11, A12, B12 and B22 quarters will be sent to Process 3 as input.
	writer(fd1[1][1], matrixA1, matrixB2);
	/*
		The C12 quarter of the resulting matrix calculated in Process 3 is read from Process 3
		and written to the C12 quarter of matrixResult.
	*/
	reader(fd2[1][0], number2, 3, matrixResult);

	// A21, A22, B11 and B21 quarters will be sent to Process 4 as input.
	writer(fd1[2][1], matrixA2, matrixB1);
	/*
		The C21 quarter of the resulting matrix calculated in Process 4 is read from Process 4
		and written to the C21 quarter of matrixResult.
	*/
	reader(fd2[2][0], number2, 4, matrixResult);

	// A21, A22, B12 and B22 quarters will be sent to Process 5 as input.
	writer(fd1[3][1], matrixA2, matrixB2);
	/*
		The C22 quarter of the resulting matrix calculated in Process 5 is read from Process 5
		and written to the C22 quarter of matrixResult.
	*/
	reader(fd2[3][0], number2, 5, matrixResult);

	// free memorys
	free(matrixA1);
	free(matrixB1);
	free(matrixA2);
	free(matrixB2);

	/*
		After completing the result matrix, singular values are calculated.
		The matrix has singular values as much as the number of rows or columns.
		So I created this amount of dynamic float arrays.
	*/
	float* w = (float *)malloc(number2 * sizeof(float *));
	dsvd(matrixResult, number2, w);

	printf("\n-------------------- Matrix - A --------------------\n");
	print(matrixA, number2);

	printf("\n-------------------- Matrix - B --------------------\n");
	print(matrixB, number2);

	printf("\n-------------------- Multiplication --------------------\n");
	print(matrixResult, number2);
	
	printf("\n---------- All Singular Values ----------\n\n");
	for(i = 0; i < number2; i++)
		printf("Singular Value - %d :\t %.3lf \n", i, w[i]);

	printf("\n");

	// free dynamic array of singular values.
	free(w);

	// free 2D matrixA array.
	for(i = 0; i < number2; i++)
    	free(matrixA[i]);
	free(matrixA);

	// free 2D matrixB array.
	for(i = 0; i < number2; i++)
    	free(matrixB[i]);
	free(matrixB);

	// free 2D matrixResult array.
	for(i = 0; i < number2; i++)
    	free(matrixResult[i]);
	free(matrixResult);

	// Close input files
	if(close(fdinA) == -1)
	{
		cexit();
		fprintf (stderr, "\nerrno = %d: %s (close inputPathA)\n\n", errno, strerror (errno));
		exit(1);
	}

	if(close(fdinB) == -1)
	{
		cexit();
		fprintf (stderr, "\nerrno = %d: %s (close inputPathB)\n\n", errno, strerror (errno));
		exit(1);
	}

	// Parent comes here: wait for SIGCHLD until all children are dead.
	sigemptyset(&emptyMask);
	while(numLiveChild > 0)
	{
		if(sigsuspend(&emptyMask) == -1 && errno != EINTR)
		{
			cexit();
			fprintf (stderr, "\nerrno = %d: %s sigsuspend\n\n", errno, strerror (errno));
			exit(1);
		}
	}

	printf("Parent Process (pid = %d) terminated\n\n",getpid());

	return 0;
}

// Read an quarter from child process and write this quarter relevant part.
void reader(int fd, int number2, int quart, int** matrix)
{
	char buf[1024] = "";
	int size = number2*number2/4;
	int A[size];
	int bytesRead = 1;
	int count = 0;
	char ch;
	while(bytesRead && count < size)
	{
		bytesRead = read(fd, &ch, 1);
		if(ch != ',')
			strncat(buf, &ch, 1);
		else
		{
			A[count] = atoi(buf);
			strcpy(buf,"");
			count++;
		}
	}

	int i, j, x = 0;

	if(quart == 2)
	{
		for(i = 0; i < number2/2; i++)
		{
			for(j = 0; j < number2/2; j++)
			{
				matrix[i][j] = A[x];
				x++;
			}
		}
	}
	else if(quart == 3)
	{
		for(i = 0; i < number2/2; i++)
		{
			for(j = number2/2; j < number2; j++)
			{
				matrix[i][j] = A[x];
				x++;
			}
		}
	}
	else if(quart == 4)
	{
		for(i = number2/2; i < number2; i++)
		{
			for(j = 0; j < number2/2; j++)
			{
				matrix[i][j] = A[x];
				x++;
			}
		}
	}
	else if(quart == 5)
	{
		for(i = number2/2; i < number2; i++)
		{
			for(j = number2/2; j < number2; j++)
			{
				matrix[i][j] = A[x];
				x++;
			}
		}
	}
	else
	{
		perror("wrong quart in reader");
		exit(1);
	}
}

// write character array of input matrix in relevant pipe.
void writer2(int fd, char* matrix)
{
	int bytes = strlen(matrix);
	int byteswritten = 0;
	char* bp = matrix;
	while(bytes > 0)
	{
		while(((byteswritten = write(fd, bp, bytes)) == -1 ) && (errno == EINTR));
		if (byteswritten < 0)
			break;
		bytes -= byteswritten;
		bp += byteswritten;
	}

}

// write character array of 2 matrixs in child process's input pipe.
void writer(int fd, char* matrixA, char* matrixB)
{
	int bytes = strlen(matrixA);
	int byteswritten = 0;
	char* bp = matrixA;
	while(bytes > 0)
	{
		while(((byteswritten = write(fd, bp, bytes)) == -1 ) && (errno == EINTR));
		if (byteswritten < 0)
			break;
		bytes -= byteswritten;
		bp += byteswritten;
	}

	bytes = strlen(matrixB);
	byteswritten = 0;
	bp = matrixB;

	while(bytes > 0)
	{
		while(((byteswritten = write(fd, bp, bytes)) == -1 ) && (errno == EINTR));
		if (byteswritten < 0)
			break;
		bytes -= byteswritten;
		bp += byteswritten;
	}
}

// Convert characters to its ASCII code integer equivalent.
void convert(int** matrix, char* input, int number)
{
	int i, j;
	int x = 0;

	for(i = 0; i < number; i++)
	{
		for(j = 0; j < number; j++)
		{
			matrix[i][j] = (int)input[x];
			x += 1;
		}
	}
}

// Multiply 2 matrixs.
void multiply(int* a, int* b, int x, int* res)
{
	int i, j, k;
	int sum;

	for (i = 0; i < x/2; i++)
	{
		for (j = 0; j < x/2; j++)
		{
			sum = 0;
			for (k = 0; k < x; k++)
				sum = sum + a[i * x + k] * b[k * x/2 + j];
			res[i * x/2 + j] = sum;
		}
	}
}

// print matrixs
void print(int** matrix, int number)
{
	int i, j;
	for(i = 0; i < number; i++)
	{
		printf("\n");
		for(j = 0; j < number; j++)
		{
			printf("%-7d ", matrix[i][j]);
		}
	}
	printf("\n");
}

// When parent process encounters error, Kill child processes
void cexit()
{
	int k;
	for(k = 0; k < 4; k++)
	{
		kill(cpids[k], SIGKILL);
	}
}

// Signal Handler
void catcher(int sig)
{
	// Keep errno
	int savedErrno = errno;

	// In case of CTRL-C, all 5 processes must exit gracefully.
	// Catch signal firstly send kill signal to child processes, then terminate.
    if(sig == SIGINT)
    {
    	int i;
    	for(i = 0; i < 4; i++)
    		kill(cpids[i], SIGKILL);
    	exit(EXIT_SUCCESS);
    }
    // P1 catch the SIGCHLD signal and perform a synchronous wait for each of its children.
    // NOTE: The section for handling the SIGCHLD signal is taken from page 557-558 of the textbook.
    else if(sig == SIGCHLD)
    {
    	int status;
		pid_t cpid;

		while ((cpid = waitpid(-1, &status, WNOHANG)) > 0)
		{
			printf("Child Process (pid = %d) terminated\n",cpid);
			numLiveChild--;
		}

		if (cpid == -1 && errno != ECHILD)
		{
			fprintf (stderr, "\nerrno = %d: %s waitpid\n\n", errno, strerror (errno));
			exit(1);
		}
    }

    // Restore errno
    errno = savedErrno;
}
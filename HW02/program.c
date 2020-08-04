#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <math.h>

#define SIZE 20
#define BLKSIZE 1024
#define NUMBER 10

void p2();
void p1();
void leastSquaresMethod(char buf[SIZE], char wbuf[BLKSIZE]);
void calculateMetrics(char line[], double TMAE[], double TMSE[], double TRMSE[], int index);
double calculateMean(double Metric[], int count);
double calculateMD(double Metric[], double mean, int count);
double calculateSD(double Metric[], double mean, int count);
void catcher(int sig);
void printSignal();

char* inputPath = NULL;
char* outputPath = NULL;
char template[] = "tempXXXXXX";
pid_t child_pid = -1;
int flag2 = 1, criticalFlag = 0;
double* MAE = NULL;
double* MSE = NULL;
double* RMSE = NULL;
char* rewrite = NULL;
int fdin, fdout, fd, fd2;

int main(int argc, char* argv[])
{
	int opt;

	if(argc != 5)
	{
		printf("This usage is wrong. \n");
		printf("Usage: ./program -i inputPath -o outputPath\n");
		exit(1);
	}

	// I used the getopt() library method for parsing commandline arguments.
	while ((opt = getopt (argc, argv, "io")) != -1)
  	{
	    switch (opt)
	      {
	      case 'i':
	        inputPath = argv[optind];
	        break;
	      case 'o':
	        outputPath = argv[optind];
	        break;
	      default:
	      	// If the command line arguments are missing/invalid your program must print usage information and exit.
	        printf("This usage is wrong. \n");
			printf("Usage: ./program -i inputPath -o outputPath\n");
			exit(1);
	      }
	}

	// Open input file
	fdin = open(inputPath, O_RDONLY);
	if(fdin == -1)
	{
		fprintf (stderr, "\nerrno = %d: %s (open inputPath)\n\n", errno, strerror (errno));
		exit(1);
	}

	// Open output file
	fdout = open(outputPath, O_WRONLY);
	if(fdout == -1)
	{
		fprintf (stderr, "\nerrno = %d: %s (open outputPath)\n\n", errno, strerror (errno));
		exit(1);
	}

	// Create temporary file via mkstemp
	fd = mkstemp(template);
	if(fd == -1)
	{
		fprintf (stderr, "\nerrno = %d: %s (mkstemp could not create file)\n\n", errno, strerror (errno));
		exit(1);
	}

	// Other cursor of temporary file for process 2.
	fd2 = open(template, O_RDWR);
	if(fd2 == -1)
	{
		fprintf (stderr, "\nerrno = %d: %s (open temp file)\n\n", errno, strerror (errno));
		exit(1);
	}

	// New handler for SIGUSR1, SIGUSR2, SIGTERM signals
	signal(SIGUSR1, catcher);
	signal(SIGUSR2, catcher);
    signal(SIGTERM, catcher);

    // Fork process
	child_pid = fork();

	// If fork was successfull
	if(child_pid >= 0)
	{
		int status;

		// If child_pid equal 0, this indicates that is child process, and call p2 function. 
		if(child_pid == 0)
			p2();
		// Otherwise, parent process call p1 function.
		else
		{
			/* In order to avoid a problem when sending a signal from transaction P1 to transaction p2,
				p1 waits for a signal from p2 indicating that the transaction has started.
			*/
			sigset_t new_mask;
			sigfillset(&new_mask);
			sigdelset(&new_mask, SIGUSR2);
			sigsuspend(&new_mask);
			// Thus p1 starts after p2 starts.
			// Thus, there is no problem sending signal to p1 p2.
			p1();
			// The parent waits for the child process to finish.
			waitpid(child_pid, &status, 0);
			if(WIFEXITED(status)) return 0;
		}

		return 0;
	}
	// If fork was not successfull, print error message via stderr
	else
	{
		fprintf (stderr, "\nerrno = %d: %s (Fork failed)\n\n", errno, strerror (errno));
        return 1;
	}

}

void p1()
{
	char *bp;
	char buf[SIZE] = "";
	char wbuf[BLKSIZE] = "";
	char temp[BLKSIZE] = "";
	int bytesread;
	int byteswritten = 0;
	int totalbytes = 0;
	int totalreadbytes = 0;
	int i = 0;
	int count = 0;

	struct flock lock;
	memset(&lock, 0, sizeof(lock));

	sigset_t prevMask, intMask;
    struct sigaction sa;

	for( ; ; )
	{

		strcpy(wbuf, "");
		strcpy(temp, "");
		strcpy(buf, "");
		bp = NULL;

		// P1 will read the contents of the file denoted by inputPath
		while(((bytesread = read(fdin, buf, SIZE)) == -1) && (errno == EINTR));
		if(bytesread <= 0)
			break;

		/*
			every couple of unsigned bytes it reads,
			will be interpreted as a 2D coordinate (x,y).
			For every 10 coordinates (i.e. every 20 bytes) it reads, 
		*/
		if(bytesread != 20) break;
		totalreadbytes += bytesread;
		for(i = 0; i < SIZE; i += 2)
		{
			sprintf(temp, "(%d, %d), ", (int)((unsigned char) buf[i]), (int)((unsigned char) buf[i+1]));
			strcat(wbuf,temp);
		}

		/*
			Critical Section
			It is not to be interrupted by SIGINT
			So it will block SIGINT signal.
		*/
        sigemptyset(&intMask);
        sigaddset(&intMask, SIGINT);
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        sa.sa_handler = catcher;

        if (sigaction(SIGINT, &sa, NULL) == -1)
        {
        	fprintf (stderr, "\nerrno = %d: %s (Failed to install SIGINT signal handler)\n\n", errno, strerror (errno));
            exit(1);
        }
        if (sigprocmask(SIG_BLOCK, &intMask, &prevMask) == -1)
        {
        	fprintf (stderr, "\nerrno = %d: %s (Failed to SIG_BLOCK)\n\n", errno, strerror (errno));
            exit(1);
        }

        /*
        	 it’ll apply the least squares method to the corresponding 2D coordinates
        	 and calculate the line equation (ax+b) that fits them. 
        */
		leastSquaresMethod(buf, wbuf);
		bytesread = strlen(wbuf);
		
		// Unblock the SIGINT signal
		if (sigprocmask(SIG_UNBLOCK, &intMask, NULL) == -1)
        {
        	fprintf (stderr, "\nerrno = %d: %s (Failed to SIG_UNBLOCK)\n\n", errno, strerror (errno));
            exit(1);
        }

        sa.sa_handler = SIG_DFL;
        if (sigaction(SIGINT, &sa, NULL) == -1)
        {
        	fprintf (stderr, "\nerrno = %d: %s (Failed to get old handler for SIGINT)\n\n", errno, strerror (errno));
            exit(1);
        }

        // Lock the temp file when p1 process was writting.
        // Avoid the conflicts for p1 and p2.
		lock.l_type = F_WRLCK;
		fcntl(fd, F_SETLKW, &lock);

		/*
			Then, P1 will write in a comma separated line the 10 coordinates
			followed by the line equation as a new line of a temporary file created via mkstemp.
		*/
		bp = wbuf;
		// Always write end of file.
		lseek(fd, 0, SEEK_END);
		while(bytesread > 0)
		{
			while(((byteswritten = write(fd, bp, bytesread)) == -1) && (errno == EINTR));
			if(byteswritten < 0)
				break;
			totalbytes += byteswritten;
			bytesread -= byteswritten;
			bp += byteswritten;
		}
		if(byteswritten == -1)
			break;

		// Unlock the temp file.
		lock.l_type = F_UNLCK;
		fcntl(fd, F_SETLKW, &lock);

		count += 1;
		/*
			Send SIGUSR2 signal child process.
			Indicates that there is new input for the child process.
			If the child process is in suspend state and is waiting for this signal,
			it will continue to work when this signal arrives.
		*/
		kill(child_pid, SIGUSR2);
	}

	/*
		Printing on screen how many bytes it has read as input,
		how many line equations it has estimated,
		and which signals where sent to P1 while it was in a critical section.
	*/
	printf("\n Number of bytes read: %d\n", totalreadbytes);
	printf(" Number of estimated line equations: %d\n", count);
	printSignal();

	/*
		After finishing processing the contents of the input file,
		P1 will terminate gracefully by closing open file
	*/
	if(close(fdin) == -1)
	{
		fprintf (stderr, "\nerrno = %d: %s (close inputPath)\n\n", errno, strerror (errno));
		exit(1);
	}

	if(close(fd) == -1)
	{
		fprintf (stderr, "\nerrno = %d: %s (close temporary file)\n\n", errno, strerror (errno));
		exit(1);
	}

	/*
		SIGUSR1 signal is sent to the child process to indicate that the Parent process is finished.
		Thus, the child process understands that no new input will come.
		In addition, when this signal is received,
		the child process understands that the parent process is done with the inputPath file and deletes it.
	*/
	kill(child_pid, SIGUSR1);
}

void p2()
{
	/*
		The child process sends a SIGUSR2 signal to let the parent process start
		so that the parent process exits the suspend state and starts running.
		The purpose of doing this is to prevent P1 from sending the signal before P2 starts.
		P2 forked early by P1.
	*/
	kill(getppid(), SIGUSR2);

	char *bp;
	char buf[BLKSIZE];
	char temp[BLKSIZE];
	int bytesread;
	int byteswritten = 0;
	int totalbytes = 0;
	int temptotalbytes = 0;
	int i = 0, j = 1, flag = 0;
	int count = 0;
	int arrSize = 20;
	MAE = (double*) malloc(arrSize * sizeof(double));
	MSE = (double*) malloc(arrSize * sizeof(double));
	RMSE = (double*) malloc(arrSize * sizeof(double));
	double MMAE = 0, MMSE = 0, MRMSE = 0, MDMAE = 0, MDMSE = 0, MDRMSE = 0, SDMAE = 0, SDMSE = 0, SDRMSE = 0; 

	struct stat fileStat;

	struct flock lock;
	memset(&lock, 0, sizeof(lock));

	sigset_t prevMask, intMask;
	sigset_t new_mask;
	struct sigaction sa;

	for ( ; ; )
	{
		/*
			If the SIGUSR1 signal is sent before process 1 enters the suspend state, it cannot handle.
			To prevent this situation, continuous signal is sent.
		*/
		kill(getppid(), SIGUSR2);

		strcpy(buf, "");
		strcpy(temp, " ");
		i = 0;
		j = 1;
		flag = 0;
		bp = NULL;

		/*
			Starting from the top, it looks for the first line available. 
			It does this by searching for the '\ n' character.
		*/
		lseek(fd2, 0, SEEK_SET);
		while(j == 1)
		{
			i += 1;
			j = read(fd2, temp, 1);
			if(temp[0] == '\n')
			{
				flag = 1;
				break;
			}
		}
		// P2 will read the contents of the temporary file created by P1, line by line
		// It always start read from top, because the line whose job is finished is deleted.
		lseek(fd2, 0, SEEK_SET);
		while(((bytesread = read(fd2, buf, i - 1)) == -1) && (errno == EINTR));

		// Deletes the new line character at the end of the string.
		if(flag == 1) buf[i-1] = '\0';
		
		/*
			If there is at least 1 line of input in the temporary file and the parent process is over,
			it means that there will be no new input.
			So it exits the loop.
			flag2 is global variable. If is equal 0, parent process done, Otherwise parent process is working.
		*/
		if((bytesread <= 0 || flag == 0) && flag2 == 0)
			break;
		
		/*
			New input can be arrive, So suspend SIGUSR1 or SIGUSR2 signal.
			If one of these signals arrived, then it will turn begin of loop.
			Then if this signal was SIGUSR1, it will break in upper condition
			Otherwise it will read in loop.
		*/
		if((bytesread <= 0 || flag == 0) && flag2 == 1)
		{
			sigfillset(&new_mask);
			sigdelset(&new_mask,SIGUSR1);
			sigdelset(&new_mask,SIGUSR2);
			sigsuspend(&new_mask);
			continue;
		}
		
		/*
			The values to be used in the calculation are kept in the array.
			If the array is too small for it, realloc is done and enlarged.
		*/
		if(count == arrSize)
		{
			arrSize *= 2;
			MAE = (double *) realloc(MAE, arrSize * sizeof(double));
			MSE = (double *) realloc(MSE, arrSize * sizeof(double));
			RMSE = (double *) realloc(RMSE, arrSize * sizeof(double));
		}

		/*
			Critical Section
			It is not to be interrupted by SIGINT
			So it will block SIGINT signal.
		*/
		sigemptyset(&intMask);
        sigaddset(&intMask, SIGINT);
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        sa.sa_handler = catcher;

        if (sigaction(SIGINT, &sa, NULL) == -1)
        {
        	fprintf (stderr, "\nerrno = %d: %s (Failed to install SIGINT signal handler)\n\n", errno, strerror (errno));
            exit(1);
        }
        if (sigprocmask(SIG_BLOCK, &intMask, &prevMask) == -1)
        {
        	fprintf (stderr, "\nerrno = %d: %s (Failed to SIG_BLOCK)\n\n", errno, strerror (errno));
            exit(1);
        }

        /*
        	  it will calculate the mean absolute error (MAE),mean squared error (MSE)
        	  and root mean squared error (RMSE) between the coordinates and the estimated line.
        */
		calculateMetrics(buf, MAE, MSE, RMSE, count);
		bytesread = strlen(buf);
		count += 1;
		
		if (sigprocmask(SIG_UNBLOCK, &intMask, NULL) == -1)
        {
        	fprintf (stderr, "\nerrno = %d: %s (Failed to SIG_UNBLOCK)\n\n", errno, strerror (errno));
            exit(1);
        }

        sa.sa_handler = SIG_DFL;
        // Unblock the SIGINT signal
        if (sigaction(SIGINT, &sa, NULL) == -1)
        {
        	fprintf (stderr, "\nerrno = %d: %s (Failed to get old handler for SIGINT)\n\n", errno, strerror (errno));
            exit(1);
        }
		
		// Lock the output file when p2 process was writting.
		lock.l_type = F_WRLCK;
		fcntl(fdout, F_SETLKW, &lock);

		/*
			write its own output to the file denoted by outputPath as 10 coordinates,
			the line equation (ax+b) and the three error estimates in a comma separated form
		*/
		bp = buf;
		// Always write end of file.
		lseek(fdout, 0, SEEK_END);
		while(bytesread > 0)
		{
			while(((byteswritten = write(fdout, bp, bytesread)) == -1) && (errno == EINTR));
			if(byteswritten < 0)
				break;
			totalbytes += byteswritten;
			bytesread -= byteswritten;
			bp += byteswritten;
		}
		if(byteswritten == -1)
			break;

		// Unblock the outputfile
		lock.l_type = F_UNLCK;
		fcntl(fdout, F_SETLKW, &lock);

		// Lock the temp file when p1 process was writting.
        // Avoid the conflicts for p1 and p2.
		lock.l_type = F_WRLCK;
		fcntl(fd2, F_SETLKW, &lock);

		// It will then remove the line it read from the file 
		// For this reason it get the size of temp file

		if(stat(template,&fileStat) < 0)
		{
			fprintf (stderr, "\nerrno = %d: %s (stat error. Check stat function)\n\n", errno, strerror (errno));
			exit(1);
		}

		// The bytes after the line to be deleted are copied. 
		rewrite = (char*) malloc(fileStat.st_size - i);
		lseek(fd2, i, SEEK_SET);
		while(((bytesread = read(fd2, rewrite, fileStat.st_size - i)) == -1) && (errno == EINTR));
		
		// The file is resized with the truncate () function. 
		truncate(template, fileStat.st_size - i);

		/*
			Then the bytes copied are written to the file from the beginning of the file.
			Such line will be deleted.
		*/
		if (bytesread <= 0)
			break;
		bp = rewrite;
		lseek(fd2, 0, SEEK_SET);
		while(bytesread > 0)
		{
			while(((byteswritten = write(fd2, rewrite, bytesread)) == -1) && (errno == EINTR));
			if(byteswritten < 0)
				break;
			temptotalbytes += byteswritten;
			bytesread -= byteswritten;
			bp += byteswritten;
		}
		strcpy(rewrite," ");
		free(rewrite);
		if(byteswritten == -1)
			break;

		// Unblock the temp file.
		lock.l_type = F_UNLCK;
		fcntl(fd2, F_SETLKW, &lock);
	}
	
	// If the input is not empty, calculations are made.
	if(count > 0)
	{
		MMAE = calculateMean(MAE, count);
		MMSE = calculateMean(MSE, count);
		MRMSE = calculateMean(RMSE, count);

		MDMAE = calculateMD(MAE, MMAE, count);
		MDMSE = calculateMD(MSE, MMSE, count);
		MDRMSE = calculateMD(RMSE, MRMSE, count);

		SDMAE = calculateSD(MAE, MMAE, count);
		SDMSE = calculateSD(MSE, MMSE, count);
		SDRMSE = calculateSD(RMSE, MRMSE, count);
	}

	//  Print on screen for each error metric, its mean, mean deviation and standard deviation.
	printf("\n MAE => Mean: %.3lf \t Mean Deviation: %.3lf \t Standard Deviation: %.3lf\n", MMAE, MDMAE, SDMAE);
	printf("\n MSE => Mean: %.3lf \t Mean Deviation: %.3lf \t Standard Deviation: %.3lf\n", MMSE, MDMSE, SDMSE);
	printf("\n RMSE => Mean: %.3lf \t Mean Deviation: %.3lf \t Standard Deviation: %.3lf\n", MRMSE, MDRMSE, SDRMSE);

	// Free memory
	free(MAE);
	free(MSE);
	free(RMSE);

	/*
		After finishing processing the contents of the input file,
		P2 will terminate gracefully by closing open file
	*/
	if(close(fdout) == -1)
	{
		fprintf (stderr, "\nerrno = %d: %s (close outputPath)\n\n", errno, strerror (errno));
		exit(1);
	}

	if(close(fd2) == -1)
	{
		fprintf (stderr, "\nerrno = %d: %s (close temporary file)\n\n", errno, strerror (errno));
		exit(1);
	}

	// Temporary file can be deleted since P2 process is also completed.
	unlink(template);

	/*
		For P2 to exit the loop, SIGUSR1 must come from P1.
		So in this case, SIGUSR1 came from P1.
		P1 is finished and the input file can be deleted.
	*/
	unlink(inputPath);
}

// calculate the line equation (ax+b) that fits them, and write in line
void leastSquaresMethod(char buf[SIZE], char wbuf[BLKSIZE])
{
    int i;
    double xsum = 0, ysum = 0, x2sum = 0, xysum = 0;
    double a = 0, b = 0;
    char temp[BLKSIZE];

    int x, y;

    for (i = 0 ; i < SIZE ; i += 2)
    {
    	x = (int)((unsigned char) buf[i]);
    	y = (int)((unsigned char) buf[i+1]);

        xsum += x;
        ysum += y;
        x2sum += (x * x);
        xysum += x * y;
    }

    a = ((NUMBER * xysum) - (xsum * ysum)) / ((NUMBER * x2sum) - (xsum * xsum));
    b = (ysum - (a * xsum)) / NUMBER;

    // All mathematical operations will be realized with an accuracy down to 3 decimal points
    if(b > 0) sprintf(temp, "%.3lfx+%.3lf\n", a, b);
    else sprintf(temp, "%.3lfx%.3lf\n", a, b);
    strcat(wbuf,temp);
}

// Calculate Metrics and add in it's array.
void calculateMetrics(char line[], double TMAE[], double TMSE[], double TRMSE[], int index)
{
	int i;
	char temp[BLKSIZE] = "";
	int count = 0;
	int x[10], y[10];
	double MAE = 0, MSE = 0, RMSE = 0;
	double a = 0, b = 0, nuy = 0;

	for(i = 0; i < strlen(line); i++)
	{
		if(count >= SIZE)
		{
			// Get line equation
		    if(line[i] != ' ')
    		{
    			if(line[i] == 'x' || i == strlen(line) - 1)
    			{
    				if(count % 2 == 0) a = atof(temp);
    				else b = atof(temp);
    				strcpy(temp, "");
    				count++;
    			}
    			else strncat(temp, &line[i], 1);
    		}
		}

		// Get the coordinates
		else if(line[i] != '(' && line[i] != ')' && line[i] != ' ')
		{
			if(line[i] == ',')
			{
				if(count % 2 == 0) x[count/2] = atoi(temp);
				else y[count/2] = atoi(temp);
				strcpy(temp, "");
				count++;
			}
			else strncat(temp, &line[i], 1);
		}
	}
	// Calculate MAE and MSE according to the their formula
	for(i = 0 ; i < NUMBER; i++)
	{
	    nuy = (a * x[i]) + b;
		MAE += abs(nuy - y[i]);
		MSE += ((nuy - y[i]) * (nuy - y[i]));
	}

	MAE /= NUMBER;
	MSE /= NUMBER;
	RMSE = sqrt(MSE);

	// For each line these result keep in arrays.
	// All mathematical operations will be realized with an accuracy down to 3 decimal points
	TMAE[index] = round(MAE * 1000) / 1000;
	TMSE[index] = round(MSE * 1000) / 1000;
	TRMSE[index] = round(RMSE * 1000) / 1000;

	strcpy(temp, "");
	// Write end of line.
	sprintf(temp, ", %.3lf, %.3lf, %.3lf\n", MAE, MSE, RMSE);
    strcat(line,temp);
}

// Calculate Mean
double calculateMean(double Metric[], int count)
{
	double sum = 0;
	int i;

	for(i = 0; i < count; i++)
	{
		sum += Metric[i];
	}

	sum /= count;

	// All mathematical operations will be realized with an accuracy down to 3 decimal points
	sum = round(sum * 1000) / 1000;

	return sum;
}

// Calculate Mean Deviation
double calculateMD(double Metric[], double mean, int count)
{
	double sum = 0;
	int i;

	for(i = 0; i < count; i++)
	{
		sum += abs(mean - Metric[i]);
	}

	sum /= count;

	// All mathematical operations will be realized with an accuracy down to 3 decimal points
	sum = round(sum * 1000) / 1000;

	return sum;
}

// Calculate Standart Deviation
double calculateSD(double Metric[], double mean, int count)
{
    double SD = 0;
    int i;

    for (i = 0; i < count; i++)
    {
        SD += ((Metric[i] - mean) * (Metric[i] - mean));
    }

    SD = sqrt(SD / count);

    // All mathematical operations will be realized with an accuracy down to 3 decimal points
    SD = round(SD * 1000) / 1000;

    return SD;
}

// Signal Catcher
void catcher(int sig)
{
	/*
		If the SIGUSR1 signal comes, the value of the global variable used for P2 becomes 0
		and the input file is deleted.
	*/
    if(sig == SIGUSR1)
    {
    	flag2 = 0;
        unlink(inputPath);
    }
    /*
    	In case of SIGTERM,
    	in either P1 or P2, your processes must catch it
    	and clean up after them before exiting gracefully,
    	by closing open files, and removing the input and temporary files from disk.
    	Parent after these operations send kill signal to child.
    	Child after these operations send kill signal to parent.
    */
    else if(sig == SIGTERM)
    {
		close(fdout);
		close(fdin);
		close(fd);
		close(fd2);

		unlink(template);
    	if(inputPath != NULL)
    		unlink(inputPath);

    	if(child_pid == 0)
    	{
    		if(MAE != NULL)
    			free(MAE);
    		if(MSE != NULL)
    			free(MSE);
    		if(RMSE != NULL)
    			free(RMSE);
    		if(rewrite != NULL)
    			free(rewrite);
    		kill(getppid(), SIGKILL);
    	}
    	else
    		kill(child_pid, SIGKILL);

        exit(0);
    }

    // If there is a SIGINT signal in the critical section, 1 is assigned to the flag.
    else if(sig == SIGINT)
    {
    	criticalFlag = 1;
    }
}

// Print receive signal in critical section.
void printSignal()
{
	int i;

	if(criticalFlag == 0)
		printf(" There was no signal while in the Process 1 critical section.\n");
	else
		printf("Received signal: SIGINT\n");
}
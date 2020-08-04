#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#define SIZE 32
#define BLKSIZE 1024

int main(int argc, char** argv) {

	char* inputPath;
	char* outputPath;
	int time;
	int opt;
	int bytesread;
	int byteswritten;
	char* bp;
	char buf[SIZE];
	char wbuf[BLKSIZE];
	char temp[BLKSIZE];
	int ctemp = 0;
	int ctemp2 = 0;
	int i = 0, j = 0, z = 0;
	struct flock lock;
	memset(&lock, 0, sizeof(lock));
	struct stat fileStat;
	char* rewrite;

	if(argc != 7)
	{
		printf("This usage is wrong. \n");
		printf("Usage: ./programA -i inputPathA -o outputPathA -t time \n");
		return 1;
	}

  	while ((opt = getopt (argc, argv, "iot:")) != -1)
  	{
	    switch (opt)
	      {
	      case 'i':
	        inputPath = argv[optind];
	        break;
	      case 'o':
	        outputPath = argv[optind];
	        break;
	      case 't':
	        time = atoi(optarg);
	        break;
	      default:
	        printf("This usage is wrong. \n");
			printf("Usage: ./programA -i inputPathA -o outputPathA -t time \n");
			return 1;
	      }
	}

	int fdin = open(inputPath, O_RDONLY);
	if(fdin == -1)
	{
		perror("open inputPath");
		return 1;
	}

	int fdout = open(outputPath, O_RDWR);
	if(fdout == -1)
	{
		perror("open and create outputPath");
		return 1;
	}

	if(time < 1 || time > 50)
	{
		printf("time must be in the range [1.50].\n");
		return 1;
	}

	time /= 1000;

	for( ; ; )
	{
		strcpy(wbuf, "");
		strcpy(temp, "");
		strcpy(buf, "");
		ctemp = 0;
		ctemp2 = 0;
		bp = NULL;
		i = 0;
		j = 0;
		z = 0;
		while(((bytesread = read(fdin, buf, SIZE)) == -1) && (errno == EINTR));
		if (bytesread <= 0)
			break;
		if(bytesread != 32) break;
		while(i < SIZE)
		{
			sprintf(temp, "%d", (int) buf[i]);
			strcat(wbuf,temp);
			strcat(wbuf,"+i");
			sprintf(temp, "%d", (int) buf[i+1]);
			strcat(wbuf,temp);
			i += 2;
			if(i != SIZE) strcat(wbuf,",");	
		}

		lock.l_type = F_WRLCK;
		fcntl(fdout, F_SETLKW, &lock);

		bp = wbuf;
		strcpy(temp, " ");
		i = 1;
		z = 0;
		while(i == 1)
		{
			i = read(fdout, temp, 1);
			ctemp = (int) temp[0];
			if((ctemp == 10 && ctemp2 == 10) | (char)ctemp == '\n' && (char)ctemp2 == '\n')
			{
				ctemp2 = ctemp;
				break;
			}
			ctemp2 = ctemp;
			z++;
		}
		z--;
		if(i != 0)
		{

			j = strlen(wbuf);
			lseek(fdout,-1,SEEK_CUR);

			if(stat(outputPath,&fileStat) < 0) {printf("stat error. Check stat function"); return -1;}
			
			rewrite = (char*) malloc(fileStat.st_size - z );
		
			while(((bytesread = read(fdout, rewrite, fileStat.st_size - z)) == -1) && (errno == EINTR));
			if (bytesread <= 0)
				break;

			lseek(fdout, -1 * (fileStat.st_size - z - 1) , SEEK_END);
			
			truncate(outputPath, fileStat.st_size + j);

			while(j > 0)
			{
				while(((byteswritten = write(fdout, bp, j)) == -1) && (errno == EINTR));
				if(byteswritten < 0)
					break;
				j -= byteswritten;
				bp += byteswritten;
				if(byteswritten == -1)
				break;
			}

			bp = rewrite;
			j = strlen(rewrite);
			
			while(j > 0)
			{
				while(((byteswritten = write(fdout, bp, j)) == -1) && (errno == EINTR));
				if(byteswritten < 0)
					break;
				j -= byteswritten;
				bp += byteswritten;
				if(byteswritten == -1)
				break;
			}
			strcpy(rewrite," ");
			free(rewrite);

			lock.l_type = F_UNLCK;
			fcntl(fdout, F_SETLKW, &lock);
		}
		else
		{
			lseek(fdout,0,SEEK_END);
			strcpy(temp, "\n");
			strcat(temp,wbuf);
			strcpy(wbuf,temp);
			j = strlen(wbuf);
			while(j > 0)
			{
				while(((byteswritten = write(fdout, bp, j)) == -1) && (errno == EINTR));
				if(byteswritten < 0)
					break;
				j -= byteswritten;
				bp += byteswritten;
				if(byteswritten == -1)
				break;
			}
			lock.l_type = F_UNLCK;
			fcntl(fdout, F_SETLKW, &lock);
		}

		sleep(time);
		lseek(fdout,0,SEEK_SET);
	}
			
	close(fdin);
	close(fdout);
    
}
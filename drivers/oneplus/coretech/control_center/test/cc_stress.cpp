#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <fstream>
#include <string>
#include <pthread.h>
#include <iostream>
#include <time.h>

using namespace std;
int debug = 0;

static void *cc_func1(void *arg __unused) {
	FILE *fp = fopen("/proc/self/tb_ctl", "wb");

	if (debug)
		printf ("    pthread: pid %d ppid %d tid %d, func %s\n", getpid(), getppid(), gettid(), __func__);

	while (1) {
		int tid = gettid();
		char buf[100] = {0};

		snprintf(buf, 100, "4,0,%d,%d,0,0,%d,10\n", tid, tid, rand()%5 + 3);

		if (debug)
			printf ("%s\n", buf);

		fwrite(buf, 1, strlen(buf), fp);
		fseek(fp, 0, SEEK_SET);
		usleep(rand() % 8000 + 2000);
	}
	fclose(fp);
	return NULL;
}

static void *cc_func2(void *arg __unused) {
	FILE *fp = fopen("/sys/module/houston/parameters/tb_ctl", "wb");

	if (debug)
		printf ("    pthread: pid %d ppid %d tid %d, func %s\n", getpid(), getppid(), gettid(), __func__);

	while (1) {
		int tid = gettid();
		char buf[100] = {0};

		snprintf(buf, 100, "0,0,%d,%d,%d,20\n", tid, tid, rand()%5 + 3);

		if (debug)
			printf ("%s\n", buf);

		fwrite(buf, 1, strlen(buf), fp);
		fseek(fp, 0, SEEK_SET);
		usleep(rand() % 8000 + 2000);
	}
	fclose(fp);
	return NULL;
}

int main(int argc __unused, char *argv[]) {
	int pid = 0;
	int now = 0;
	int count = 1000;
	time_t start, end;
	double diff;

	int c;
	while ((c = getopt (argc, argv, "c:v")) != -1) {
		switch (c)
		{
			case 'v':
				debug = 1;
				break;
			case 'c':
				count = atoi(optarg);
				break;
			default:
				abort ();
		}
	}

	time (&start);

	srand(time(0));
	while (now != count) {
		pid = fork();
		printf ("Test: %d / %d\n", ++now, count);
		if (pid) {
			if (debug)
				printf ("parent: pid %d ppid %d tid %d, child: %d\n", getpid(), getppid(), gettid(), pid);

			usleep (rand() % 80000 + 100000);

			if (debug)
				printf ("kill child %d\n", pid);

			kill (pid, SIGKILL);
		} else {
			#define PTHREAD_SIZE 8
			pthread_t child[PTHREAD_SIZE];

			for (int i = 0; i < PTHREAD_SIZE; ++i)
				pthread_create(&child[i], NULL, (i & 0x1) ? &cc_func1 : &cc_func2, NULL);

			if (debug)
				printf ("child: pid %d ppid %d tid %d\n", getpid(), getppid(), gettid());

			for (int i = 0; i < PTHREAD_SIZE; ++i)
				pthread_join(child[i], NULL);
		}
	}

	time (&end);

	diff = difftime(end, start);
	printf ("execution time = %f sec\n", diff);
	return 0;
}

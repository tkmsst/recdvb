#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <libgen.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>

#include <sys/ioctl.h>

#include "config.h"
#include "decoder.h"
#include "mkpath.h"
#include "recpt1core.h"

#include <sys/ipc.h>
#include <sys/msg.h>
#include "tssplitter_lite.h"

/* prototypes */
int tune(char *channel, thread_data *tdata, int dev_num);
int close_tuner(thread_data *tdata);

void cleanup(thread_data *tdata)
{
	f_exit = TRUE;
}

/* will be signal handler thread */
void *process_signals(void *data)
{
	sigset_t waitset;
	int sig;
	thread_data *tdata = (thread_data *)data;

	sigemptyset(&waitset);
	sigaddset(&waitset, SIGINT);
	sigaddset(&waitset, SIGTERM);
	sigaddset(&waitset, SIGUSR1);

	sigwait(&waitset, &sig);

	switch (sig) {
		case SIGINT:
			fprintf(stderr, "\nSIGINT received. cleaning up...\n");
			cleanup(tdata);
			break;
		case SIGTERM:
			fprintf(stderr, "\nSIGTERM received. cleaning up...\n");
			cleanup(tdata);
			break;
		case SIGUSR1: /* normal exit*/
			cleanup(tdata);
			break;
	}

	return NULL; /* dummy */
}

void init_signal_handlers(pthread_t *signal_thread, thread_data *tdata)
{
	sigset_t blockset;

	sigemptyset(&blockset);
	sigaddset(&blockset, SIGINT);
	sigaddset(&blockset, SIGTERM);
	sigaddset(&blockset, SIGUSR1);

	if (pthread_sigmask(SIG_BLOCK, &blockset, NULL))
		fprintf(stderr, "pthread_sigmask() failed.\n");

	pthread_create(signal_thread, NULL, process_signals, tdata);
}

void show_usage(char *cmd)
{
	fprintf(stderr, "Usage: \n%s [--dev devicenumber] [--lnb voltage] [--bell] channel\n", cmd);
	fprintf(stderr, "\n");
}

void show_options(void)
{
	fprintf(stderr, "Options:\n");
	fprintf(stderr, "--dev N:             Use DVB device /dev/dvb/adapterN\n");
	fprintf(stderr, "--lnb voltage:       Specify LNB voltage (0, 11, 15)\n");
	fprintf(stderr, "--bell:              Notify signal quality by bell\n");
	fprintf(stderr, "--help:              Show this help\n");
	fprintf(stderr, "--version:           Show version\n");
	fprintf(stderr, "--list:              Show channel list\n");
}

int main(int argc, char **argv)
{
	pthread_t signal_thread;
	static thread_data tdata;
	int result;
	int option_index;
	struct option long_options[] = {
		{"bell",    0, NULL, 'b'},
		{"help",    0, NULL, 'h'},
		{"version", 0, NULL, 'v'},
		{"list",    0, NULL, 'l'},
		{"LNB",     1, NULL, 'n'},
		{"lnb",     1, NULL, 'n'},
		{"dev",     1, NULL, 'd'},
		{0, 0, NULL, 0} /* terminate */
	};

	tdata.lnb = -1;
	tdata.tfd = -1;
	tdata.fefd = 0;
	tdata.dmxfd = 0;
	int dev_num = -1;
	int val;
	char *voltage[] = {"11V", "15V", "0V"};
	boolean use_bell = FALSE;

	while ((result = getopt_long(argc, argv, "bhvln:d:",
								 long_options, &option_index)) != -1) {
		switch (result) {
			case 'b':
				use_bell = TRUE;
				break;
			case 'h':
				fprintf(stderr, "\n");
				show_usage(argv[0]);
				fprintf(stderr, "\n");
				show_options();
				fprintf(stderr, "\n");
				show_channels();
				fprintf(stderr, "\n");
				exit(0);
				break;
			case 'v':
				fprintf(stderr, "%s %s\n", argv[0], version);
				fprintf(stderr, "signal check utility for DVB tuner.\n");
				exit(0);
				break;
			case 'l':
				show_channels();
				exit(0);
				break;
			/* following options require argument */
			case 'n':
				val = atoi(optarg);
				switch (val) {
					case 11:
						tdata.lnb = 0;  // SEC_VOLTAGE_13 日本は11V(PT1/2/3は12V)
						break;
					case 15:
						tdata.lnb = 1;  // SEC_VOLTAGE_18 日本は15V
						break;
					default:
						tdata.lnb = 2;  // SEC_VOLTAGE_OFF
						break;
				}
				fprintf(stderr, "LNB = %s\n", voltage[tdata.lnb]);
				break;
			case 'd':
				dev_num = atoi(optarg);
				break;
		}
	}

	if (argc - optind < 1) {
		fprintf(stderr, "channel must be specified!\n");
		fprintf(stderr, "Try '%s --help' for more information.\n", argv[0]);
		return 1;
	}

	/* set tune_persistent flag */
	tdata.tune_persistent = TRUE;

	/* spawn signal handler thread */
	init_signal_handlers(&signal_thread, &tdata);

	/* tune */
	if (tune(argv[optind], &tdata, dev_num) != 0)
		return 1;

	while (1) {
		if (f_exit)
			break;
		/* show signal strength */
		calc_cn(tdata.fefd, tdata.table->type, use_bell);
		sleep(1);
	}

	/* wait for signal thread */
	pthread_kill(signal_thread, SIGUSR1);
	pthread_join(signal_thread, NULL);

	/* close tuner */
	if (close_tuner(&tdata) != 0)
		return 1;

	return 0;
}

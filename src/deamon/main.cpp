/* getopt, setsid, getpid, dup2, sleep */
#include <unistd.h>

/*	memset */
#include <string.h>

/* open, kill */
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

/* sigaction, kill */
#include <signal.h>

/* fprintf, fopen */
#include <stdio.h>

/* malloc, free */
#include <stdlib.h>

/* errno */
#include <errno.h>

#include <map>
#include <vector>
#include <string>

#include "utils/coacommon.h"
#include "utils/config/config.h"
#include "utils/log/log.h"
#include "../manager/coam.h"
#include "main.h"

static void sig_handler(int sig);

pid_t g_pidPid;
int g_iEvent;
extern SMainConfig g_soMainCfg;
extern CLog g_coLog;
extern CConfig g_coConf;
char g_mcDebugFooter[256];
SSignalDesc g_soSigDesc[] = {
	{SIGABRT, 'A', "Process abort signal."},
	{SIGBUS, 'A', "Access to an undefined portion of a memory object."},
	{SIGFPE, 'A', "Erroneous arithmetic operation."},
	{SIGILL, 'A', "Illegal instruction."},
	{SIGQUIT, 'A', "Terminal quit signal."},
	{SIGSEGV, 'A', "Invalid memory reference."},
	{SIGSYS, 'A', "Bad system call."},
	{SIGTRAP, 'A', "Trace/breakpoint trap."},
	{SIGXCPU, 'A', "CPU time limit exceeded."},
	{SIGXFSZ, 'A', "File size limit exceeded."},
	{SIGCONT, 'C', "Continue executing, if stopped."},
	{SIGCHLD, 'I', "Child process terminated, stopped, or continued."},
	{SIGURG, 'I', "High bandwidth data is available at a socket."},
	{SIGSTOP, 'S', "Stop executing (cannot be caught or ignored)."},
	{SIGTSTP, 'S', "Terminal stop signal."},
	{SIGTTIN, 'S', "Background process attempting read."},
	{SIGTTOU, 'S', "Background process attempting write."},
	{SIGALRM, 'T', "Alarm clock."},
	{SIGHUP, 'T', "Hangup."},
	{SIGINT, 'T', "Terminal interrupt signal."},
	{SIGKILL, 'T', "Kill (cannot be caught or ignored)."},
	{SIGPIPE, 'T', "Write  on a pipe with no one to read it."},
	{SIGTERM, 'T', "Termination signal."},
	{SIGUSR1, 'T', "User-defined signal 1."},
	{SIGUSR2, 'T', "User-defined signal 2."},
	{SIGPOLL, 'T', "Pollable event."},
	{SIGPROF, 'T', "Profiling timer expired."},
	{SIGVTALRM, 'T', "Virtual timer expired."}};

int main(int argc, char *argv[])
{
	int iRetCode = 0;
	int iArgVal;
	int iDontFork = false;
	char *pszConf = NULL;

	struct sigaction soSigAct;

	/*
	 *	Ensure that the configuration is initialized.
	 */
	memset (&g_soMainCfg, 0, sizeof(g_soMainCfg));
	memset (g_mcDebugFooter, '\t', sizeof(g_mcDebugFooter));
	g_mcDebugFooter[sizeof(g_mcDebugFooter)/sizeof(*g_mcDebugFooter)-1] = '\0';

	memset (&soSigAct, 0, sizeof(soSigAct));
	soSigAct.sa_flags = 0 ;
	sigemptyset (&soSigAct.sa_mask) ;

	/*
	 *	Don't put output anywhere until we get told a little
	 *	more.
	 */

	/*  Process the options.  */
	while ((iArgVal = getopt (argc, argv, "d:fp:x:")) != EOF) {

		switch(iArgVal) {

			case 'd':
				if (pszConf) free (pszConf);
				pszConf = strdup (optarg);
				break;

			case 'f':
				iDontFork = true;
				break;

			case 'p':	/* pid file */
				g_soMainCfg.m_pszPidFile = strdup(optarg);
				break;

			default:
				break;
		}
	}
	if (NULL == pszConf) {
		printf ("Configuration dirictory not defined\n");
		return -1;
	}
	if (LoadConf (pszConf)) {
		printf ("LoadConf failed\n");
		return -1;
	}

	/*
	 *  Disconnect from session
	 */
	if (iDontFork == false) {
		pid_t pid = fork();

		if (pid < 0) {
			printf("Couldn't fork: '%s'\n", strerror(errno));
			exit(1);
		}

		/*
		 *  The parent exits, so the child can run in the background.
		 */
		if (pid > 0) {
			exit(0);
		}
		setsid();
	}

	/*
	 *  Ensure that we're using the CORRECT pid after forking,
	 *  NOT the one we started with.
	 */
	g_pidPid = getpid();

	/*
	 *  Only write the PID file if we're running as a daemon.
	 *
	 *  And write it AFTER we've forked, so that we write the
	 *  correct PID.
	 */
	if (iDontFork == false) {
		FILE *fp;

		fp = fopen(g_soMainCfg.m_pszPidFile, "w");
		if (fp != NULL) {
			/*
			 *	FIXME: What about following symlinks,
			 *	and having it over-write a normal file?
			 */
			fprintf(fp, "%d\n", (int) g_pidPid);
			fclose(fp);
		}
		else {
			printf ("Failed creating PID file '%s': '%s'\n", g_soMainCfg.m_pszPidFile, strerror(errno));
			exit(1);
		}
	}

	/*
	 *	If we're running as a daemon, close the default file
	 *	descriptors, AFTER forking.
	 */
	int devnull;

	devnull = open ("/dev/null", O_RDWR);
	if (devnull < 0) {
		printf("Failed opening /dev/null: '%s'\n", strerror(errno));
		exit(1);
	}
	dup2 (devnull, STDIN_FILENO);
	dup2 (devnull, STDOUT_FILENO);
	dup2 (devnull, STDERR_FILENO);
	close (devnull);

	/*
	 *	Now that we've set everything up, we can install the signal
	 *	handlers.  Before this, if we get any signal, we don't know
	 *	what to do, so we might as well do the default, and die.
	 */
	soSigAct.sa_handler = sig_handler;
	sigaction( SIGTERM, &soSigAct, NULL );
	sigaction( SIGINT, &soSigAct, NULL );

	do {
		if (InitCoAManager ()) {
			break;
		}
		UTL_LOG_N(g_coLog, "Program initialized successfully");

		/*
		 *	Process requests until HUP or exit.
		 */
		while (g_iEvent != 'T'
			&& g_iEvent != 'A') {
				sleep (1);
				RequestTimer ();
		}
		DeInitCoAManager();

	} while (0);

	if (iRetCode < 0) {
		UTL_LOG_F(g_coLog, "exiting due to internal error");
		iRetCode = 2;
	} else {
		UTL_LOG_N(g_coLog, "exiting normally");
	}

	/*
	 *	We're exiting, so we can delete the PID
	 *	file.  (If it doesn't exist, we can ignore
	 *	the error returned by unlink)
	 */
	if (iDontFork == false) {
		unlink(g_soMainCfg.m_pszPidFile);
	}

	if (pszConf) {
		free (pszConf);
	}

	return (iRetCode - 1);
}

static void sig_handler(int sig)
{
	if (getpid() != g_pidPid) _exit(sig);

	SSignalDesc *psoSigDesc = NULL;
	for (size_t i = 0; i < sizeof(g_soSigDesc)/sizeof(*g_soSigDesc); ++i) {
		if (g_soSigDesc[i].m_iSigCode == sig) {
			psoSigDesc = &g_soSigDesc[i];
			break;
		}
	}
	char cAction;
	if (psoSigDesc) {
		cAction = psoSigDesc->m_cAction;
		UTL_LOG_N(g_coLog, "signal received: code '%d', description '%s'", sig, psoSigDesc->m_pszDescription);
	} else {
		cAction = -1;
		UTL_LOG_N(g_coLog, "signal received: code '%d'", sig);
	}

	g_iEvent = cAction;
}

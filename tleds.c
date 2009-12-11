/*
File: tleds.c
(X11) netTrafficLEDS - Copyright (C) 1997,1998  Jouni.Lohikoski@iki.fi
This can be run either on VT or in X. Works best when EUID is root.(opt -c)
If you like this alot, and kinda use this daily for months, and would
like to send me a postcard or $10 or offer a project, feel free to do so. 

<URL: http://www.iki.fi/Jouni.Lohikoski/tleds.html> for more info and for
the latest version.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; version 2 of the License.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

---
Makefile:
all:	xtleds tleds
xtleds:	tleds.c
gcc -O3 -D_GNU_SOURCE -Wall -o xtleds tleds.c -I /usr/X11R6/include/ \ 
	-L /usr/X11R6/lib/ -lX11
tleds:	tleds.c
gcc -DNO_X_SUPPORT -D_GNU_SOURCE -O3 -Wall -o tleds tleds.c 
---	
(x)tleds needs "XkbDisable" on v3.2 and v3.3 XFree, which is no good.
If you run tleds as root, "XkbDisable" is not needed.
Put following two lines in your XF86Config if you use xtleds from X.
"Xleds 2 3" is needed always when using X with tleds or with xtleds.
XF86Config:
XkbDisable 	# Needed when EUID non root and Xfree v3.2 or v3.3
Xleds 2 3	# This line is a must.
*/
#define VERSION	"1.05beta10"
#define MYNAME	"tleds"

/* Supported kernel version */
/* If you want to compile for Linux 2.1.x add -DKERNEL2_1 to gcc options. */
/* Currently kernel v2.1.97 is "tested", older v2.1.x kernels may not work */
#ifdef KERNEL2_1
#undef KERNEL2_1
#define KERNEL2_0 0
#else
#define KERNEL2_0 1
#endif

/* If you don't want X stuff. */
#ifdef NO_X_SUPPORT
#define REMOVE_X_CODE 1
#else
#define REMOVE_X_CODE 0
#endif

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <paths.h>
#include <ctype.h>
#include <stdlib.h>
#include <time.h>
#include <signal.h>
#if (! REMOVE_X_CODE)
#include <X11/Xlib.h>
#else
#define LedModeOff         0
#define LedModeOn          1
#endif
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/kd.h>
#include <linux/vt.h>
#include <assert.h>
#include <sys/utsname.h>

#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif
#define MAXLEN		255
#define KEYBOARDDEVICE	"/dev/console"
#define CURRENTTTY	"/dev/tty0"
#define MAXVT		12
#define NETDEVFILENAME	"/proc/net/dev"
#define TERMINATESTR	"Program (and child) terminated.\n"
#define DEEPSLEEP	10
#define	REMINDVTDELAY	15
#define DEFPPPDELAY 	200
#define DEFETHDELAY	50
#define CAPSLOCKLED	1
#define NUMLOCKLED	2
#define SCROLLLOCKLED	3
typedef enum {CLEAR = 0, SET = 1, TOGGLE = 2} LedMode;
typedef enum {DELAYED = 0, FINISH = 1, NOW = 2} ActionMode;
#if KERNEL2_0
#define FIELDCOUNT 	7	/* 12 really */
#else
#define FIELDCOUNT	11	/* 17 really, in v2.1.97 +/- N */
#endif

/* Function prototypes */
void		check_sanity ();
void		check_kernel_version ();
ulong		correct_caps (ulong ledVal);
void		create_pid_file (pid_t pid, const char* name);
void		detach_all_vt_leds (int wantDetach);
ulong		detach_vt_leds (int tty, int wantDetach);
char*		find_device_line (char* buffer, char* netDeviceName);
inline int	find_max_VT ();
pid_t		get_old_pid ();
int		get_sleeptime (int isDefinedByUser, char* interfaceName);
void		handle_my_argvs (char** interfaceName, int* sleeptime,
			int argc, char** argv);
inline int	is_on_X (int ttyFd);
int		kill_old_process ();
void		led (int what, LedMode mode, ActionMode doAction);
void		my_exit ();
void		my_signal_handler (int);
inline void	my_sleep (struct timeval sleeptimeval);
void		parent_wants_me_dead (int);
void 		report_traffic (char** list);
char**		split_on_blank (char* line);
inline void	clear_led (int what) { led(what, CLEAR, NOW); }
inline void	set_led (int what) { led(what, SET, NOW); }
inline void	toggle_led (int what) { led(what, TOGGLE, NOW); }
void		usage (char* name);

/* Global and static variables */
static const char	devFileName [] = NETDEVFILENAME;
static char		pidFileName [30] = ""; /* 30 should be enough */
static char		rootPidFileName [30] = "";
#if (! REMOVE_X_CODE)
static Display 		*myDisplay = NULL;
#else
static char		*myDisplay = NULL;
#endif
static int		keyboardDevice = 0;
static char		ttyLEDs [MAXVT] = {};
static ushort		previousActive = (ushort)(MAXVT + 1);
static int		remindVTcoef = 0;
static int		opt_b = FALSE, opt_d = FALSE, opt_h = FALSE,
		opt_k = FALSE, opt_q = FALSE, opt_v = FALSE,
		opt_V = FALSE, opt_c = FALSE;

/* The code */
int	main (int argc, char* argv [])
{
char*	interfaceName;
char	buffer [MAXLEN];
ulong	ledVal;	
char*	tmpPointer;
char**	list;
pid_t	pid;
int	sleeptime;
int	wasInDeepSleep;
struct timeval sleeptimeval;

interfaceName = NULL;
sleeptime = 0;
check_kernel_version(); /* May die here */
handle_my_argvs(&interfaceName, &sleeptime, argc, argv);
check_sanity();	/* Checks and maybe changes the option flags. */
#ifdef DEBUG
opt_b = TRUE;	/* We are debugging so don't go to the background */
#endif
if (opt_v && !opt_q) {
	printf(
	    "%s version %s, GNU GPL (c) 1998 Jouni.Lohikoski@iki.fi\n",
	    MYNAME, VERSION);
	printf("<URL: http://www.iki.fi/Jouni.Lohikoski/tleds.html>\n");
}
strcpy(pidFileName, _PATH_TMP);
strcpy(rootPidFileName, _PATH_VARRUN);
strcat(pidFileName, MYNAME); /* Was argv[0]. Probs coz/if path. */
strcat(rootPidFileName, MYNAME);
strcat(pidFileName, ".pid");
strcat(rootPidFileName, ".pid");
if (opt_k) {
	return kill_old_process();
}
if (opt_h) {
	usage(argv[0]);
	return 0;
}
if (! opt_q) {
	printf("Setting keyboard LEDs based on %s %s %s %s\n",
	    "changes of Receive/Transmit\npackets of", interfaceName,
	    "in", devFileName);
	printf("Delay between updates is %d milliseconds.\n",
	    sleeptime);
}
if (! find_device_line(buffer, interfaceName) && !opt_q) {
	printf(
	    "There is currently no such interface as %s in %s.\n%s\n",
	    interfaceName, devFileName,
	    "Maybe later there will be. Kill me (-k) if ya want.");
}

if(! opt_b) {
	if (-1 == (pid = fork())) {
		perror("tleds: fork");
		return 1;
	}
} else {
	pid = getpid();
}
if (pid) {
	create_pid_file(pid, argv[0]);
	if (! opt_q)
		printf("Running in %sground. Pid: %ld\n",
			(opt_b ? "fore" : "back"),
			(long)pid);
	if (! opt_b)
		exit(0);
}
if (atexit(my_exit)) {
	perror("tleds: atexit() failed");
	return 1;
}
if (! opt_b) {
	signal(SIGUSR1, parent_wants_me_dead);
}
signal(SIGHUP, SIG_IGN);
signal(SIGTERM, my_signal_handler);
signal(SIGINT, my_signal_handler);
signal(SIGQUIT, my_signal_handler);
signal(SIGTSTP, my_signal_handler); 
signal(SIGUSR2, SIG_IGN);
signal(SIGPIPE, my_signal_handler);
if (! geteuid()) {	/* We are running as EUID root - CONSOLE */
	if (-1 == (keyboardDevice = open(KEYBOARDDEVICE, O_RDONLY))) {
		perror("tleds");
		fprintf(stderr, "%s:%s", KEYBOARDDEVICE, TERMINATESTR);
		exit(1);
	}
} else {		/* EUID not root */
#if (! REMOVE_X_CODE)
	if (! (myDisplay = XOpenDisplay(NULL))		/* X  */
		&& ioctl(0, KDGETLED, &ledVal) ) { 	/* VT */
		perror(
		   "tleds: Can't open X DISPLAY on the current host.");
#else
	if (ioctl(0, KDGETLED, &ledVal) ) {
		perror("tleds: KDGETLED");
		fprintf(stderr,
			"Error reading current led setting.\n%s\n",
			"Maybe stdin is not a VT?");
#endif
		fprintf(stderr, TERMINATESTR);
		exit (1);
	}
}
sleeptimeval.tv_sec = (int)((long)sleeptime * 1000L) / 1000000L;
sleeptimeval.tv_usec = (int)((long)sleeptime * 1000L) % 1000000L;
remindVTcoef = (int)( (long)REMINDVTDELAY * 1000L / (long)sleeptime ); 
wasInDeepSleep = TRUE;

/* The main loop */
while (1) {
	if ((tmpPointer = find_device_line(buffer, interfaceName))) {
		if (wasInDeepSleep) {
			wasInDeepSleep = FALSE;
			detach_all_vt_leds(TRUE);
		}
		list = split_on_blank(tmpPointer);
		report_traffic(list);
		my_sleep(sleeptimeval);
	} else {
		if (! wasInDeepSleep) {
			wasInDeepSleep = TRUE;
			detach_all_vt_leds(FALSE);
			previousActive = (ushort)(MAXVT + 1);
		}
		sleep(DEEPSLEEP);
	}
}
return 0;	/* Yeah right, never gets this far. */
}

char*	find_device_line (char* buffer, char* netDeviceName)
{
static long	fileOffset = 0L; 
register FILE*	devFile;

if (! (devFile = fopen(devFileName, "r")) ) {
	perror(devFileName);
	exit(1);
}
/* Skip two lines. (the header) */
/* Two choices how to do this. Didn't find any differences in speed. */
#if 0
fgets(buffer, MAXLEN, devFile);
fgets(buffer, MAXLEN, devFile);
#else
if (fileOffset) {
	fseek(devFile, fileOffset, SEEK_SET);
} else {
	fgets(buffer, MAXLEN, devFile);
	fileOffset += (long)strlen(buffer);
	fgets(buffer, MAXLEN, devFile);
	fileOffset += (long)strlen(buffer);
}
#endif

while ( fgets(buffer, MAXLEN, devFile) ) {
	while(isblank(*buffer))
		buffer++;
	if (buffer == strstr(buffer, netDeviceName)) {
		fclose(devFile);
		return buffer;
	}
}
fclose(devFile);
return NULL;
}

void	my_sleep (struct timeval sleeptimeval)
{
#if 1
select(1, NULL, NULL, NULL, &sleeptimeval);
#else
usleep(sleeptimeval.tv_usec);
#endif
}

char**	split_on_blank (char* line)
{
/*
Has a "bug". If tried to monitor network traffic e.g. on "dummy".
Look at the end of this file for example /proc/net/dev listing.
*/

static char* 	list [FIELDCOUNT] = {};
register int 	i;

i = 0;
goto middle;	/* speed(?) hack */
for (; i < FIELDCOUNT; i++) {
	while (isblank(*line))
		line++;
middle:
	list[i] = line;
	while (! isblank(*line) && *line != ':' && *line != '\n')
		line++;
	*(line++) = '\0';
}
return list;
}

void	report_traffic (char** list)
{
static long	formerReceived = 0L;
static long	formerTransmitted = 0L;
register long	received, transmitted;

#if KERNEL2_0
received = atol(list[1]);
transmitted = atol(list[6]);
#else
received = atol(list[2]);
transmitted = atol(list[10]);	/* Kernel v2.1.119 */
#endif

if (received != formerReceived) {
	led(NUMLOCKLED, SET, DELAYED);
	formerReceived = received;
} else {
	led(NUMLOCKLED, CLEAR, DELAYED);
}

if (transmitted != formerTransmitted) {
	led(SCROLLLOCKLED, SET, FINISH);
	formerTransmitted = transmitted;
} else {
	led(SCROLLLOCKLED, CLEAR, FINISH);
}
}

void	led (int led, LedMode mode, ActionMode doAction)
/* Only LED_NUM can be ActionMode DELAYED */
{
static ulong	ledReminder = 0x00;
ulong		ledVal;
#if (! REMOVE_X_CODE)
XKeyboardControl values;
#endif
#ifdef DEBUG
printf("led(%d, %d)\n", led, (int)mode);
#endif
#if (! REMOVE_X_CODE)
if (myDisplay) {
	switch (mode) {
		case SET:
			values.led_mode = LedModeOn;
			break;
		case CLEAR:
			values.led_mode = LedModeOff;
			break;
		case TOGGLE:
			values.led_mode = LedModeOn;
	}
}
values.led = led;
#endif
if (myDisplay) {
#if (! REMOVE_X_CODE)
	XChangeKeyboardControl(myDisplay, KBLed | KBLedMode, &values);
	if (doAction != DELAYED)
		XSync(myDisplay, FALSE);
#endif
} else {
	if (doAction != FINISH) { 
		if (ioctl(keyboardDevice, KDGETLED, &ledVal)) {
			perror("tleds: KDGETLED");
			exit(1);
		}
	} else {
		ledVal = 0L;
	}
	switch (led) {
		case SCROLLLOCKLED:
			if (mode == SET)
				ledVal |= LED_SCR;
			else
				ledVal &= ~LED_SCR;
			break;
		case NUMLOCKLED:
			if (mode == SET)
				ledVal |= LED_NUM;
			else
				ledVal &= ~LED_NUM;
			break;
		default:
			perror("tleds: wrong led-value");
			exit(1);
	}
	if (opt_c && doAction != FINISH) {
		ledVal = correct_caps(ledVal);
	}
	if (doAction) { /* FINISH or NOW */
		if (doAction == FINISH)
			ledVal |= ledReminder;
		if (ioctl(keyboardDevice, KDSETLED, (char)ledVal)) {
			perror("tleds: KDSETLED");
			exit(1);
		}
		ledReminder = 0x00;
	} else {
		/* Well, we know from report_traffic(), LED_SCR is
		   processed later. OK, kludge. */
		ledReminder = ledVal & ~LED_SCR;
	}
}
}

int	is_on_X (int ttyFd)
{
long	mode;

if (ioctl(ttyFd, KDGETMODE, &mode))
	return TRUE;	/* perror is not wanted here */
return (mode & KD_GRAPHICS);
}

ulong	correct_caps (ulong ledVal)
{
static int	remindVTRound = 0;
struct vt_stat	vtStat;
int		currentVT;
ulong		flagVal;

currentVT = open(CURRENTTTY, O_RDONLY); /* Blah, only for root. */
if (-1 != currentVT) {
	if (! ioctl(currentVT, KDGKBLED, &flagVal)
	    && ! ioctl(currentVT, VT_GETSTATE, &vtStat)) {
		if (previousActive == --vtStat.v_active) {
			if (is_on_X(currentVT)) {
				ioctl(currentVT, KDGETLED, &flagVal);
			}
			if (flagVal & LED_CAP)
				ledVal |= LED_CAP;
			else
				ledVal &= ~LED_CAP;
			ttyLEDs[previousActive] = (char)ledVal;
		} else {
			previousActive = vtStat.v_active;
			ledVal = (ulong)ttyLEDs[previousActive];
		}
	}
	if(remindVTRound++ > remindVTcoef) {
		remindVTRound = 0;
		detach_vt_leds(currentVT, TRUE);
	}
	close(currentVT);
}
return ledVal;
}

ulong	detach_vt_leds (int tty, int wantDetach)
/* 
What I really would like to do, is to deattach only num-lock and scroll-lock
leds and leave caps-lock led attached to indicate current keyboard caps
lock flag. But Linux kernel (2.0.x) doesn't allow this, it's either all or
nothing. Someone should patch the kernel to correct this for 2.2.0?
*/
{
ulong	ledVal;

if (ioctl(tty, KDGETLED, &ledVal)) {
	return 0;
}
ledVal &= ~LED_SCR;
ledVal &= ~LED_NUM;
if (!wantDetach && !is_on_X(tty)) {
	ioctl(tty, KDGKBLED, &ledVal);
	ledVal |= 0x08;	/* Reattach. */
}
ioctl(tty, KDSETLED, (char)ledVal);
return ledVal;
}

void	detach_all_vt_leds (int wantDetach)
{
ulong	ledVal;
int	i, maxVT, tty;
char	ttyFileName [30];
char	ttyFileNameTmp [30];

strcpy(ttyFileName, _PATH_TTY);
maxVT = find_max_VT();
for (i=0; i <= maxVT; i++) {
	if (i > 0)
		ttyLEDs[i-1] = 0x00;
	/* No error checkings here, if we can't, we can't. */
	sprintf(ttyFileNameTmp, "%s%d", ttyFileName, i);
	if (-1 == (tty = open(ttyFileNameTmp, O_RDONLY))) {
		continue;
	}
	ledVal = detach_vt_leds(tty, wantDetach);
	if (i > 0)
		ttyLEDs[i-1] = (char)ledVal;
	close(tty);
}
}

int	find_max_VT ()
{
return MAXVT;
}

void	parent_wants_me_dead (int x)
{
exit(x);
}

void	my_signal_handler (int x)
{
exit(x);
}

void	my_exit ()
{
if (opt_b && ! opt_q)
	printf("Bye-Bye !\n");
if (myDisplay) {
#if (! REMOVE_X_CODE)
	clear_led(NUMLOCKLED);
	clear_led(SCROLLLOCKLED);
	XCloseDisplay(myDisplay);	/* X */
#endif
}
detach_all_vt_leds(FALSE);		/* re-attach */
if (keyboardDevice) 	/* EUID root - CONSOLE */
	close(keyboardDevice);
if(getpid() == get_old_pid()) {
	unlink(pidFileName);
	unlink(rootPidFileName);
}
}

int	kill_old_process ()
{
pid_t	pid, pid2;

if (! (pid = get_old_pid())) {
	if (!opt_q) {
		fprintf(stderr,
			"Couldn't find what to kill.\n");
		perror(pidFileName);
	}
	return 1;
}
kill(pid, SIGUSR1);
if (!opt_q) 
	printf("One moment...(3 secs)...\n");
sleep(3);
if ((pid2 = get_old_pid())) {
	if (!opt_q)
		fprintf(stderr,
		"PID: %d - Hmm...not sure if I succeeded in kill.\n",
			pid2);
	return 1;
}
if (! opt_q)
	printf("Killed. (The old PID was %d)\n", pid);
return 0;
}

void	create_pid_file (pid_t pid, const char* name)
{
FILE*		pidFile;
pid_t		oldPid;
char		procFileName [80];
char		*tmpPidFileName;
char		pidString [11]; /* "length" of UINT_MAX */
struct stat	status;
int		isAnother;

if (geteuid())
	tmpPidFileName = pidFileName;
else
	tmpPidFileName = rootPidFileName;  /* root */
/*
We check if there already is the *.pid file and if maybe the
process' (child) which created it is dead, so we could try to fix.
*/
isAnother = FALSE;
oldPid = (pid_t) 0;
if (! stat(pidFileName, &status)
    || ! stat(rootPidFileName, &status)) { 
	if ((oldPid = get_old_pid())) {
		strcpy(procFileName, "/proc/");
		sprintf(pidString, "%ld", (long)oldPid);
		strcat(procFileName, pidString);
		strcat(procFileName, "/environ");			
		if(! stat(procFileName, &status)) { /* The old proc. */
			isAnother = TRUE;
		} else {
		/* The old process was not alive, so we try to fix. */
			unlink(rootPidFileName);
			unlink(pidFileName);
			if (get_old_pid()) {
				fprintf(stderr,
				  "%s: Can't remove %s or %s\n%s\n",
				  MYNAME, pidFileName,
				  rootPidFileName,
				  "Program terminated.");
				  exit(1);
			}
		}
	} else {
		isAnother = TRUE;
	}				
}

if (isAnother) {
	if (oldPid)
		fprintf(stderr, "(The old PID %ld) ", (long)oldPid);
	fprintf(stderr, "%s %s runnning.\n%s %s %s\n",
		"\nSorry, can't run. There might be another",
		name, "If not, try: rm", pidFileName, rootPidFileName);
	kill(pid, SIGUSR1);
	exit(1);
}
	
if( !(pidFile = fopen(tmpPidFileName, "w"))) {
	perror(tmpPidFileName);
	kill(pid, SIGUSR1);
	exit(1);
}
fprintf(pidFile, "%ld\n", (long)pid);
fclose(pidFile);
if (chmod(tmpPidFileName, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)) {
	perror(tmpPidFileName);
	exit(1);
}
if (! geteuid()) {  /* EUID root */
	if (symlink(tmpPidFileName, pidFileName)) {
		perror(pidFileName);
		exit(1);
	}
}
}

pid_t	get_old_pid ()
{
FILE*	pidFile;
long	returnValue;

if (! (pidFile = fopen(pidFileName, "r"))) {
	if (! (pidFile = fopen(rootPidFileName, "r")))
		return (pid_t)0L;
}
fscanf(pidFile, "%ld", &returnValue);
fclose(pidFile);
return (pid_t)returnValue;
}

void	handle_my_argvs (char** interfaceName, int* sleeptime,
		int argc, char* argv [])
{
int	c;

while(EOF != (c = getopt(argc, argv, "bcd:hkqvV"))) {
	switch (c) {
		case 'V':
			opt_V = TRUE;
			break;
		case 'b':
			opt_b = TRUE;
			break;
		case 'c':
			opt_c = TRUE;
			break;
		case 'd':
			opt_d = TRUE;
			*sleeptime
				= get_sleeptime(TRUE, NULL);
			break;
		case 'h':
			opt_h = TRUE;
			break;
		case 'k':
			opt_k = TRUE;
			break;
		case 'q':
			opt_q = TRUE;
			break;
		case 'v':
			opt_v = TRUE;
			break;
		default:
			opt_h = TRUE;
			/* assert(0); */
	}
}
*interfaceName = argv[optind];
if (! *interfaceName || ! (*interfaceName)[0]) {
	opt_h = TRUE; /* We may also have opt_k so we won't get h. */
	return;
}
if (opt_V)
	printf("Marjo Helena Salmela on %svalehtelija ja paskiainen.\n",
		"minua kohtaan ollut "); /* Don't ask. */
if (! *sleeptime)
	*sleeptime = get_sleeptime(FALSE, *interfaceName);
}

void	check_sanity ()
{
if (opt_c && geteuid()) {
	opt_c = FALSE;
	if (! opt_q)
		fprintf(stderr,
		    "You have to be EUID root for -c. -c removed.\n");
}
}

int	get_sleeptime (int isDefinedByUser, char* interfaceName)
{
int	returnValue;

if (isDefinedByUser) {
	returnValue = atol(optarg);
	if (returnValue < 0 || returnValue > 10000) {
		opt_h = TRUE;  /* Illegal value. */
		return 0;
	}
	return returnValue;
} else {
/* Ok, we have to figure ourselves what would be good update delay. */
	if (interfaceName == strstr(interfaceName, "eth"))
		returnValue = DEFETHDELAY;
	else
		returnValue = DEFPPPDELAY;
	return returnValue;
}
}

void	check_kernel_version ()
{
struct utsname	buffer;

if (-1 == uname(&buffer)) {
	perror("tleds: check_kernel_version()");
	exit(1);
}
#if KERNEL2_0
if (strncmp("2.0.", (const char*)buffer.release, 4)) {
	fprintf(stderr,
		"%s was compiled for v2.0 kernel. Check Makefile. %s",
		MYNAME, TERMINATESTR);
	exit(1);
}
#else
if (! strncmp("2.0.", (const char*)buffer.release, 4)) {
	fprintf(stderr, "%s was compiled for v2.1 (2.2?) kernel. %s",
		MYNAME, TERMINATESTR);
	exit(1);
}
#endif
}

void	usage (char* name)
{
printf("Usage: %s [-bchkqv] [-d <update_delay>] <interface_name>\n",
    name);
printf("Example: %s -d 300 ppp0\n", name);
printf("Options:\n%s\n%s\n%s\n%s\n%s\n%s\n%s\n%s\n%s\n",
	"\t-b\tDon't go to the background.",
	"\t-c\tFix the CapsLED in VTs. Only for EUID root.",
	"\t-d N\tSet update delay.",
	"\t\tN must be between 1 and 10000 (milliseconds)",
	"\t-h\tHelp. (this)",
	"\t-k\tKill (old) (x)tleds running.",
	"\t-q\tBe quiet.",
	"\t-v\tPrint version information.",
	"\t\t(`cat /proc/net/dev` to see your interfaces.)");
}

/*
In v2.0.x kernels:
$ cat /proc/net/dev
Inter-|   Receive                  |  Transmit
face |packets errs drop fifo frame|packets errs drop fifo colls carrier
lo:      2    0    0    0    0        2    0    0    0     0    0
eth0:   3154    0    0    0    0     2553    0    0    0     0    0
dummy: No statistics available.
ppp0:  26619    0    0    0    0    42230    0    0    0     0    0
$

In v2.1 kernels they haven't obviously decided yet what it will be in v2.2.x

In v2.1.72 +/- N kernels:
$ cat /proc/net/dev
Inter-|   Receive                           |  Transmit
face |bytes    packets errs drop fifo frame|bytes    packets errs drop fifo colls carrier
lo:  388390    7747    0    0    0    0   388390     7747    0    0    0     0    0    0
ppp0:       0  496878   19   19    0    0        0   301287    0    0    0     0    0    0
$

In v2.1.97 +/- N kernels:
$ cat /proc/net/dev
Inter-|   Receive                                                |  Transmit
face |bytes    packets errs drop fifo frame compressed multicast|bytes packets errs drop fifo colls carrier compressed
lo: 4171066   19848    0    0    0     0          0         0  4171066 19848    0    0    0     0    0    0
eth0: 2699632   19917    0    0    0     0          0         0  15993153 23079    0    0    0  1983    0    0
ppp0:  391413    4588    1    0    0     0          0         0   4447 6560    0    0    0     0    0    0
$
*/   


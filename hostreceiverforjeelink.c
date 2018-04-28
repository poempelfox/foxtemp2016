/* $Id: hostreceiverforjeelink.c $
 * This is software for serving the data received from a HaWo tempdevice 2016
 * or a Foxtemp2016 device or some commercial temperature sensors using a
 * variant of the LaCrosse protocol to the network.
 * Data is received through a JeeLink that you will need to attach to some
 * USB port. The JeeLink will need to run the firmware for
 * FHEM, recompiled to support 'custom sensors' (the binary you usually
 * download does not have that compiled in).
 * This is basically a recycled hostsoftware.c from the ds1820tousb project,
 * which was in turn based on an example included in the reference
 * implementation of avrusb, although close to nothing of that should remain.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <sys/select.h>  /* According to POSIX.1-2001 */
#include <termios.h>
#include <ctype.h>

int verblev = 1;
#define VERBPRINT(lev, fmt...) \
        if (verblev > lev) { \
          printf(fmt); \
          fflush(stdout); \
        }
int runinforeground = 0;
unsigned char * serialport = "/dev/ttyUSB2";
int restartonerror = 0;

struct daemondata {
  unsigned char sensortype;
  unsigned char sensorid;
  unsigned int port;
  int fd;
  time_t lastseen;
  double lasttemp;
  double lasthum;
  double lastvoltage;
  unsigned char outputformat[1000];
  struct daemondata * next;
};

static void usage(char *name)
{
  printf("usage: %s [-v] [-q] [-d n] [-h] command <parameters>\n", name);
  printf(" -v     more verbose output. can be repeated numerous times.\n");
  printf(" -q     less verbose output. using this more than once will have no effect.\n");
  printf(" -d p   Port to which the Jeelink is attached (default: %s)\n", serialport);
  printf(" -r br  Select bitrate mode. -1 makes the JeeLink toggle, 1 or 9579\n");
  printf("        forces 9579 baud, 2 or 17241 forces 17241. The default 0 picks\n");
  printf("        a value based on the selected sensors.\n");
  printf(" -f     relevant for daemon mode only: run in foreground.\n");
  printf(" -h     show this help\n");
  printf("Valid commands are:\n");
  printf(" daemon   Daemonize and answer queries. This requires one or more\n");
  printf("          parameters in the format\n");
  printf("           [sensortype]sensorid:port[:outputformat]\n");
  printf("          where sensorid is the sensor-id-number of a sensor;\n");
  printf("          sensortype is one of: H for a hawotempdev2016 (this is also\n");
  printf("          the default if you omit the type), F for a foxtempdev2016, L for some\n");
  printf("          commercial sensors using the LaCrosse protocol;\n");
  printf("          port is a TCP port where the data from this sensor is to be served\n");
  printf("          The optional outputformat specifies how the\n");
  printf("          output to the network should look like.\n");
  printf("          Available are: %%S = sensorid, %%T = temperature, %%H = humidity,\n");
  printf("          %%L = last seen timestamp. The default is '%%S %%T'.\n");
  printf("          Examples: 'H42:31337'   'F23:7777:%%T %%H'\n");
}

void sigpipehandler(int bla) { /* Dummyhandler for catching the event */
  return;
}

void logaccess(struct sockaddr * soa, int soalen, char * txt) {
  struct sockaddr_in * sav4;
  struct sockaddr_in6 * sav6;

  if (soalen == sizeof(struct sockaddr_in6)) {
    sav6 = (struct sockaddr_in6 *)soa;
    if ((sav6->sin6_addr.s6_addr[ 0] == 0)
     && (sav6->sin6_addr.s6_addr[ 1] == 0)
     && (sav6->sin6_addr.s6_addr[ 2] == 0)
     && (sav6->sin6_addr.s6_addr[ 3] == 0)
     && (sav6->sin6_addr.s6_addr[ 4] == 0)
     && (sav6->sin6_addr.s6_addr[ 5] == 0)
     && (sav6->sin6_addr.s6_addr[ 6] == 0)
     && (sav6->sin6_addr.s6_addr[ 7] == 0)
     && (sav6->sin6_addr.s6_addr[ 8] == 0)
     && (sav6->sin6_addr.s6_addr[ 9] == 0)
     && (sav6->sin6_addr.s6_addr[10] == 0xFF)
     && (sav6->sin6_addr.s6_addr[11] == 0xFF)) {
      /* This is really a IPv4 not a V6 access, so log it as
       * a such. */
      VERBPRINT(2, "%d.%d.%d.%d\t%s\n", sav6->sin6_addr.s6_addr[12],
              sav6->sin6_addr.s6_addr[13],
              sav6->sin6_addr.s6_addr[14],
              sav6->sin6_addr.s6_addr[15], txt);
    } else {
      /* True IPv6 access */
      VERBPRINT(2, "%x:%x:%x:%x:%x:%x:%x:%x\t%s\n",
              (sav6->sin6_addr.s6_addr[ 0] << 8) | sav6->sin6_addr.s6_addr[ 1],
              (sav6->sin6_addr.s6_addr[ 2] << 8) | sav6->sin6_addr.s6_addr[ 3],
              (sav6->sin6_addr.s6_addr[ 4] << 8) | sav6->sin6_addr.s6_addr[ 5],
              (sav6->sin6_addr.s6_addr[ 6] << 8) | sav6->sin6_addr.s6_addr[ 7],
              (sav6->sin6_addr.s6_addr[ 8] << 8) | sav6->sin6_addr.s6_addr[ 9],
              (sav6->sin6_addr.s6_addr[10] << 8) | sav6->sin6_addr.s6_addr[11],
              (sav6->sin6_addr.s6_addr[12] << 8) | sav6->sin6_addr.s6_addr[13],
              (sav6->sin6_addr.s6_addr[14] << 8) | sav6->sin6_addr.s6_addr[15],
              txt);
    }
  } else if (soalen == sizeof(struct sockaddr_in)) {
    unsigned char brokeni32[4];

    sav4 = (struct sockaddr_in *)soa;
    brokeni32[0] = (sav4->sin_addr.s_addr & 0xFF000000UL) >> 24;
    brokeni32[1] = (sav4->sin_addr.s_addr & 0x00FF0000UL) >> 16;
    brokeni32[2] = (sav4->sin_addr.s_addr & 0x0000FF00UL) >>  8;
    brokeni32[3] = (sav4->sin_addr.s_addr & 0x000000FFUL) >>  0;
    VERBPRINT(2, "%d.%d.%d.%d\t%s\n", brokeni32[0], brokeni32[1],
            brokeni32[2], brokeni32[3], txt);
  } else {
    VERBPRINT(2, "!UNKNOWN_ADDRESS_TYPE!\t%s\n", txt);
  }
}

static void printtooutbuf(char * outbuf, int oblen, struct daemondata * dd) {
  unsigned char * pos = &dd->outputformat[0];
  while (*pos != 0) {
    if (*pos == '%') {
      pos++;
      if        (*pos == 'S') { /* SensorID */
        outbuf += sprintf(outbuf, "0x%02x", dd->sensorid);
      } else if ((*pos == 'T') || (*pos == 't')) { /* Temperature */
        if ((dd->lastseen + 180) < time(NULL)) { /* Stale data / no data yet */
          outbuf += sprintf(outbuf, "%s", "N/A");
        } else {
          if (*pos == 'T') { /* fixed width */
            outbuf += sprintf(outbuf, "%6.2lf", dd->lasttemp);
          } else { /* variable width. */
            outbuf += sprintf(outbuf, "%.2lf", dd->lasttemp);
          }
        }
      } else if ((*pos == 'H') || (*pos == 'h')
              || (*pos == 'F') || (*pos == 'f')) { /* Humidity */
        if ((dd->lastseen + 180) < time(NULL)) { /* Stale data / no data yet */
          outbuf += sprintf(outbuf, "%s", "N/A");
        } else {
          if (*pos == 'H') { /* fixed width, 2 digits after the comma */
            outbuf += sprintf(outbuf, "%6.2lf", dd->lasthum);
          } else if (*pos == 'h') { /* variable width, 2 digits after the comma. */
            outbuf += sprintf(outbuf, "%.2lf", dd->lasthum);
          } else if (*pos == 'F') { /* fixed width, 1 digit after the comma. */
            outbuf += sprintf(outbuf, "%5.1lf", dd->lasthum);
          } else if (*pos == 'f') { /* variable width, 1 digit after the comma. */
            outbuf += sprintf(outbuf, "%.1lf", dd->lasthum);
          }
        }
      } else if ((*pos == 'V') || (*pos == 'v')) { /* Voltage */
        if ((dd->lastseen + 180) < time(NULL)) { /* Stale data / no data yet */
          outbuf += sprintf(outbuf, "%s", "N/A");
        } else {
          outbuf += sprintf(outbuf, "%4.2lf", dd->lastvoltage);
        }
      } else if (*pos == 'L') { /* Last seen */
        outbuf += sprintf(outbuf, "%u", (unsigned int)dd->lastseen);
      } else if (*pos == 'r') { /* carriage return */
        *outbuf = '\r';
        outbuf++;
      } else if (*pos == 'n') { /* linefeed / Newline */
        *outbuf = '\n';
        outbuf++;
      } else if (*pos == '%') { /* literal percent sign */
        *outbuf = '%';
        outbuf++;
      } else if (*pos == 0) {
        *outbuf = 0;
        return;
      }
      pos++;
    } else {
      *outbuf = *pos;
      outbuf++;
      pos++;
    }
  }
  *outbuf = 0;
}

static void dotryrestart(struct daemondata * dd, char ** argv, int serialfd) {
  struct daemondata * curdd = dd;

  if (!restartonerror) {
    exit(1);
  }
  /* close all open sockets */
  close(serialfd);
  while (curdd != NULL) {
    close(curdd->fd);
    curdd = curdd->next;
  }
  fprintf(stderr, "Will try to restart in %d second(s)...\n", restartonerror);
  sleep(restartonerror);
  execv(argv[0], argv);
  exit(1); /* This should never be reached, but just to be sure in case the exec fails... */
}

#define LLSIZE 1000
static void parseserialline(unsigned char * lastline, struct daemondata * dd) {
  unsigned char isok[LLSIZE];
  unsigned char rtype[LLSIZE];
  unsigned int sid;
  unsigned char stype = 0;
  unsigned int parsed[7];
  int ret;
  struct daemondata * curdd;
  double newtemp, newhum, newvolt;
  
  /* OK CC 7 23 144 34 53 133         hawotempdev2016 length=8 */
  /* OK CC 8 247 98 194 159 169 198   foxtempdev2016  length=9 */
  /* OK 9 9 1 4 194 32                lacrosse        length=7 */
  ret = sscanf(lastline, "%s %s %u %u %u %u %u %u %u %u",
                         &isok[0], &rtype[0], &sid, &parsed[0], &parsed[1], &parsed[2],
                         &parsed[3], &parsed[4], &parsed[5], &parsed[6]);
  if ((ret != 7) && (ret != 8) && (ret != 9)) return;
  if (strcmp(isok, "OK")) return;
  if ((strcmp(rtype, "CC") == 0) && (ret == 8)) { /* hawotempdev2016 */
    stype = 'H';
    newtemp = ((165.0 / 16383.0) * (double)((parsed[0] << 8) | parsed[1])) - 40.0;
    newhum = (100.0 / 16383.0) * (double)((parsed[2] << 8) | parsed[3]);
    newvolt = 3.0 * (parsed[4] / 255.0);
    VERBPRINT(1, "Received data from H-sensor %u: t=%.2lf h=%.2lf v=%.2lf\n",
                 sid, newtemp, newhum, newvolt);
  } else if ((strcmp(rtype, "CC") == 0) && (ret == 9)) { /* foxtempdev2016 */
    if (parsed[0] != 0xf7)  return; /* 'subtype' is not foxtemp (0xf7) */
    stype = 'F';
    newtemp = (-45.00 + 175.0 *((double)((parsed[1] << 8) | parsed[2]) / 65535.0));
    newhum = (100.0 * ((double)((parsed[3] << 8) | parsed[4]) / 65535.0));
    newvolt = (3.3 * parsed[5]) / 255.0;
    VERBPRINT(1, "Received data from F-sensor %u: t=%.2lf h=%.2lf v=%.2lf\n",
                 sid, newtemp, newhum, newvolt);
  } else if ((strcmp(rtype, "9") == 0) && (ret == 7)) { /* cheap lacrosse */
    stype = 'L';
    newtemp = ((double)((parsed[1] << 8) | parsed[2]) - 1000.0) / 10.0;
    newhum = (double)(parsed[3] & 0x7f);
    if ((parsed[3] & 0x80)) { /* There is no real voltage measurement available */
      newvolt = 1.0;          /* just a weak battery flag. We take a weak */
    } else {                  /* battery as having 1.0 volt and everything else */
      newvolt = 2.5;          /* as having 2.5 volt. */
    }
    VERBPRINT(1, "Received data from L-sensor %u: t=%.2lf h=%.2lf v=%.2lf\n",
                 sid, newtemp, newhum, newvolt);
  } else {
    return; /* Not a known/supported sensor */
  }
  curdd = dd;
  while (curdd != NULL) {
    if ((curdd->sensortype == stype)
     && (curdd->sensorid == sid)) { /* This sensor type+ID is requested */
      curdd->lastseen = time(NULL);
      curdd->lasttemp = newtemp;
      curdd->lasthum = newhum;
      curdd->lastvoltage = newvolt;
    }
    curdd = curdd->next;
  }
}

static int processserialdata(int serialfd, struct daemondata * dd, char ** argv) {
  static unsigned char lastline[LLSIZE];
  static unsigned int llpos = 0;
  unsigned char buf[100];
  int ret; int i;

  ret = read(serialfd, buf, sizeof(buf));
  if (ret < 0) {
    fprintf(stderr, "unexpected ERROR reading serial input: %s\n", strerror(errno));
    dotryrestart(dd, argv, serialfd);
  }
  for (i = 0; i < ret; i++) {
    if ((buf[i] == '\n') || (buf[i] == '\r')
     || (buf[i] == 0) || (llpos >= (LLSIZE - 10))) { /* Line complete. process it. */
      if (llpos > 0) {
        lastline[llpos] = 0;
        VERBPRINT(2, "Received on serial: %s\n", lastline);
        parseserialline(lastline, dd);
        llpos = 0;
      }
    } else {
      lastline[llpos] = buf[i];
      llpos++;
    }
  }
  return ret;
}

static void dodaemon(int serialfd, struct daemondata * dd, char ** argv) {
  fd_set mylsocks;
  struct daemondata * curdd;
  struct timeval to;
  int maxfd;
  int readysocks;
  time_t lastdatarecv;

  lastdatarecv = time(NULL);
  while (1) {
    curdd = dd; /* Start from beginning */
    maxfd = 0;
    FD_ZERO(&mylsocks);
    while (curdd != NULL) {
      FD_SET(curdd->fd, &mylsocks);
      if (curdd->fd > maxfd) { maxfd = curdd->fd; }
      curdd = curdd->next;
    }
    FD_SET(serialfd, &mylsocks);
    if (serialfd > maxfd) { maxfd = serialfd; }
    to.tv_sec = 60; to.tv_usec = 1;
    if ((readysocks = select((maxfd + 1), &mylsocks, NULL, NULL, &to)) < 0) { /* Error?! */
      if (errno != EINTR) {
        perror("ERROR: error on select()");
        dotryrestart(dd, argv, serialfd);
      }
    } else {
      if (FD_ISSET(serialfd, &mylsocks)) {
        if (processserialdata(serialfd, dd, argv) > 0) {
          lastdatarecv = time(NULL);
        }
      }
      curdd = dd;
      while (curdd != NULL) {
        if (FD_ISSET(curdd->fd, &mylsocks)) {
          int tmpfd;
          struct sockaddr_in6 srcad;
          socklen_t adrlen = sizeof(srcad);
          tmpfd = accept(curdd->fd, (struct sockaddr *)&srcad, &adrlen);
          if (tmpfd < 0) {
            perror("WARNING: Failed to accept() connection");
          } else {
            char outbuf[250];
            printtooutbuf(outbuf, strlen(outbuf), curdd);
            logaccess((struct sockaddr *)&srcad, adrlen, outbuf);
            /* The write might file if the client already disconnected, but
             * there is nothing we can do anyways and the connection is closed
             * immediately afterwards - so remove the gcc -Wunused-result warning.
             * Note that the gcc devs are braindead assholes that like to force
             * you to jump through hoops, thus simply casting to void does NOT
             * work in gcc as it does in almost every other compiler. */
            int gccdevsareassholes __attribute__((unused));
            gccdevsareassholes = write(tmpfd, outbuf, strlen(outbuf));
            close(tmpfd);
          }
        }
        curdd = curdd->next;
      }
    }
    if (restartonerror) {
      /* Did we receive something on the serial port recently? */
      if ((time(NULL) - lastdatarecv) > 300) {
        fprintf(stderr, "%s\n", "Timeout: No data from serial port for 5 minutes.");
        dotryrestart(dd, argv, serialfd);
      }
    }
  }
  /* never reached */
}


int main(int argc, char ** argv)
{
  int curarg;
  int serialfd;
  int forcebitrate = 0;

  for (curarg = 1; curarg < argc; curarg++) {
    if        (strcmp(argv[curarg], "-v") == 0) {
      verblev++;
    } else if (strcmp(argv[curarg], "-q") == 0) {
      verblev--;
    } else if (strcmp(argv[curarg], "-f") == 0) {
      runinforeground = 1;
    } else if (strcmp(argv[curarg], "-h") == 0) {
      usage(argv[0]); exit(0);
    } else if (strcmp(argv[curarg], "--help") == 0) {
      usage(argv[0]); exit(0);
    } else if (strcmp(argv[curarg], "--restartonerror") == 0) {
      restartonerror += 5;
    } else if (strcmp(argv[curarg], "-d") == 0) {
      curarg++;
      if (curarg >= argc) {
        fprintf(stderr, "ERROR: -d requires a parameter!\n");
        usage(argv[0]); exit(1);
      }
      serialport = strdup(argv[curarg]);
    } else if (strcmp(argv[curarg], "-r") == 0) {
      curarg++;
      if (curarg >= argc) {
        fprintf(stderr, "ERROR: -r requires a parameter!\n");
        usage(argv[0]); exit(1);
      }
      forcebitrate = strtol(argv[curarg], NULL, 10);
      if (forcebitrate == 1) { forcebitrate = 9579; }
      if (forcebitrate == 2) { forcebitrate = 17241; }
    } else {
      /* Unknown - must be the command. */
      break;
    }
  }
  if (curarg == argc) {
    fprintf(stderr, "ERROR: No command given!\n");
    usage(argv[0]);
    exit(1);
  }
  serialfd = open(serialport, O_NOCTTY | O_NONBLOCK | O_RDWR);
  if (serialfd < 0) {
    fprintf(stderr, "ERROR: Could not open serial port %s (%s).\n", serialport, strerror(errno));
    exit(1);
  }
  if (strcmp(argv[curarg], "daemon") == 0) { /* Daemon mode */
    struct daemondata * mydaemondata = NULL;
    int havefastsensors = 0;
    curarg++;
    do {
      int l; int optval;
      struct daemondata * newdd;
      struct sockaddr_in6 soa;
      unsigned char sensorid[1000];

      if (curarg >= argc) continue;
      newdd = calloc(sizeof(struct daemondata), 1);
      newdd->next = mydaemondata;
      mydaemondata = newdd;
      l = sscanf(argv[curarg], "%999[^:]:%u:%999[^\n]",
                 sensorid, &mydaemondata->port, &mydaemondata->outputformat[0]);
      if (l < 2) {
        fprintf(stderr, "ERROR: failed to parse daemon command parameter '%s'\n", argv[curarg]);
        exit(1);
      }
      if (l == 2) {
        strcpy((char *)&mydaemondata->outputformat[0], "%S %T");
      }
      if ((sensorid[0] >= (unsigned char)'0') && (sensorid[0] <= (unsigned char)'9')) {
        /* JUST a number. This is easy. */
        mydaemondata->sensortype = (unsigned char)'H';
        mydaemondata->sensorid = strtoul(sensorid, NULL, 0);
      } else { /* type+ID - this needs to be a known type */
        switch (sensorid[0]) {
        case 'F':
        case 'f':
        case 'L':
        case 'l': /* these often use the faster data rate */
                  havefastsensors = 1;
                  /* no break here, fall through! */
        case 'H':
        case 'h':
                  mydaemondata->sensortype = toupper(sensorid[0]);
                  break;
        default:
                  fprintf(stderr, "ERROR: Unknown sensortype selected in daemon parameter '%s'.\n", argv[curarg]);
                  exit(1);
        };
        mydaemondata->sensorid = strtoul(&sensorid[1], NULL, 0);
      }
      /* Open the port */
      mydaemondata->fd = socket(PF_INET6, SOCK_STREAM, 0);
      soa.sin6_family = AF_INET6;
      soa.sin6_addr = in6addr_any;
      soa.sin6_port = htons(mydaemondata->port);
      optval = 1;
      if (setsockopt(mydaemondata->fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval))) {
        VERBPRINT(0, "WARNING: failed to setsockopt REUSEADDR: %s", strerror(errno));
      }
#ifdef BRAINDEADOS
      /* For braindead operating systems in default config (BSD, Windows,
       * newer Debian), we need to tell the OS that we're actually fine with
       * accepting V4 mapped addresses as well. Because apparently for
       * braindead idiots accepting only selected addresses is a more default
       * case than accepting everything. */
      optval = 0;
      if (setsockopt(mydaemondata->fd, IPPROTO_IPV6, IPV6_V6ONLY, &optval, sizeof(optval))) {
        VERBPRINT(0, "WARNING: failed to setsockopt IPV6_V6ONLY: %s", strerror(errno));
      }
#endif
      if (bind(mydaemondata->fd, (struct sockaddr *)&soa, sizeof(soa)) < 0) {
        perror("Bind failed");
        printf("Could not bind to port %u\n", mydaemondata->port);
        exit(1);
      }
      if (listen(mydaemondata->fd, 20) < 0) { /* Large Queue as we might block for some time while reading */
        perror("Listen failed");
        exit(1);
      }
      curarg++;
    } while (curarg < argc);
    if (mydaemondata == NULL) {
      fprintf(stderr, "ERROR: the daemon command requires parameters.\n");
      exit(1);
    }
    {
      /* configure serial port parameters */
      struct termios tio;
      char jlinitstr[500];
      strcpy(jlinitstr, "0a "); /* Turn off that annoying ultrabright blue LED */
      if (forcebitrate == 0) {
        if (havefastsensors) { /* do we have at least 1 sensor that could use the faster rate of 17241? */
          strcat(jlinitstr, "30t "); /* Set to automatically switch data rate every 30 seconds */
        } else {
          strcat(jlinitstr, "1r "); /* Fixed slow rate of 9579 */
        }
      } else if (forcebitrate < 0) {
        strcat(jlinitstr, "30t "); /* Set to automatically switch data rate every 30 seconds */
      } else if (forcebitrate == 9579) {
        strcat(jlinitstr, "1r "); /* Fixed slow rate of 9579 */
      } else if (forcebitrate == 17241) {
        strcat(jlinitstr, "0r "); /* Fixed fast rate of 17241 */
      } else {
        fprintf(stderr, "WARNING: Don't know how to do a bitrate of %d, ignoring bitrate setting!\n", forcebitrate);
      }
      strcat(jlinitstr, "?");
      tcgetattr(serialfd, &tio);
      cfsetspeed(&tio, B57600);
      tio.c_lflag &= ~(ICANON | ECHO); /* Clear ICANON and ECHO. */
      tio.c_iflag &= ~(IXON | IGNBRK); /* no flow control */
      tio.c_cflag &= ~(CSTOPB); /* just one stop bit */
      tcsetattr(serialfd, TCSAFLUSH, &tio);
      /* Now give the jlink some time to reboot, then send the init string */
      sleep(2);
      write(serialfd, jlinitstr, strlen(jlinitstr));
    }
    /* the good old doublefork trick from 'systemprogrammierung 1' */
    if (runinforeground != 1) {
      int ourpid;
      VERBPRINT(2, "launching into the background...\n");
      ourpid = fork();
      if (ourpid < 0) {
        perror("Ooops, fork() #1 failed");
        exit(1);
      }
      if (ourpid == 0) { /* We're the child */
        ourpid = fork(); /* fork again */
        if (ourpid < 0) {
          perror("Ooooups. fork() #2 failed");
          exit(1);
        }
        if (ourpid == 0) { /* Child again */
          /* Just don't exit, we'll continue below. */
        } else { /* Parent */
          exit(0); /* Just exit */
        }
      } else { /* Parent */
        exit(0); /* Just exit */
      }
    }
    {
      struct sigaction sia;
      sia.sa_handler = sigpipehandler;
      sigemptyset(&sia.sa_mask); /* If we don't do this, we're likely */
      sia.sa_flags = 0;          /* to die from 'broken pipe'! */
      sigaction(SIGPIPE, &sia, NULL);
    }
    dodaemon(serialfd, mydaemondata, argv);
  } else {
    fprintf(stderr, "ERROR: Command '%s' is unknown.\n", argv[curarg]);
    usage(argv[0]);
    exit(1);
  }
  return 0;
}

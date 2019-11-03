#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
#include <dirent.h>
#include <stdlib.h>
#include <sys/types.h>
#include <fcntl.h>
#include <string.h>

#include "logtools.h"

void usage()
{
  fprintf(stderr, "Usage: logprn logfile idle-time[:max-wait] command\n"
  "\nVersion: " VERSION "\n");
  exit(ERR_PARAM);
}

int main(int argc, char **argv)
{
  if(argc != 4)
    usage();

  struct stat sBuf;
  int rc = stat(argv[1], &sBuf);
  if(rc)
  {
    printf("Can't stat \"%s\".\n", argv[1]);
    return ERR_PARAM;
  }

  bool changed = false;
  time_t mtime = sBuf.st_mtime;
  ino_t inode = sBuf.st_ino;
  off_t size = sBuf.st_size;
  off_t print_size = size;

  char *pbuf = strdup(argv[2]);
  strtok(pbuf, ":");
  char *maxBuf = strtok(NULL, ":");
  time_t delay = atoi(pbuf);
  time_t maxWait = 0;
  if(maxBuf)
    maxWait = atoi(maxBuf);
  free(pbuf);
  if(maxWait && maxWait < delay)
    usage();

  if(delay < 1)
    usage();
  time_t last_change = time(NULL);
  time_t first_unwritten_change = 0;

  while(1)
  {
    sleep(1);
    rc = stat(argv[1], &sBuf);
    if(rc)
    {
      // if failed then try again 1 second later in case of link changes etc.
      sleep(1);
      rc = stat(argv[1], &sBuf);
    }
    if(rc)
    {
      fprintf(stderr, "File disappeared or became unreadable.\n");
      return ERR_INPUT;
    }
    if(inode != sBuf.st_ino)
    {
      inode = sBuf.st_ino;
      changed = true;
      mtime = sBuf.st_mtime;
      print_size = 0;
      size = sBuf.st_size;
      last_change = time(NULL);
    }
    else if(mtime != sBuf.st_mtime || size != sBuf.st_size)
    {
      if(size > sBuf.st_size)
        print_size = 0;
      size = sBuf.st_size;
      mtime = sBuf.st_mtime;
      changed = true;
      last_change = time(NULL);
      if(first_unwritten_change == 0)
        first_unwritten_change = last_change;
    }
    time_t now = time(NULL);
    if(changed)
    {
      if((now - last_change) > delay
       || (maxWait && (now - first_unwritten_change ) > maxWait) )
      {
        int fd = open(argv[1], O_RDONLY);
        if(fd == -1)
        {
          fprintf(stderr, "Can't open file \"%s\"\n", argv[1]);
          return ERR_INPUT;
        }
        rc = lseek(fd, print_size, SEEK_SET);
        if(rc == -1)
        {
          fprintf(stderr, "Can't lseek().\n");
        }
        FILE *fp = popen(argv[3], "w");
        if(!fp)
        {
          fprintf(stderr, "Can't run \"%s\"\n", argv[3]);
          return ERR_OUTPUT;
        }
        char buf[4096];
        while( (rc = read(fd, buf, sizeof(buf))) > 0)
        {
          if(int(fwrite(buf, 1, rc, fp)) != rc)
          {
            fprintf(stderr, "Short write to pipe.\n");
            break;
          }
          print_size += rc;
        }

        pclose(fp);
        close(fd);
        changed = false;
        first_unwritten_change = 0;
      }
    }
  }
  return 0; // to make gcc happy
}


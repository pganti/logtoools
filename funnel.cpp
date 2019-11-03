#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>

#include "logtools.h"

void usage()
{
  fprintf(stderr, "Usage: funnel [|] [>[>]file] [|program]\n"
  "\nVersion: " VERSION "\n");
  exit(ERR_PARAM);
}

PFILE openit(const char *arg)
{
  const char *ptr = arg;
  PFILE ret = NULL;

  if(!strcmp(arg, "|"))
    return stdout;

  if(ptr[0] == '>')
  {
    const char *mode = "w";
    ptr++;
    if(ptr[0] == '>')
    {
      ptr++;
      mode = "a";
    }
    ret = fopen(ptr, mode);
  }
  else if(ptr[0] == '|')
  {
    ptr++;
    ret = popen(ptr, "w");
  }
  else
  {
    fprintf(stderr, "Bad command \"%s\".\n", arg);
    return NULL;
  }
  return ret;
}

int main(int argc, char **argv)
{
  if(argc < 2)
    usage();

  PFILE *out = new PFILE[argc];

  int i;
  for(i = 1; i < argc; i++)
  {
    out[i] = openit(argv[i]);
  }
  char buf[8192];
  size_t rc;
  int count = argc - 1;
  int errors = 0;
  int f = fcntl(0, F_GETFL);
  if(f == -1 || -1 == fcntl(0, F_SETFL, f & (!O_NONBLOCK)) )
  {
    fprintf(stderr, "Can't fcntl\n");
    return ERR_INPUT;
  }

  while( int(rc = read(0, buf, sizeof(buf)) ) > 0 && count > 0)
  {
    if(rc)
    {
      for(i = 1; i < argc; i++)
      {
        if(out[i])
        {
          size_t wrote = fwrite(buf, rc, 1, out[i]);
          if(wrote != 1)
          {
            fclose(out[i]);
            out[i] = NULL;
            fprintf(stderr, "Output \"%s\" aborted.\n", argv[i]);
            count--;
            errors++;
          }
        }
      }
      fflush(NULL);
    }
  }
  close(0);
  for(i = 1; i < argc; i++)
  {
    if(out[i])
      fclose(out[i]);
  }
  return 100 + errors;
}


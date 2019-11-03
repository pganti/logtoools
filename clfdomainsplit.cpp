#include <stdio.h>

#if (__GNUC__ >= 3)
#include <ext/hash_map>
#else
#include <hash_map>
#endif

#include <unistd.h>
#include <cstring>
#include <vector>
#include <ctype.h>
#include "logtools.h"

using namespace std;
using namespace __gnu_cxx;

// MAX_FDS is the maximum number of files that will be directly written to
// by one process
#define MAX_FDS  900

// NUM_CHILDREN is the number of child processes that each process may have
// Note that each child may also have that many children.  The limit is the
// size of the process table in your kernel...
#define NUM_CHILDREN  100

// Get the "domain" component of the line.  Change this if you want to split
// on other parts of the name (EG split files on the directory names).  Make
// sure that the value assigned to dom is a valid file name.
// Change this if you want to use this program for splitting files other than
// web logs.
bool getDomain(string &dom, PCCHAR buf)
{
  if(!(buf = index(buf, '[')) ) return true;
  if(!(buf = index(buf, ']')) ) return true;
  if(!(buf = index(buf, '"')) ) return true;
  if(!(buf = strstr(buf, "http://")) ) return true;
  buf += 7;

  PCCHAR end = index(buf, '/');
  if(!end) return true;
  PCCHAR tmp_end = index(buf, ':'); //search for ":80/" at the end and ignore it
  if(tmp_end && tmp_end < end)
    end = tmp_end;
  int len = end - buf;
  PCHAR tmp = new char[len];
  memcpy(tmp, buf, len);
  for(int i = 0; i < len; i++)
    tmp[i] = tolower(tmp[i]);
  dom = string(tmp, int(len));
  free(tmp);
  return false;
}

// The data structure for configuration.  If you change the getDomain() and
// canonDom() functions then you'll probably need to change this and the
// parseConfig() function.
typedef struct
{
  string domain;
  int significant_parts;
} CFG_TYPE;
vector<CFG_TYPE> config;

// Get the canonical name
bool canonDom(string &canon, string &dom)
{
  int last = dom.length() - 1;
  canon = dom;
  if(canon[last] == '.')
  {
    canon.erase(last);
    last--; // for names that end in '.'
  }

  if(isdigit(canon[last]))
    return true; // canon == dom for numeric ID, and it's not valid for split

  int significant_parts = 2;
  unsigned int i;
  for(i = 0; i < config.size(); i++)
  {
    size_t pos = dom.rfind(config[i].domain);
    if(int(pos) != -1 && (pos + config[i].domain.length()) == dom.length())
    {
      significant_parts = config[i].significant_parts;
      break;
    }
  }
  for(i = last; i > 0; i--)
  {
    if(dom[i] == '.')
    {
      significant_parts--;
      if(!significant_parts)
      {
        canon.assign(dom, i + 1, dom.length() - i - 1);
        break;
      }
    }
  }
  return false;
}

void usage();

// This function parses the config file.
bool parseConfig(CPCCHAR configFile)
{
  FILE *fp = fopen(configFile, "r");
  if(!fp)
  {
    fprintf(stderr, "Can't open \"%s\".\n", configFile);
    usage();
  }
  char buf[1024];
  CFG_TYPE cfg;
  while(fgets(buf, sizeof(buf), fp))
  {
    buf[sizeof(buf) - 1] = '\0';
    strtok(buf, "\r\n");
    if(strlen(buf) && buf[0] != '#')
    {
      cfg.significant_parts = atoi(buf);
      if(cfg.significant_parts <= 0 || cfg.significant_parts > 10)
      {
        fprintf(stderr, "Error in config line \"%s\".\n", buf);
        return true;
      }
      char *ptr = index(buf, ':');
      ptr++;
      if(*ptr == '\0')
      {
        fprintf(stderr, "Error in config line \"%s\".\n", buf);
        return true;
      }
      cfg.domain = string(ptr);
      config.push_back(cfg);
    }
  }
  fclose(fp);
  return false;
}


FILE **children[NUM_CHILDREN];
int num_fds = 0;
int newTarget = -1; // -1 means handle locally, anything else means the
                    // current child process target
FILE *input = stdin;
PFILE *defFilePtr;
int num_children = 0;



struct eqstr
{
  bool operator()(string s1, string s2) const
  {
    return s1 == s2;
  }
};

struct hash_string
{
  size_t operator()(const string str) const { return hash<const char *>()(str.c_str()); }
};

typedef hash_map<const string, FILE **, hash_string, eqstr> HASH_TYPE;
HASH_TYPE fd_map;

typedef FILE * PFILE;

void usage()
{
  fprintf(stderr, "usage: clfdomainsplit [filenames]\n"
  "This program splits CLF format web logs into multiple files, one for each\n"
  "domain in the input.\n"
  "[-i input] [-d defaultfile] [-c cfg-file] [-o directory]\n"
  "\nVersion: " VERSION "\n");
  exit(ERR_PARAM);
}

FILE **forkit(string &dom);
bool printLine(string &dom, PCCHAR buf);
void closeit();
bool newdom(string &dom);
FILE **openOutput(string &dom);

int main(int argc, char **argv)
{
  for(int i = 0; i < NUM_CHILDREN; i++)
  {
    children[i] = NULL;
  }
  PCCHAR configFile = "/etc/clfdomainsplit.cfg";
  FILE *defFile = stderr;

  int int_c;
  optind = 0;
  while(-1 != (int_c = getopt(argc, argv, "c:d:i:o:")) )
  {
    switch(char(int_c))
    {
      case '?':
      case ':':
        usage();
      break;
      case 'c':
        configFile = optarg;
      break;
      case 'd':
        defFile = fopen(optarg, "a");
        if(!defFile)
        {
          fprintf(stderr, "Can't open \"%s\".\n", optarg);
          usage();
        }
      break;
      case 'i':
        input = fopen(optarg, "r");
        if(!input)
        {
          fprintf(stderr, "Can't open \"%s\".\n", optarg);
          usage();
        }
      break;
      case 'o':
        if(chdir(optarg))
        {
          fprintf(stderr, "Can't chdir() to \"%s\".\n", optarg);
          usage();
        }
      break;
    }
  }
  if(parseConfig(configFile))
    return ERR_PARAM;

  defFilePtr = new PFILE;
  *defFilePtr = defFile;

  char buf[4096];
  while(fgets(buf, sizeof(buf), input))
  {
    buf[sizeof(buf) - 1] = '\0';
    strtok(buf, "\r\n");
    string dom;
    if(getDomain(dom, buf))
    {
      fprintf(stderr, "bad line:%s\n", buf);
    }
    else
    {
      if(printLine(dom, buf))
      {
        closeit();
        return 1;
      }
    }
  }
  closeit();
  return 0;
}

bool printLine(string &dom, PCCHAR buf)
{
  if(!fd_map[dom])
  {
    if(newdom(dom))
      return true;
  }
  fprintf(*(fd_map[dom]), "%s\n", buf);
  return false;
}

bool newdom(string &dom)
{
  string canon;
  if(canonDom(canon, dom))
  {
    // send this and anything like it to the default output
    // beware - a file of totally random junk could run us out of RAM!!!
    fd_map[canon] = defFilePtr;
    fd_map[dom] = defFilePtr;
    return false;
  }
  if(fd_map[canon])
  {
    fd_map[dom] = fd_map[canon];
    return false;
  }
  FILE **fpp = openOutput(canon);
  if(!fpp)
    return true;
  fd_map[dom] = fpp;
  return false;
}

FILE **openOutput(string &dom)
{
  if(num_fds > MAX_FDS)
  {
    newTarget++;
    if(newTarget > NUM_CHILDREN)
      newTarget = 0;
  }
  if(newTarget != -1)
  {
    if(children[newTarget] == NULL)
    {
      return forkit(dom); // NB forkit() can call us
    }
    else
    {
      fd_map[dom] = children[newTarget];
      return children[newTarget];
    }
  }

  FILE *fp = fopen(dom.c_str(), "a");
  if(!fp)
  {
    fprintf(stderr, "Can't open/create file \"%s\".\n", dom.c_str());
    return NULL;
  }
  num_fds++;
  FILE **val = new PFILE;
  *val = fp;
  fd_map[dom] = val;
  return val;
}

FILE **forkit(string &dom)
{
  int filedes[2], childFiledes[2];
  if(pipe(filedes) || pipe(childFiledes))
  {
    fprintf(stderr, "Can't create pipe!\n");
    return NULL;
  }
  fflush(NULL);
  int rc = fork();
  if(rc == -1)
  {
    fprintf(stderr, "Can't fork!\n");
    return NULL;
  }
  char c;
  if(rc == 0)
  { // child
    closeit();
    num_fds = 0;
    newTarget = -1;
    num_children = 0;
    for(int i = 0; i < NUM_CHILDREN; i++)
    {
      children[i] = NULL;
    }
    close(childFiledes[0]);
    write(childFiledes[1], &c, 1);
    close(childFiledes[1]);
    close(filedes[1]);
    input = fdopen(filedes[0], "r");
    if(!input)
    {
      fprintf(stderr, "Can't fdopen!\n");
      close(filedes[0]);
      exit(1);
    }
    return openOutput(dom);
  }
  else
  { // parent
    num_children++;
    close(filedes[0]);
    close(childFiledes[1]);
    read(childFiledes[0], &c, 1);
    close(childFiledes[0]);
    FILE *fp = fdopen(filedes[1], "w");
    if(!fp)
    {
      fprintf(stderr, "Can't fdopen!\n");
      return NULL;
    }
    FILE **fpp = new PFILE;
    *fpp = fp;
    children[newTarget] = fpp;
    fd_map[dom] = children[newTarget];
    return children[newTarget];
  }
  return NULL; // should never reach here
}

void closeit()
{
  while(!fd_map.empty())
  {
    HASH_TYPE::iterator st = fd_map.begin();
    FILE **fpp = (*st).second;
    if(fpp)
    {
      if(*fpp)
      {
        fclose(*fpp);
      }
      *fpp = NULL;
    }
    fd_map.erase(st);
  }
}

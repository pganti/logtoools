#include <cstdlib>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <map>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "logtools.h"

using namespace std;

void usage()
{
  fprintf(stderr
        , "Usage: clfsplit -d defaultfile -f file -s spec [-f file -s spec]\n"
          "\nSpec is a list of IP ranges or a file containing such a list.\n"
  "\nVersion: " VERSION "\n");
  exit(ERR_PARAM);
}

class file_wrap
{
public:
  file_wrap(FILE *fp, const char *name)
   : m_fp(fp)
   , m_name(name)
  { }


  void close() { if(m_fp) { fclose(m_fp); m_fp = NULL; } }

  void write(const char *line)
  {
    if(EOF == fputs(line, m_fp))
    {
      fprintf(stderr, "Can't write to output file %s\n", m_name);
      exit(ERR_OUTPUT);
    }
  }

private:
  FILE *m_fp;
  const char *m_name;

  file_wrap(const file_wrap&);
  file_wrap & operator=(const file_wrap&);
};

class details;
typedef map<int, details *> MAP;

const char *inet_itoa(unsigned int ip)
{
  static char buf[30];
  sprintf(buf, "%d.%d.%d.%d", ip >> 24, (ip >> 16) % 256, (ip >> 8) % 256, ip % 256);
  return buf;
}

class details
{
public:
  details(unsigned int begin, unsigned int end, file_wrap *wrap, MAP &m)
   : m_begin(begin)
   , m_end(end)
   , m_wrap(wrap)
  {
    m[begin] = this;
  }

  ~details() { m_wrap->close(); }

  bool check_item(unsigned int ip, const char *line)
  {
    if(ip >= m_begin && ip <= m_end)
    {
      m_wrap->write(line);
      return true;
    }
    return false;
  }

private:
  unsigned int m_begin, m_end;
  file_wrap *m_wrap;

  details(const details&);
  details & operator=(const details&);
};


unsigned int inet_atoi(const char *n)
{
  char tmp[16];
  strncpy(tmp, n, sizeof(tmp) - 1);
  tmp[sizeof(tmp) - 1] = '\0';
  for(unsigned int i = 0; i < sizeof(tmp); i++)
  {
    if(tmp[i] == ' ')
      tmp[i] = '\0';
  }
  return ntohl(inet_addr(tmp));
}

FILE *open_output_file(const char *file)
{
  if(!file)
    usage();

  const char *mode = "w";
  struct stat stat_buf;
  if(!stat(file, &stat_buf) && S_ISREG(stat_buf.st_mode))
    mode = "a";
  FILE *output = fopen(file, mode);
  if(!output)
    fprintf(stderr, "Can't open file %s\n", file);
  return output;
}

void process_spec(MAP &m, const char *file, const char *opt)
{
  FILE *output = open_output_file(file);
  if(!output)
    exit(ERR_OUTPUT);
  file_wrap *wrap = new file_wrap(output, file);

  FILE *fp = NULL;
  char buf[4096];
  char *ptr = buf;
  if(opt[0] < '0' || opt[0] > '9')
  {
    fp = fopen(opt, "r");
    if(!fp)
    {
      fprintf(stderr, "Can't open file %s\n", opt);
      exit(ERR_SPEC);
    }
    if(!fgets(buf, sizeof(buf), fp) )
    {
      fprintf(stderr, "Can't read from file %s\n", opt);
      exit(ERR_SPEC);
    }
  }
  else
  {
    ptr = strdup(opt);
  }
  do
  {
    buf[sizeof(buf) - 1] = '\0';

    char *item;
    item = strtok(ptr, "\r\n:");
    do
    {
      int i;
      char *it = strdup(item);
      for(i = 0; item[i] != '\0' && item[i] != '-' && item[i] != '/'; i++)
      { }

      unsigned int first_ip = 0;
      unsigned int second_ip = 0;
      switch(item[i])
      {
        case '\0':
          first_ip = inet_atoi(item);
          second_ip = first_ip;
        break;
        case '-':
          item[i] = '\0';
          first_ip = inet_atoi(item);
          second_ip = inet_atoi(&item[i + 1]);
        break;
        case '/':
        {
          item[i] = '\0';
          first_ip = inet_atoi(item);
          unsigned int sub = atoi(&item[i + 1]);
          second_ip = first_ip + (1 << (32 - sub)) - 1;
        }
        break;
      }
      new details(first_ip, second_ip, wrap, m);
      free(it);
    } while( (item = strtok(NULL, "\r\n:")) );

  } while(fp && fgets(buf, sizeof(buf), fp) );
  if(fp)
    fclose(fp);
  else
    free(ptr);
}


int main(int argc, char **argv)
{
  int int_c;
  const char *defaultfile = NULL;
  const char *file = NULL;
  MAP m;
  FILE *input = stdin;
  bool new_input = false;
  optind = 0;
  while(-1 != (int_c = getopt(argc, argv, "d:i:f:s:")) )
  {
    switch(char(int_c))
    {
      case '?':
      case ':':
        usage();
      break;
      case 'd':
        defaultfile = optarg;
      break;
      case 'i':
        if(new_input)
          usage();
        input = fopen(optarg, "r");
        if(!input)
        {
          fprintf(stderr, "Can't open file %s\n", optarg);
          return ERR_INPUT;
        }
        new_input = true;
      break;
      case 'f':
        file = optarg;
      break;
      case 's':
        process_spec(m, file, optarg);
      break;
    }
  }
  new details(0, 0, NULL, m);
  FILE *def = open_output_file(defaultfile);
  if(!def)
    return ERR_OUTPUT;
  char buf[4096];
  while(fgets(buf, sizeof(buf), input))
  {
    buf[sizeof(buf) - 1] = '\0';
    unsigned int ip = inet_atoi(buf);
    if(ip)
    {
      MAP::iterator i = m.upper_bound(ip);
      bool done = false;
      if(i != m.begin() )
      {
        i--;
        done = (*i).second->check_item(ip, buf);
      }
      if(!done)
      {
        if(EOF == fputs(buf, def))
        {
          fprintf(stderr, "Can't write to output file %s\n", defaultfile);
          return ERR_OUTPUT;
        }
      }
    }
  }
  if(new_input)
    fclose(input);
  return 0;
}


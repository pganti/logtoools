#include <stdio.h>
#include <ext/hash_map>
#include <stdlib.h>
#include <map>
#include <cstring>
#include <unistd.h>
#include <ctype.h>

#include "logtools.h"

#define BUF_SIZE 4096

using namespace std;
using namespace __gnu_cxx;

struct eqstr
{
  bool operator()(const char *s1, const char *s2) const
  { return strcmp(s1, s2) == 0; }
};

hash_map<const char *, const char *, hash<const char *>, eqstr> months;

class LogFile
{
public:
  LogFile();
  ~LogFile(){ if(m_fp) fclose(m_fp); }

  // open a file, return 0 for success, 1 for minor error (0 length file)
  // and 2 for serious error (file not found)
  int open(const char *name, bool domain_mangling);

  // get the date string of the current item
  const char *date() const { return m_date; }
  // get the full data for the current item
  const char *line() { m_valid = false; return m_line; }

  // get a line from the file and parse it
  // return 0 for success and 1 for EOF
  int getLine();

  bool valid() const { return m_valid; }

private:
  bool m_valid;
  FILE *m_fp;
  char m_date[17];
  char m_lineBuf[BUF_SIZE];
  char m_lineBuf2[BUF_SIZE + 7];
  char *m_line;
  bool m_web_first;

  // store the date in numeric format so that strcmp() can be used to compare
  // dates.  Returns 1 for error and 0 for OK.
  int setDate();

  LogFile(const LogFile&);
  LogFile & operator=(const LogFile&);
};

LogFile::LogFile()
 : m_valid(false)
 , m_fp(NULL)
{
  m_line = m_lineBuf;
  m_lineBuf[0] = '\0';
}

// Common log format:
// 1.2.3.4 - - [23/Aug/2000:12:00:32 +0200] etc
int LogFile::setDate()
{
  unsigned int i;
  // find '[' or abort if we can't
  for(i = 0; m_lineBuf[i] != '[' && m_lineBuf[i] != '\0'; i++)
  { }

  // if not enough data left for the full date then return
  if(i + 21 > strlen(m_lineBuf))
    return 1;
  memcpy(m_date, &m_lineBuf[i + 8], 4);
  char mon[4];
  memcpy(mon, &m_lineBuf[i + 4], 3);
  mon[3] = '\0';
  const char *m = months[mon];
  if(!m) return 1;
  strcpy(&m_date[4], m);
  memcpy(&m_date[6], &m_lineBuf[i + 1], 2);
  memcpy(&m_date[8], &m_lineBuf[i + 13], 8);
  m_date[16] = '\0';
  if(m_web_first) // make m_lineBuf2 have the data for the mangled line
  {
    unsigned int end_webname;
    // find where the domain name ends
    for(end_webname = 0; m_lineBuf[end_webname] != ' ' && m_lineBuf[end_webname] != '\0'; end_webname++)
    { }

    if(end_webname >= i)
      return 1;
    for(i = 0; i < end_webname; i++)
      m_lineBuf[end_webname] = tolower(m_lineBuf[end_webname]);

    // there will be more than 40 chars in between
    unsigned int start_url = end_webname + 40;
    // search for the start quote character
    for(; m_lineBuf[start_url] != '\"' && m_lineBuf[start_url] != '\0'; start_url++)
    { }
    // search for the space in the web request
    for(; m_lineBuf[start_url] != ' ' && m_lineBuf[start_url] != '\0'; start_url++)
    { }

    if(strlen(&m_lineBuf[start_url]) < 6) return 1;

    memcpy(m_lineBuf2, &m_lineBuf[end_webname + 1], start_url - end_webname);
    m_line = &m_lineBuf2[start_url - end_webname]; // m_line points to next char
    if(strncmp(&m_lineBuf[start_url + 1], "http://", 7))
    {
      strcpy(m_line, "http://");
      m_line += 7;
      memcpy(m_line, m_lineBuf, end_webname);
      m_line += end_webname;
      if(m_lineBuf[start_url + 1] != '/')
      {
        // if URL doesn't start with a '/' then we add one
        *m_line = '/';
        m_line++;
      }
    }
    strcpy(m_line, &m_lineBuf[start_url + 1]);
    m_line = m_lineBuf2;
  }
  return 0;
}

int LogFile::open(const char *name, bool domain_mangling)
{
  m_web_first = domain_mangling;
  m_fp = fopen(name, "r");
  if(!m_fp)
  {
    fprintf(stderr, "Can't open %s.\n", name);
    return 2;
  }
  if(getLine())
    return 1;
  return 0;
}

int LogFile::getLine()
{
  while(1)
  {
    // if can't get more data then return 1
    if(!fgets(m_lineBuf, sizeof(m_lineBuf) - 1, m_fp))
      return 1;
    m_lineBuf[sizeof(m_lineBuf) - 1] = '\0';
    m_line = m_lineBuf;
    strtok(m_line, "\n\r");
    // if setDate() returns 1 then we can't parse the line so we keep looping
    // if setDate() returns 0 then return success!
    if(!setDate())
    {
      m_valid = true;
      return 0;
    }
    fprintf(stderr, "Skipping bad line: %s\n", m_line);
  }
  return 0; // to make compilers happy - will not be reached
}

typedef LogFile *PLogFile;

int item_compare(const void *a, const void *b)
{
  const LogFile * const left = *(LogFile * const *)a;
  const LogFile * const right = *(LogFile * const *)b;
  return strcmp(left->date(), right->date());
}

struct ltstr
{
  bool operator()(const string s1, const string s2) const
  {
    return strcmp(s1.c_str(), s2.c_str()) < 0;
  }
};

void usage(const char *const arg)
{
  fprintf(stderr, "usage: %s [filenames]", arg);
  fprintf(stderr, "\n"
  "This program merges web logs in common log format into a single stream\n"
  "on standard output.  It reads from multiple input files and outputs the\n"
  "data in-order as much as is possible.  If there is only a single input\n"
  "file it will re-order it (with a 1000 line buffer size) to deal with web\n"
  "servers that output data out of order.\n"
  "\nVersion: " VERSION "\n");
  exit(ERR_PARAM);
}

int main(int argc, char **argv)
{
  if(argc == 1)
    return 0;

  unsigned int map_items = 0;
  bool set_map_items = false, domain_mangling = false;
  int int_c;
  optind = 0;
  while(-1 != (int_c = getopt(argc, argv, "b:hd")) )
  {
    switch(char(int_c))
    {
      case '?':
      case ':':
      case 'h':
        usage(argv[0]);
      break;
      case 'b':
        set_map_items = true;
        map_items = atoi(optarg);
      break;
      case 'd':
        domain_mangling = true;
      break;
    }
  }
  months["Jan"] = "01";
  months["Feb"] = "02";
  months["Mar"] = "03";
  months["Apr"] = "04";
  months["May"] = "05";
  months["Jun"] = "06";
  months["Jul"] = "07";
  months["Aug"] = "08";
  months["Sep"] = "09";
  months["Oct"] = "10";
  months["Nov"] = "11";
  months["Dec"] = "12";

  multimap<const string, const string, ltstr> outputMap;

  LogFile **items = new PLogFile[argc - optind];

  unsigned int item_count = 0;
  int i;
  for(i = optind; i < argc; i++)
  {
    items[item_count] = new LogFile;
    int rc = items[item_count]->open(argv[i], domain_mangling);
    // if rc==2 then file not found, if rc==1 then 0 length file
    if(rc > 1)
      return ERR_INPUT;
    if(rc == 1)
      delete items[item_count];
    else
      item_count++;
  }

  if(!set_map_items)
  {
    map_items = item_count * 400;
    if(map_items < 4000)
      map_items = 4000;
  }
  while(item_count > 1)
  {
    qsort(items, item_count, sizeof(LogFile *), item_compare);
    while(items[0]->valid() && strcmp(items[0]->date(), items[1]->date()) <= 0)
    {
      if(map_items > 0)
      {
        outputMap.insert(pair<string, string>(items[0]->date(), items[0]->line()));
        while(outputMap.size() > map_items)
        {
          printf("%s\n", outputMap.begin()->second.c_str());
          outputMap.erase(outputMap.begin());
        }
      }
      else
      {
        printf("%s\n", items[0]->line());
      }
      if(items[0]->getLine())
      {
        delete(items[0]);
        item_count--;
        items[0] = items[item_count];
        break;
      }
    }
  }
  if(item_count == 1)
  {
    if(map_items > 0)
    {
      do
      {
        outputMap.insert(pair<string, string>(items[0]->date(), items[0]->line()));
      } while(!items[0]->getLine() && outputMap.size() < map_items);

      if(items[0]->valid())
      {
        do
        {
          outputMap.insert(pair<string, string>(items[0]->date(), items[0]->line()));
          CPCCHAR tmp = outputMap.begin()->second.c_str();
          if(printf("%s\n", tmp) != int(strlen(tmp) + 1))
          {
            fprintf(stderr, "Can't write output!\n");
              return ERR_OUTPUT;
          }
          outputMap.erase(outputMap.begin());
        } while(!items[0]->getLine());
      }
      delete items[0];
      while(!outputMap.empty())
      {
        CPCCHAR tmp = outputMap.begin()->second.c_str();
        if(printf("%s\n", tmp) != int(strlen(tmp) + 1))
        {
          fprintf(stderr, "Can't write output!\n");
            return ERR_OUTPUT;
        }
        outputMap.erase(outputMap.begin());
      }
    }
    else
    {
      do
      {
        CPCCHAR tmp = items[0]->line();
        if(printf("%s\n", tmp) != int(strlen(tmp) + 1))
        {
          fprintf(stderr, "Can't write output!\n");
            return ERR_OUTPUT;
        }
      } while(!items[0]->getLine());
    }
  }
  delete items;
  return 0;
}

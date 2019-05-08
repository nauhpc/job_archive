
#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <math.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/inotify.h>
#include <limits.h>
using namespace std;

// some time functions
struct timeval getCurTime() {
    struct timeval curTime;
    gettimeofday(&curTime,NULL);
    return curTime;
}
// elapse time
double timeDiff(struct timeval before, struct timeval after) {
  return  ( 1000000.0*(after.tv_sec - before.tv_sec) + (after.tv_usec - before.tv_usec))/1000000.0;
}

// for logs datetimesamp
string getCurDateTimeMilliSec() {
  // return format: 2019-04-01 14:58:41.853260
  char buffer[30];
  int microsec;
  struct tm* tm_info;
  struct timeval tv;

  gettimeofday(&tv, NULL);

  microsec = lrint(tv.tv_usec);
  if (microsec>=1000000) { // Allow for rounding up to nearest second
    microsec -=1000000;
    tv.tv_sec++;
  }

  tm_info = localtime(&tv.tv_sec);
  strftime(buffer, 26, "%Y-%m-%d %H:%M:%S", tm_info);

  char result[30];
  sprintf(result, "%s.%06d", buffer, microsec);
  return string(result);
}
// example usage: from my timeElapse.cpp
//   cout << getCurDateTime1() << " begin" << endl;
//   struct timeval before = getCurTime();
//   usleep(1500000);
//   cout << getCurDateTime1() << " elapse: " << timeDiff(before, getCurTime()) << endl;

// new combined getCurYear and getCurMonth
struct getCurYearMonth {
    string curYear;
    string curMonth;
    // constructor
    getCurYearMonth() {
      time_t rawtime;
      time(&rawtime);
      struct tm* timeinfo = localtime(&rawtime);
      char buf[10];
      sprintf(buf, "%d", timeinfo->tm_year+1900);
      curYear = string(buf); 
      sprintf(buf, "%02d", timeinfo->tm_mon+1);
      curMonth = string(buf); 
    }
};

string getCurYear() {
  time_t rawtime;
  time(&rawtime);
  struct tm* timeinfo = localtime(&rawtime);
  
  char year[5];
  sprintf(year, "%d", timeinfo->tm_year+1900);
  
  return string(year);
}

string getCurMonth() {
  time_t rawtime;
  time(&rawtime);
  struct tm* timeinfo = localtime(&rawtime);

  char month[3];
  sprintf(month, "%02d", timeinfo->tm_mon+1);

  return string(month);
}

inline bool doesFileExist(const std::string& name) {
  struct stat buffer;   
  return (stat (name.c_str(), &buffer) == 0); 
}

inline bool doesDirExist(const string dirName) {
    DIR *dir;
    class dirent *ent;
    class stat st;

    if ((stat(dirName.c_str(), &st) != 0) || (! S_ISDIR(st.st_mode))) {
        return false;
    }
    return true;
}

void mkDirectory(const string dirName) {
    const int dir_err = mkdir(dirName.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
    if (-1 == dir_err)
    {
        printf("ERROR creating directory!\n");
        return;
    }
}

void copyFile( const string& srce_file, const string& dest_file ) {
    // normal copy such as 
    std::ifstream srce( srce_file, std::ios::binary ) ;
    std::ofstream dest( dest_file, std::ios::binary ) ;
    dest << srce.rdbuf() ;
    srce.close();
    dest.close();
}
void writeString2File( const string& line, const string& dest_file ) {
    std::ofstream out(dest_file);
    out << line;
    out.close();
}

bool setfacl(string userid, string directory, bool debug) {
    vector<char> output;
    // this is normally from userid: slurm
    char cmd[100];
    // example expected output: /usr/bin/setfacl -d -m u:cbc:rx,o::- /common/jobscript_archive/cbc
    // also manually posix permission before user mkdir: chmod 766 /common/jobscript_archive
    sprintf(cmd, "/usr/bin/setfacl -d -m u:%s:rx,o::- %s", userid.c_str(), directory.c_str());
    if (debug) 
        cout << "setfacl cmd: " << cmd << endl;

    FILE* pipe = popen(cmd, "r");
    if (!pipe) throw std::runtime_error("popen() failed!");
    try {
        // todo review/test error recover strategy
        size_t status = fread(output.data(), 1, output.size(), pipe);
        if (debug) 
            cout << "setfacl status: " << status << endl;
        if(status < output.size()) {
            throw;
        }
    } catch (...) {
        pclose(pipe);
        throw;
    }
    pclose(pipe);

    if (output.size() > 0) {
        cout << "setfacl ERROR: " << &output[0] << endl;
        return false;
    }
    return true;
}


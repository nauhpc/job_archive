
// to build (also see 00build.txt)
/*
g++ job_archive.cpp -o job_archive -std=c++0x -lpthread
*/
// these are configured in main() at bottom of this code:
//  string srcSpoolHashPath = "/var/spool/slurm/hash.";
//  string targDestPath = "/var/slurm/jobscript_archive";

#include <thread>
#include <iostream>
#include <cstring>
#include <ctime>
#include <string>
#include <fstream>
#include <sstream>
#include <iterator>
#include <iomanip>
#include <vector>
#include <signal.h>
#include <cctype>
using namespace std;

#include "HelperFn.h"
#include "Queue.h"
#include "Logger.h"

// key inotify processing controls
#define MAX_EVENTS 1024 /*Max. number of events to process at one go*/
#define LEN_NAME 16 /*Assuming that the length of the filename won't exceed 16 bytes*/
#define EVENT_SIZE  ( sizeof (struct inotify_event) ) /*size of one event*/
#define BUF_LEN     ( MAX_EVENTS * ( EVENT_SIZE + LEN_NAME )) /*buffer to store the data of events*/

// global flags
int maxSaveJobCnt = 0;
int debug = 0;
/* debug levels
 * 0 - silent, except for errors
 * 1 - final do_processFiles status
 * 2 - final do_processFiles status plus re-try warnings
 * 3 - verbose
 */

void sig(int signo) {
    // note - each version of linux implements different signals, this matches centos 6
    if (signo == SIGHUP) {
        std::cerr << "**** signal commands - current debug = " << debug << " ****" << std::endl;
        std::cerr << "SIGHUP   : sudo kill -" << SIGHUP << " " << getpid() << " - this help" << std::endl;
        std::cerr << "SIGUSR1  : sudo kill -" << SIGUSR1 << " " << getpid() << " - debug on" << std::endl;
        std::cerr << "SIGUSR2  : sudo kill -" << SIGUSR2 << " " << getpid() << " - debug off" << std::endl;
    } else if (signo == SIGUSR1) {
        debug++;
        if (debug > 3) debug = 0;
        std::cerr << "**** debug = " << debug << " ****" << std::endl;

    } else if (signo == SIGUSR2) {
        debug = 0;
        std::cerr << "**** debug = " << debug << " ****" << std::endl;

    } else if (signo == SIGSEGV) {
        std::cerr << "**** Segmentation-fault ****" << std::endl;
        abort();
    } else if (signo == SIGABRT) {
        std::cerr << "**** ABORT ****" << std::endl;
        //abort();
    } else if (signo == SIGINT) {
        std::cerr << "**** INTERRUPT ****" << std::endl;
        abort();
    } else { // in case another signal call occurs, see sig list in main
        std::cerr << "**** SIGNAL: " << signo << " ****" << std::endl;
    }

    return;
}


struct SlurmJobDirectory {
    // constructor
    SlurmJobDirectory(string srcjobdir, int jobId)
        : srcjobdir(srcjobdir), jobId(jobId), retryCnt(0) {
        begin = getCurTime(); 
    }

    string srcjobdir;
    int jobId;
    struct timeval begin;
    int retryCnt;

    string getString() {
        std::ostringstream os;
        os << " jobid: " << jobId
           << " retrycnt: " << retryCnt
           //<< " jobdir: " << srcjobdir
           << " elapse: " << fixed << setprecision(6) << elapsed();
        return(os.str());
    }
    struct timeval getCurTime() {
        struct timeval curTime;
        gettimeofday(&curTime,NULL);
        return curTime;
    }
    double elapsed() {
        struct timeval after = getCurTime();
        return  ( 1000000.0*(after.tv_sec - begin.tv_sec) + (after.tv_usec - begin.tv_usec))/1000000.0;
    }
};

struct ParseBuffer {
    string user;
    string slurm_job_name;
    string slurm_submit_dir;
    // constructor
    ParseBuffer(char *sentence) {
      std::stringstream ss(sentence);
      std::string line;
      if (sentence != NULL) {
        while(std::getline(ss,line,'\n')) {
          size_t found=line.find('=');
          if (found!=std::string::npos) {
              string key = line.substr(0,found);
              if (key == "USER") user = line.substr(found+1);
              if (key == "SLURM_JOB_NAME") slurm_job_name = line.substr(found+1);
              if (key == "SLURM_SUBMIT_DIR") slurm_submit_dir = line.substr(found+1);
          }
          //cout << line << endl;
        }
      }
    }
    string altUser() {
      if (slurm_submit_dir.size() == 0) 
        return slurm_submit_dir;
      return splitUserId(slurm_submit_dir);
    }
    string splitUserId(const string& str) {
      int ix1 = str.find("/home/");
      if (ix1 < 0) return string("");
      string userId = str.substr(ix1+6);
      int ix2 = userId.find("/");
      if (ix2 < 0) return userId;
      return userId.substr(0, ix2);
    }
};

bool verifyUserId(string userid) {
    string alpha = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    string alphanum = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

    // userid criteria: (1) userid.size() > 0
    //    (2) first letter must be alpha, (3) remaining letters must be alpha/num

    if (userid.size() == 0) 
        return false;
    if ( ! isalpha(userid.c_str()[0]))
        return false;
    if (userid.size() > 1)
        for (int i=1; i<userid.size(); i++) {
            //if ( ! isalnum(userid.c_str()[i]) && ! isnum(userid.c_str()[i]))
            if ( ! isalnum(userid.c_str()[i]) )
                return false;
        }

    return true; 
}


vector<char> getEnvFile(const string& inFilename) {

    vector<char> buffer;
    if (debug > 2) cout << "getEnvFile: " << inFilename << endl;
    ifstream ifile( inFilename.c_str(), ios::in|ios::binary);
    if (ifile.fail()) {
        cout << "ERROR - unable to open file: " << inFilename << endl;
        cout << "ERROR: " << strerror(errno) << endl;
        return buffer;
    }

    ifile.seekg (0, ifile.end);
    int length = ifile.tellg();
    ifile.seekg (0, ifile.beg);
    if (debug > 2) cout << "file size: " << length << endl;
    buffer.reserve(length);

     // read the data:
     buffer.insert(buffer.begin(),
         std::istream_iterator<char>(ifile),
         std::istream_iterator<char>());

    return buffer;
}

void saveJobFiles(char* reason, SlurmJobDirectory* jobdir, Logger* logger) {

    // an emergency stop switch in the case of excessive errors
    // todo add lastHour and reset on new hour
    if (++maxSaveJobCnt > 5) return;

    string destDir = "/usr/local/jobarchive/job." + to_string(static_cast<long long>(jobdir->jobId));
    if (! doesDirExist(destDir)) {
        mkDirectory(destDir);
    } else {
        char prtBuf[100];
        sprintf( prtBuf, "WARN: dir already exists: %s", destDir.c_str());
        logger->LOG(prtBuf);
    }
    if (debug > 2) cout << "copy from: " << jobdir->srcjobdir << " to: " << destDir << endl;
    // add newline to reason
    int len = strlen(reason);
    reason[len+0] = 0x0a;
    reason[len+1] = 0x0;
    writeString2File( string(reason), destDir + "/00reason.txt");
    copyFile(jobdir->srcjobdir + "/environment", destDir + "/environment");
    copyFile(jobdir->srcjobdir + "/script", destDir + "/script");
} 

void do_processFiles( const int& id, const string& targDestPath1, Queue<SlurmJobDirectory>* pqueue, Logger* logger) {

    char prtBuf[100];

    while( 1 ) {
        // process both (A)envrionment and (B)script files from: jobdir->srcjobdir
        SlurmJobDirectory* jobdir = pqueue->dequeue();
        if (debug > 1) {
            sprintf( prtBuf, "do_processFiles:%d begin -%s", id, jobdir->getString().c_str());
            logger->LOG(prtBuf);
        }

        // note env file copy is unusual since we read, extract variables, then write file, all as seperate steps
        if (debug > 2) cout << "begin processFiles: " << jobdir->srcjobdir << " jobid: " << jobdir->jobId << endl;

        // A1-verify both files exist before proeeding
        string srcEnvFile = jobdir->srcjobdir + "/environment";
        string srcScriptFile = jobdir->srcjobdir + "/script";
        if (! doesFileExist(srcEnvFile) || ! doesFileExist(srcScriptFile)) {
            jobdir->retryCnt++;
            if (jobdir->elapsed() < 60) {
                if (debug > 1) cout << getCurDateTimeMilliSec() << " WARN: retry file not found in: " << jobdir->srcjobdir << endl;
                // todo put sleep in queue if jobdir->retryCnt > 0 and sleep for 1 sec
                usleep(100);
                pqueue->enqueue(jobdir); // since enqueue/retry then we do not delete jobdir
                if (debug > 2) cout << "enqueue:" << id << " - queue q-size: " << pqueue->getQueueSize() << endl;
            } else {
                sprintf( prtBuf, "ERROR:%d job files not found -%s", id, jobdir->getString().c_str());
                logger->LOG(prtBuf);
                delete jobdir;
            }
            continue;
        }

        // A2-get copy of env file buffer then extract env vars and write new file
        vector<char> buffer = getEnvFile(srcEnvFile);
        int maxRetry = 0;
        while (buffer.size() == 0 && ++maxRetry < 10) {
            if (debug > 2) {
                sprintf( prtBuf, "ERROR:%d retry - %s", id, jobdir->getString().c_str());
                logger->LOG(prtBuf);
            }
            usleep(10000);
            buffer = getEnvFile(srcEnvFile);
        }
        if (buffer.size() == 0) {
            sprintf( prtBuf, "ERROR:%d retry -%s", id, jobdir->getString().c_str());
            logger->LOG(prtBuf);
            delete jobdir;
            continue;
        }
        if (debug > 2) cout << "buffer size = " << buffer.size() << endl;

        // A3-translate hex code to NL
        for (int i=0; i<buffer.size(); i++) {
            // change all 0x00 to newline
            if (buffer[i] == 0x00) buffer[i] = 0x0a;
        }
        // parse env for USER= SLURM_JOB_NAME= SLURM_SUBMIT_DIR=
        ParseBuffer parse(&buffer[0]);

        if (parse.user.size() == 0) {
            if (debug > 1) {
                // ondemand will trigger this, but then it should be fixed by parse.altUser()
                sprintf( prtBuf, "WARN:%d USER env not found -%s", id, jobdir->getString().c_str());
                logger->LOG(prtBuf);
                // good for testing saveJobFile by triggering ondemand job submit
                //saveJobFiles( prtBuf, jobdir, logger );
            }
            // user is required - typically because of ondemand, so here is alt user parse
            // example SLURM_SUBMIT_DIR=/home/bab456/ondemand/data/sys/myjobs/projects/default/3
            if (parse.slurm_submit_dir.size() > 0) {
                parse.user = parse.altUser();
                if (parse.user.size() == 0) {
                    sprintf( prtBuf, "ERROR:%d USER/SLURM_SUBMIT_DIR env not found -%s", id, jobdir->getString().c_str());
                    logger->LOG(prtBuf);
                    saveJobFiles( prtBuf, jobdir, logger );
                    delete jobdir;
                    continue;
                }
                if (debug > 1) cout << "INFO SUCCESS:" << id << " found userid: " << parse.user << endl;
            }
        }
        if ( ! verifyUserId(parse.user)) {
            sprintf( prtBuf, "ERROR:%d invalid userid: %s", id, parse.user.c_str());
            logger->LOG(prtBuf);
            saveJobFiles( prtBuf, jobdir, logger );
            delete jobdir;
            continue;
        }
        if (parse.slurm_job_name.size() == 0) {
            // slurm_job_name is normally populated with 1.script --job-name= setting, or the 2.act script name, or 3.not at all???
            // todo - actually this is optional if not found
            sprintf( prtBuf, "ERROR:%d SLURM_JOB_NAME env not found -%s", id, jobdir->getString().c_str());
            logger->LOG(prtBuf);
            saveJobFiles( prtBuf, jobdir, logger );
            delete jobdir;
            continue;
        }
        if (debug > 2) cout << "Found: USER=" << parse.user
                        << " SLURM_JOB_NAME=" << parse.slurm_job_name
                        << " in: " << srcEnvFile << endl;

        // A4-verfiy user dir path exists, if not then create
        // todo replace with new getCurYearMonth()
        string year = getCurYear();
        string month = getCurMonth();
        string targDestPathUser = targDestPath1 + "/" + parse.user;
        string targDestPathUserYear = targDestPath1 + "/" + parse.user + "/" + year;
        string targDestPathUserYearMonth = targDestPath1 + "/" + parse.user + "/" + year + "/" + month;
        string targDestPathFull = targDestPath1 + "/" + parse.user + "/" + year + "/" + month;

        // if user dir does not exist then create dir
        if (! doesDirExist(targDestPathUser)) {
            if (debug > 2) cout << "INFO: creating user - dir does not exist: " << targDestPathUser << endl;
            mkDirectory(targDestPathUser);
            if (! doesDirExist(targDestPathUser)) {
                sprintf( prtBuf, "ERROR:%d user dir does not exist: %s -%s", id, targDestPathUser.c_str(), jobdir->getString().c_str());
                logger->LOG(prtBuf);
                saveJobFiles( prtBuf, jobdir, logger );
                delete jobdir;
                continue;
            }
            // after creating new dir then setfacl
            bool setfaclDebug = false;
            if (debug > 1) setfaclDebug = true;
            if (! setfacl( parse.user, targDestPathUser, setfaclDebug)) {
                // todo double check the error handling in setfacl
                sprintf( prtBuf, "ERROR:%d setfacl user: %s -%s", id, parse.user.c_str(), jobdir->getString().c_str());
                logger->LOG(prtBuf);
                saveJobFiles( prtBuf, jobdir, logger );
                delete jobdir;
                continue;
            }
        }

        // if user/year dir does not exist then create dir
        if (! doesDirExist(targDestPathUserYear)) {
            if (debug > 2) cout << "INFO: creating user/year - dir does not exist: " << targDestPathUserYear << endl;
            mkDirectory(targDestPathUserYear);
            if (! doesDirExist(targDestPathUserYear)) {
                sprintf( prtBuf, "ERROR:%d user/year dir does not exist: %s -%s", id, targDestPathUserYear.c_str(), jobdir->getString().c_str());
                logger->LOG(prtBuf);
                saveJobFiles( prtBuf, jobdir, logger );
                delete jobdir;
                continue;
            }
        }

        // if user/year/month dir does not exist then create dir
        if (! doesDirExist(targDestPathUserYearMonth)) {
            if (debug > 2) cout << "INFO: creating user/year/month - dir does not exist: " << targDestPathUserYearMonth << endl;
            mkDirectory(targDestPathUserYearMonth);
            if (! doesDirExist(targDestPathUserYearMonth)) {
                sprintf( prtBuf, "ERROR:%d user year/month/dir does not exist: %s -%s", id, targDestPathUserYearMonth.c_str(), jobdir->getString().c_str());
                logger->LOG(prtBuf);
                saveJobFiles( prtBuf, jobdir, logger );
                delete jobdir;
                continue;
            }
        }

        // base filename
        string targFileBase = to_string(static_cast<long long>(jobdir->jobId)) + (parse.slurm_job_name.size() == 0 ? "" : string("-") + parse.slurm_job_name);
        string destScriptFile = targDestPathFull + "/" + targFileBase + ".sh";
        string destEnvFile = targDestPathFull + "/" + targFileBase + ".env";

        // A5-now save env file
        if (debug > 2) cout << "copy from: " << srcEnvFile << " to: " << destEnvFile << endl;
        ofstream oFile( destEnvFile.c_str(), ios::out | ios::binary);
        oFile.write( &buffer[0], buffer.size() );
        oFile.close();
        // consider verify destEnvFile was copied?

        // B1-now copy script file
        if (debug > 2) cout << "copy from: " << srcScriptFile << " to: " << destScriptFile << endl;
        copyFile(srcScriptFile, destScriptFile);
        // consider verify destScriptFile was copied?

        if (debug > 0) {
            sprintf( prtBuf, "do_processFiles:%d  DONE -%s", id, jobdir->getString().c_str());
            logger->LOG(prtBuf);
        }

       delete jobdir;
    }
}

void do_inotify(const int& id, const string& watchDir, Queue<SlurmJobDirectory>* pqueue, Logger* logger) {
    if (debug > 2) cout << "do_inotify begin:" << id << " watch: " << watchDir.c_str() << endl;

    if (! doesDirExist(watchDir)) {
        cout << "ERROR do_inotify watchDir does not exists: " << watchDir << endl;
        exit(1);
    }

    int length, i = 0, wd;
    int fd;
    char buffer[BUF_LEN];

    /* Initialize Inotify*/
    fd = inotify_init();
    if ( fd < 0 ) {
        printf( "ERROR: could not initialize inotify\n");
        exit(1);
    }

    /* add watch to starting directory */
    wd = inotify_add_watch(fd, watchDir.c_str(), IN_CREATE);
    if (wd == -1) {
        printf("ERROR: could not add watch to %s\n",watchDir.c_str());
        exit(1);
    } else {
        printf("watching:%d %s\n",id, watchDir.c_str());
    }
 
    while( 1 ) {
        i = 0;
        length = read( fd, buffer, BUF_LEN );  
 
        if ( length < 0 ) {
          perror( "read" );
          fflush(stderr);
        }  
 
        while ( i < length ) {
          struct inotify_event *event = ( struct inotify_event * ) &buffer[ i ];
          if ( event->len ) {
            if ( event->mask & IN_CREATE && event->mask & IN_ISDIR) {
                if (debug > 2) {
                    printf( "The directory %s was Created. %d\n", event->name, event->wd );
                }

                int job_id = 0;
                // event->name expected format: job.NNNN
                if (strchr(event->name, '.')) {
                    job_id = stoi(strchr(event->name, '.')+1);
                    // todo - test the non-numeric case
                } 
                if (job_id == 0) { 
                    char prtBuf[BUF_LEN];
                    sprintf( prtBuf, "ERROR:%d unexpected hash->jobdir format: %s", id, event->name);
                    logger->LOG(prtBuf);
                    continue;
                }
                
                // main processing is fed into queue for do_processFiles
                string srcJobIdDir = watchDir + "/" + string(event->name);
                SlurmJobDirectory* jobdir = new SlurmJobDirectory(srcJobIdDir, job_id);
                pqueue->enqueue(jobdir);
                if (debug > 2) cout << "do_inotify:" << id << " enqueue q-size: " << pqueue->getQueueSize() << endl;

            } // if ( event->mask & IN_CREATE && event->mask & IN_ISDIR)

            i += EVENT_SIZE + event->len;
          } // if ( event->len )
        } // while
    } // while(1)
 
    /* Clean up*/
    inotify_rm_watch( fd, wd );
    close( fd );
    cout << "do_inotify end:" << id << " watch: " << watchDir.c_str() << endl;
}

int main( int argc, char **argv ) {

    signal(SIGINT, sig);
    signal(SIGSEGV, sig);
    signal(SIGABRT, sig);
    signal(SIGHUP, sig);
    signal(SIGUSR1, sig);
    signal(SIGUSR2, sig);

    Logger logger;
    char prtBuf[100];
    sprintf(prtBuf, "main begin - for help: sudo kill -1 %ld", getpid());
    logger.LOG(prtBuf);

    if (argc == 2 && (strcmp(argv[1],"-d") == 0 || strcmp(argv[1],"-d1") == 0)) {
        debug = 1;
        std::cerr << "**** debug = " << debug << " ****" << std::endl;
    }
    if (argc == 2 && strcmp(argv[1],"-d2") == 0) {
        debug = 2;
        std::cerr << "**** debug = " << debug << " ****" << std::endl;
    }
    if (argc == 2 && strcmp(argv[1],"-d3") == 0) {
        debug = 3;
        std::cerr << "**** debug = " << debug << " ****" << std::endl;
    }

    string srcSpoolHashPath = "/var/spool/slurm/hash.";
    string targDestPath = "/var/slurm/jobscript_archive";
    Queue<SlurmJobDirectory> queue;

    int QUE_THD_SIZE=2;
    thread th_que_process[QUE_THD_SIZE];
    // Create threads for watching queue - id: 10,11
    for (int i = 0; i < QUE_THD_SIZE; i++) {
        th_que_process[i] = thread(do_processFiles, i+10, targDestPath, &queue, &logger);
    }

    int DIR_THD_SIZE=10;
    thread th_inotify[DIR_THD_SIZE];
    // Create threads for watching hash directories - id: 0 to 9
    for (int i = 0; i < DIR_THD_SIZE; i++) {
        string slurmHashDir = srcSpoolHashPath + to_string(static_cast<long long>(i));
        th_inotify[i] = thread(do_inotify, i, slurmHashDir, &queue, &logger);
    }

    // Join threads
    for (int i = 0; i < DIR_THD_SIZE; i++) {
        th_inotify[i].join();
    }
    // Join threads
    for (int i = 0; i < QUE_THD_SIZE; i++) {
        th_que_process[i].join();
    }
    
    cerr << "=== main end ===" << endl;
    return 0;
}

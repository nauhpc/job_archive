
#ifndef _LOGGER_H
#define _LOGGER_H

#include <thread>
#include <mutex>

class Logger {
    bool _useLock;
    std::mutex _log;
    std::string getDateTime() {
        // return format: 2019-04-01 14:58:41
        time_t timer;
        char buffer[26];
        struct tm* tm_info;

        time(&timer);
        tm_info = localtime(&timer);

        strftime(buffer, 26, "%Y-%m-%d %H:%M:%S", tm_info);
        return std::string(buffer);
    }
    string getDateTimeMilliSec() {
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

public:
    Logger() : _useLock(true) {};
    Logger(bool useLock) : _useLock(useLock) {};

    void LOG(const char *message) {
        if (_useLock) {
            _log.lock();
        }
        //std::cout << std::this_thread::get_id() << " " << getDateTime() << " " << message << std::endl;
        std::cout << getDateTimeMilliSec() << " " << message << std::endl;
        if (_useLock) {
            _log.unlock();
        }
    }
    void LOG(std::string message) {
        if (_useLock) {
            _log.lock();
        }
        //std::cout << std::this_thread::get_id() << " " << getDateTime() << " " << message << std::endl;
        std::cout << getDateTimeMilliSec() << " " << message << std::endl;
        if (_useLock) {
            _log.unlock();
        }
    }
};

#endif //_LOGGER_H


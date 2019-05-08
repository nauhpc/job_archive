
#include <mutex>
#include <condition_variable>
        
class Semaphore {
    std::mutex mtx;
    std::condition_variable cv;
    int count;
public:
    Semaphore (int count_ = 0) : count(count_) { }
    
    //inline void post( int tid ) {
    inline void post() {
        std::unique_lock<std::mutex> lock(mtx);
        count++;
        //cout << "thread " << tid <<  " notify" << endl;
        //notify the waiting thread
        cv.notify_one();
    }

    //inline void wait( int tid ) {
    inline void wait() {
        std::unique_lock<std::mutex> lock(mtx);
        while(count == 0) {
            //cout << "thread " << tid << " wait" << endl;
            //wait on the mutex until notify is called
            cv.wait(lock);
            //cout << "thread " << tid << " run" << endl;
        }
        count--;
    }
};

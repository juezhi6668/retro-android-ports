// ported from pdx203-jellybean by @chardidathing, https://github.com/chardidathing/pdx203-jellybean
// the renderer only wants android::Mutex out of the platform's libutils
#pragma once
#include <pthread.h>

namespace android {

class Mutex {
public:
    Mutex() { pthread_mutex_init(&m, nullptr); }
    ~Mutex() { pthread_mutex_destroy(&m); }
    int lock() { return -pthread_mutex_lock(&m); }
    void unlock() { pthread_mutex_unlock(&m); }
    int tryLock() { return -pthread_mutex_trylock(&m); }

    class Autolock {
    public:
        explicit Autolock(Mutex& mutex) : l(mutex) { l.lock(); }
        explicit Autolock(Mutex* mutex) : l(*mutex) { l.lock(); }
        ~Autolock() { l.unlock(); }
    private:
        Mutex& l;
    };

private:
    Mutex(const Mutex&);
    Mutex& operator=(const Mutex&);
    pthread_mutex_t m;
};

typedef Mutex::Autolock AutoMutex;

}

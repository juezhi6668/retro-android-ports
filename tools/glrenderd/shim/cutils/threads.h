// ported from pdx203-jellybean by @chardidathing, https://github.com/chardidathing/pdx203-jellybean
// cutils thread_store_t on plain pthread keys
#pragma once
#include <pthread.h>

typedef struct {
    pthread_mutex_t lock;
    int has_tls;
    pthread_key_t tls;
} thread_store_t;

#define THREAD_STORE_INITIALIZER { PTHREAD_MUTEX_INITIALIZER, 0, 0 }

typedef void (*thread_store_destruct_t)(void* value);

static inline void* thread_store_get(thread_store_t* store)
{
    return store->has_tls ? pthread_getspecific(store->tls) : nullptr;
}

static inline void thread_store_set(thread_store_t* store, void* value, thread_store_destruct_t destroy)
{
    pthread_mutex_lock(&store->lock);
    if (!store->has_tls) {
        pthread_key_create(&store->tls, destroy);
        store->has_tls = 1;
    }
    pthread_mutex_unlock(&store->lock);
    pthread_setspecific(store->tls, value);
}

typedef pthread_mutex_t mutex_t;
#define MUTEX_INITIALIZER PTHREAD_MUTEX_INITIALIZER
static inline int mutex_init(mutex_t* m) { return pthread_mutex_init(m, nullptr); }
static inline int mutex_lock(mutex_t* m) { return pthread_mutex_lock(m); }
static inline int mutex_unlock(mutex_t* m) { return pthread_mutex_unlock(m); }
static inline void mutex_destroy(mutex_t* m) { pthread_mutex_destroy(m); }

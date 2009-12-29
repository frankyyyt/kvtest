#ifndef EP_HH
#define EP_HH 1

#include <pthread.h>
#include <assert.h>
#include <stdbool.h>
#include <stdexcept>
#include <iostream>

#include "google/config.h"
#include <google/sparse_hash_map>
#include <google/sparse_hash_set>

#include "base-test.hh"

namespace kvtest {

    // Forward declaration
    class Flusher;

    class EventuallyPersistentStore : public KVStore {
    public:

        EventuallyPersistentStore(KVStore *t);

        ~EventuallyPersistentStore();

        void set(std::string &key, std::string &val,
                 Callback<bool> &cb);

        void get(std::string &key, Callback<GetValue> &cb);

        void del(std::string &key, Callback<bool> &cb);

        void reset();

    private:

        void markDirty(std::string &key);
        void flush(bool shouldWait);
        void flushOne(std::string &key, Callback<bool> &cb);

        friend class Flusher;

        KVStore                                           *underlying;
        Flusher                                           *flusher;
        google::sparse_hash_map<std::string, std::string>  storage;
        pthread_mutex_t                                    mutex;
        pthread_cond_t                                     cond;
        google::sparse_hash_set<std::string>              *towrite;
        pthread_t                                          thread;
        DISALLOW_COPY_AND_ASSIGN(EventuallyPersistentStore);
    };

    class Flusher {
    public:
        Flusher(EventuallyPersistentStore *st) {
            store = st;
            running = true;
        }
        ~Flusher() {
            stop();
        }
        void stop() {
            running = false;
        }
        void run() {
            try {
                while(running) {
                    store->flush(true);
                }
                std::cout << "Shutting down flusher." << std::endl;
            } catch(std::runtime_error &e) {
                std::cerr << "Exception in executor loop: "
                          << e.what() << std::endl;
                assert(false);
            }
        }
    private:
        EventuallyPersistentStore *store;
        volatile bool running;
    };

}

#endif /* EP_HH */
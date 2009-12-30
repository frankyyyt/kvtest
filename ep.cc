#include "ep.hh"
#include "locks.hh"

namespace kvtest {

    static void* launch_flusher_thread(void* arg) {
        Flusher *flusher = (Flusher*) arg;
        try {
            flusher->run();
        } catch(...) {
            std::cerr << "Caught a fatal exception in the thread" << std::endl;
        }
        return NULL;
    }

    EventuallyPersistentStore::EventuallyPersistentStore(KVStore *t,
                                                         size_t est) {

        pthread_mutex_init(&mutex, NULL);
        pthread_cond_init(&cond, NULL);
        est_size = est;
        towrite = NULL;
        initQueue();
        flusher = new Flusher(this);

        // Run in a thread...
        if(pthread_create(&thread, NULL, launch_flusher_thread, flusher)
           != 0) {
            throw std::runtime_error("Error initializing queue thread");
        }

        underlying = t;
        assert(underlying);
    }

    EventuallyPersistentStore::~EventuallyPersistentStore() {
        LockHolder lh(&mutex);
        flusher->stop();
        if(pthread_cond_signal(&cond) != 0) {
            throw std::runtime_error("Error signaling change.");
        }
        lh.unlock();
        pthread_join(thread, NULL);
        delete flusher;
        delete towrite;
        for (std::map<std::string, StoredValue*>::iterator it=storage.begin();
             it != storage.end(); it++ ) {
            delete it->second;
        }
        pthread_cond_destroy(&cond);
        pthread_mutex_destroy(&mutex);
    }

    void EventuallyPersistentStore::initQueue() {
        assert(!towrite);
        towrite = new std::queue<std::string>;
    }

    void EventuallyPersistentStore::set(std::string &key, std::string &val,
                                        Callback<bool> &cb) {
        bool wasDirty = false;
        LockHolder lh(&mutex);
        std::map<std::string, StoredValue*>::iterator it = storage.find(key);
        if (it != storage.end()) {
            wasDirty = it->second->isDirty();
            delete it->second;
        }

        storage[key] = new StoredValue(val, wasDirty);
        bool rv = true;
        markDirty(key);
        lh.unlock();
        cb.callback(rv);
    }

    void EventuallyPersistentStore::reset() {
        flush(false);
        LockHolder lh(&mutex);
        underlying->reset();
        delete towrite;
        towrite = NULL;
        initQueue();
        storage.clear();
    }

    void EventuallyPersistentStore::get(std::string &key,
                                        Callback<kvtest::GetValue> &cb) {
        LockHolder lh(&mutex);
        std::map<std::string, StoredValue*>::iterator it = storage.find(key);
        bool success = it != storage.end();
        kvtest::GetValue rv(success ? it->second->getValue() : std::string(":("),
                            success);
        lh.unlock();
        cb.callback(rv);
    }

    void EventuallyPersistentStore::del(std::string &key, Callback<bool> &cb) {
        bool rv = true;
        LockHolder lh(&mutex);
        std::map<std::string, StoredValue*>::iterator it = storage.find(key);
        if(it == storage.end()) {
            rv = false;
        } else {
            markDirty(key);
            delete it->second;
            storage.erase(key);
        }
        lh.unlock();
        cb.callback(rv);
    }

    void EventuallyPersistentStore::markDirty(std::string &key) {
        // Lock is assumed to be held here.
        std::map<std::string, StoredValue*>::iterator it = storage.find(key);
        if (it == storage.end() || it->second->isClean()) {
            if (it != storage.end()) {
                assert(it->second->isClean());
                it->second->markDirty();
            }

            towrite->push(key);
            if(pthread_cond_signal(&cond) != 0) {
                throw std::runtime_error("Error signaling change.");
            }
        }
    }

    void EventuallyPersistentStore::flush(bool shouldWait) {
        LockHolder lh(&mutex);

        if (towrite->empty()) {
            if (shouldWait) {
                if(pthread_cond_wait(&cond, &mutex) != 0) {
                    throw std::runtime_error("Error waiting for signal.");
                }
            }
        } else {
            std::queue<std::string> *q = towrite;
            towrite = NULL;
            initQueue();
            lh.unlock();

            RememberingCallback<bool> cb;
            assert(underlying);
            while (!q->empty()) {
                flushSome(q, cb);
            }

            delete q;
        }
    }

    void EventuallyPersistentStore::flushSome(std::queue<std::string> *q,
                                             Callback<bool> &cb) {
        std::map<std::string, std::string> toSet;
        std::queue<std::string> toDelete;

        LockHolder lh(&mutex);
        for (int i = 0; i < 1000 && !q->empty(); i++) {
            std::string key = q->front();
            q->pop();

            std::map<std::string, StoredValue*>::iterator it = storage.find(key);
            bool found = it != storage.end();
            bool isDirty = (found && it->second->isDirty());
            std::string val;
            if (isDirty) {
                it->second->markClean();
                val = it->second->getValue();
            }

            if (found && isDirty) {
                toSet[key] = val;
            } else if (!found) {
                toDelete.push(key);
            }
        }
        lh.unlock();

        underlying->begin();
        // Handle deletes first.
        while (!toDelete.empty()) {
            std::string key = toDelete.front();
            underlying->del(key, cb);
            toDelete.pop();
        }

        // Then handle sets.
        std::map<std::string, std::string>::iterator it = toSet.begin();
        for (; it != toSet.end(); it++) {
            std::string key = it->first;
            std::string val = it->second;
            underlying->set(key, val, cb);
        }
        underlying->commit();
    }

}

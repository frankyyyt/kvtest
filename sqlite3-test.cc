#include <assert.h>
#include <stdio.h>
#include <iostream>

#include <sqlite3.h>

#include "base-test.hh"

#define MAX_STEPS 10000

class PreparedStatement {
public:
    PreparedStatement(sqlite3 *d, const char *query) {
        db = d;
        if(sqlite3_prepare_v2(db, query, strlen(query), &st, NULL)
           != SQLITE_OK) {
            throw std::runtime_error(sqlite3_errmsg(db));
        }
    }

    ~PreparedStatement() {
        sqlite3_finalize(st);
    }

    void bind(int pos, const char *s) {
        sqlite3_bind_text(st, pos, s, strlen(s), SQLITE_TRANSIENT);
    }

    int execute() {
        int steps_run = 0, rc = 0;
        while ((rc = sqlite3_step(st)) != SQLITE_DONE) {
            steps_run++;
            assert(steps_run < MAX_STEPS);
        }
        return sqlite3_changes(db);
    }

    bool fetch() {
        bool rv = true;
        assert(st);
        switch(sqlite3_step(st)) {
        case SQLITE_BUSY:
            throw std::runtime_error("DB was busy.");
            break;
        case SQLITE_ROW:
            break;
        case SQLITE_DONE:
            rv = false;
            break;
        default:
            throw std::runtime_error("Unhandled case.");
        }
        return rv;
    }

    const char *column(int x) {
        return (char*)sqlite3_column_text(st, x);
    }

private:
    sqlite3      *db;
    sqlite3_stmt *st;
};

class Sqlite3 : public kvtest::ThingUnderTest {
public:

    Sqlite3(const char *fn) {
        filename = fn;
        open();
    }

    ~Sqlite3() {
        free_errmsg();
        close();
    }

    void open() {
        if(!db) {
            if(sqlite3_open(filename, &db) !=  SQLITE_OK) {
                throw std::runtime_error("Error initializing sqlite3");
            }

            initTables();
        }
    }

    void close() {
        if(db) {
            sqlite3_close(db);
        }
    }

    void initTables() {
        execute("drop table if exists kv");
        execute("vacuum");
        execute("create table if not exists kv"
                " (k varchar(250) primary key on conflict replace,"
                "  v text)");
    }

    void reset() {
        close();
        open();
    }

    bool set(std::string &key, std::string &val) {
        PreparedStatement st(db, "insert into kv(k,v) values(?, ?)");
        st.bind(1, key.c_str());
        st.bind(2, val.c_str());
        st.execute();
        return true;
    }

    std::string* get(std::string &key) {
        PreparedStatement st(db, "select v from kv where k = ?");
        st.bind(1, key.c_str());

        if(!st.fetch()) {
            return NULL;
        }

        return new std::string(st.column(0));
    }

    bool del(std::string &key) {
        PreparedStatement st(db, "delete from kv where k = ?");
        st.bind(1, key.c_str());
        return st.execute() == 1;
    }

protected:
    void execute(const char *query) {
        free_errmsg();
        if(sqlite3_exec(db, query, NULL, NULL, &errmsg) != SQLITE_OK) {
            throw std::runtime_error(errmsg);
        }
    }

private:
    const char *filename;
    char *errmsg;
    sqlite3 *db;

    void free_errmsg() {
        if(errmsg) {
            sqlite3_free(errmsg);
            errmsg = NULL;
        }
    }
};

int main(int argc, char **args) {
    Sqlite3 *thing = new Sqlite3("/tmp/test.db");

    kvtest::TestSuite suite(thing);
    return suite.run() ? 0 : 1;
}
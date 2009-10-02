#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <iostream>

#include "base-test.hh"
#include "suite.hh"
#include "sqlite-base.hh"
#include "async.hh"

using namespace kvtest;

int main(int argc, char **args) {
    Sqlite3 *sq = new Sqlite3("/tmp/test.db");
    QueuedKVStore *thing = new QueuedKVStore(sq);

    TestSuite suite(thing);
    int rv = suite.run() ? 0 : 1;
    delete thing;
    delete sq;

    return rv;
}

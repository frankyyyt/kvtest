#include <stdio.h>
#include <string.h>
#include <iostream>

#define HAVE_CXX_STDHEADERS 1
#include <db.h>

#include "base-test.hh"
#include "suite.hh"
#include "bdb-base.hh"
#include "async.hh"

using namespace std;
using namespace kvtest;

int main(int argc, char **args) {
    BDBStore *bdb = new BDBStore("/tmp/test.bdb", false);
    QueuedKVStore *thing = new QueuedKVStore(bdb);

    TestSuite suite(thing);
    int rv = suite.run() ? 0 : 1;
    delete thing;
    delete bdb;

    return rv;
}

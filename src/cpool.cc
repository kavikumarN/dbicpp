#include "dbic++.h"

namespace dbi {
    extern void initCheck(string name);
    extern map<string, Driver *> drivers;

    ConnectionPool::ConnectionPool(
        int size, string driver_name, string user, string pass, string dbname, string host, string port
    ) {
        initCheck(driver_name);
        struct Connection c;
        for (int n = 0; n < size; n++) {
            AbstractHandle *h = drivers[driver_name]->connect(user, pass, dbname, host, port);
            h->initAsync();
            c.handle = h;
            c.busy   = false;
            pool.push_back(c);
        }
    }

    ConnectionPool::ConnectionPool (int size, string driver_name, string user, string pass, string dbname) {
        initCheck(driver_name);
        struct Connection c;
        for (int n = 0; n < size; n++) {
            AbstractHandle *h = drivers[driver_name]->connect(user, pass, dbname, "", "");
            h->initAsync();
            c.handle = h;
            c.busy   = false;
            pool.push_back(c);
        }
    }

    Request* ConnectionPool::execute(string sql, vector<Param> &bind, void (*cb)(AbstractResultSet *r), void *context) {
        for (int n = 0; n < pool.size(); n++) {
            if (!pool[n].busy) {
                pool[n].busy = true;

                AbstractHandle *h = pool[n].handle;
                AbstractResultSet *rs = h->aexecute(sql, bind);
                rs->context = context;

                Request *r  = new Request(this, h, rs, cb);
                return r;
            }
        }

        return 0;
    }

    Request* ConnectionPool::execute(string sql, void (*cb)(AbstractResultSet *r), void *context) {
        vector<Param> bind;
        return execute(sql, bind, cb, context);
    }

    ConnectionPool::~ConnectionPool() {
        for (int n = 0; n < pool.size(); n++) {
            pool[n].handle->close();
            pool[n].handle->cleanup();
            delete pool[n].handle;
        }
    }

    bool ConnectionPool::process(Request *r) {
        AbstractResultSet *rs = r->result;
        void (*callback)(AbstractResultSet *) = r->callback;
        if(!rs->consumeResult()) {
            rs->prepareResult();
            for (int n = 0; n < pool.size(); n++) {
                if (r->handle == pool[n].handle) {
                    pool[n].busy = false;
                    break;
                }
            }
            callback(rs);
            return true;
        }
        return false;
    }

    Request::Request(ConnectionPool *p, AbstractHandle *h, AbstractResultSet *rs, void (*cb)(AbstractResultSet *)) {
        target   = p;
        handle   = h;
        result   = rs;
        callback = cb;
    }

    Request::~Request() {
        result->cleanup();
        delete result;
    }

    int Request::socket() {
        return handle->socket();
    }

    bool Request::process() {
        return target->process(this);
    }
}
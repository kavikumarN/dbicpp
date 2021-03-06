#include "common.h"

namespace dbi {
    Sqlite3Statement::Sqlite3Statement(string sql,  sqlite3 *conn) {
        _sql    = sql;
        _conn   = conn;
        _stmt   = 0;
        _result = 0;

        SQLITE3_PREPROCESS_QUERY(sql);
        if (sqlite3_prepare_v2(conn, sql.c_str(), sql.length(), &_stmt, 0) != SQLITE_OK) {
            snprintf(errormsg, 8192, "%s", sqlite3_errmsg(conn));
            throw RuntimeError(errormsg);
        }
    }

    Sqlite3Statement::~Sqlite3Statement() {
        cleanup();
    }

    void Sqlite3Statement::cleanup() {
        finish();

        if (_stmt)   sqlite3_finalize(_stmt);
        if (_result) delete _result;

        _result = 0;
        _stmt   = 0;
    }

    void Sqlite3Statement::finish() {
        if (_stmt) {
            sqlite3_reset(_stmt);
            sqlite3_clear_bindings(_stmt);
        }
    }

    string Sqlite3Statement::command() {
        return _sql;
    }

    uint32_t Sqlite3Statement::execute() {
        param_list_t bind;
        return execute(bind);
    }

    uint32_t Sqlite3Statement::execute(param_list_t &bind) {
        int rc, n;
        unsigned char *data;
        uint64_t length;
        unsigned char *empty = (unsigned char*)"";

        finish();

        uint64_t affected_rows = sqlite3_total_changes(_conn);
        int expect_bind_count  = sqlite3_bind_parameter_count(_stmt);

        if (bind.size() != expect_bind_count) {
            snprintf(errormsg, 8192, "In SQL: %s, expected %d bind values got %d\n",
                               _sql.c_str(), expect_bind_count, (int)bind.size());
            throw RuntimeError(errormsg);
        }

        if (_result) _result->clear();
        else _result = new Sqlite3Result(_stmt, _sql);

        if (bind.size() > 0) SQLITE3_PROCESS_BIND(_stmt, bind);
        while ((rc = sqlite3_step(_stmt)) == SQLITE_ROW) {
            for (n = 0; n < _result->columns(); n++) {
                switch(sqlite3_column_type(_stmt, n)) {
                    case SQLITE_NULL:
                        _result->write(n, 0, 0);
                        break;
                    case SQLITE_TEXT:
                    case SQLITE_BLOB:
                        data   = (unsigned char*)sqlite3_column_blob(_stmt, n);
                        length = sqlite3_column_bytes(_stmt, n);
                        _result->write(n, data ? data : empty, length);
                        break;
                    default:
                        data = (unsigned char*)sqlite3_column_text(_stmt, n);
                        _result->write(n, data, data ? strlen((char*)data) : 0);
                }
            }
            _result->flush(_stmt);
        }

        if (rc != SQLITE_DONE) {
            snprintf(errormsg, 8192, "%s", sqlite3_errmsg(_conn));
            throw RuntimeError(errormsg);
        }

        _result->rewind();
        _result->affected_rows  = sqlite3_total_changes(_conn) - affected_rows;
        _result->last_insert_id = last_insert_id = sqlite3_last_insert_rowid(_conn);
        return _result->rows();
    }

    Sqlite3Result* Sqlite3Statement::result() {
        Sqlite3Result *instance = _result;
        _result = 0;
        return instance;
    }

    uint64_t Sqlite3Statement::lastInsertID() {
        return last_insert_id;
    }
}

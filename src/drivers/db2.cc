#include "dbic++.h"
#include "sqlcli1.h"
#include "db2ApiDf.h"

#define DRIVER_NAME     "db2"
#define DRIVER_VERSION  "1.3"


namespace dbi {

    using namespace std;

    SQLLEN DB2_NULL_INDICATOR = SQL_NULL_DATA;
    map<int, bool> OPENSOCKETS;

    void db2error(short type, SQLHANDLE handle) {
        SQLCHAR     message[SQL_MAX_MESSAGE_LENGTH+1];
        SQLCHAR     sqlstate[SQL_SQLSTATE_SIZE+1];
        SQLINTEGER  sqlcode;
        SQLSMALLINT length, i = 1;

        string messages;

        while(SQLGetDiagRec(type, handle, i++, sqlstate, &sqlcode, message, SQL_MAX_MESSAGE_LENGTH, &length) == 0)
            messages += string((char*)message) + "\n";
        throw RuntimeError(messages);
    }

    void CHECK_HANDLE_RESULT(SQLHANDLE handle, int rc) {
        if (rc != SQL_SUCCESS && rc != SQL_SUCCESS_WITH_INFO) {
            SQLEndTran(SQL_HANDLE_DBC, handle, SQL_ROLLBACK);
            db2error(SQL_HANDLE_DBC, handle);
        }
    }

    void CHECK_STATEMENT_RESULT(SQLHANDLE handle, SQLHANDLE stmt, int rc) {
        if (rc != SQL_SUCCESS && rc != SQL_SUCCESS_WITH_INFO) {
            SQLEndTran(SQL_HANDLE_DBC, handle, SQL_ROLLBACK);
            db2error(SQL_HANDLE_STMT, stmt);
        }
    }

    class DB2Statement : public AbstractStatement {
        protected:
        bool isasync;
        string sql;
        uint32_t _rowno, _rows;
        vector<ResultRow> results;
        vector<string>    _rsfields;
        vector<int>       _rstypes;
        SQLHANDLE stmt;
        SQLSMALLINT _columns;
        SQLHANDLE handle;

        void checkResult(int);
        void fetchMeta();
        void processBindArguments(vector<Param> &bind, int size = 0);

        public:
        uint32_t columns();
        uint32_t rows();
        vector<string> fields();
        bool read(ResultRow &);
        bool read(ResultRowHash &);
        unsigned char* read(uint32_t r, uint32_t c, uint64_t *l = 0);
        uint64_t write(string, FieldSet&, IO*);
        bool finish();
        uint32_t tell();
        void cleanup();
        uint64_t lastInsertID();
        bool consumeResult();
        void prepareResult();
        void rewind();
        vector<int>& types();
        void seek(uint32_t);
        bool cancel();

        // Statement specific stuff
        DB2Statement(SQLHANDLE, string);
        DB2Statement(SQLHANDLE);
        ~DB2Statement();
        uint32_t execute();
        uint32_t execute(vector<Param> &bind);
        uint32_t execute(string);
        uint32_t execute(string, vector<Param> &bind);
        void aexecute(string, vector<Param> &bind);
        bool isBusy();
        string command();
        string driver();
    };

    class DB2Handle : public AbstractHandle {
        protected:
        SQLHANDLE env, handle;
        DB2Statement *stmt;
        void setup();
        void TCPIPConnect(string, string, string, string, string);
        int tr_nesting;
        void checkResult(int);
        int socketfd;

        public:
        DB2Handle(string, string, string);
        DB2Handle(string, string, string, string, string);
        ~DB2Handle();
        uint32_t execute(string);
        uint32_t execute(string, vector<Param> &);
        DB2Statement* prepare(string);
        bool begin();
        bool commit();
        bool rollback();
        bool begin(string);
        bool commit(string);
        bool rollback(string);
        void* call(string, void*, uint64_t);
        bool close();
        uint64_t write(string, FieldSet&, IO*);
        void setTimeZoneOffset(int, int);
        void setTimeZone(char*);
        void cleanup();
        string escape(string);
        AbstractResult* results();
        string driver();

        // ASYNC
        int socket();
        AbstractResult* aexecute(string, vector<Param> &);
        void initAsync();
        bool isBusy();
        bool cancel();
    };


    // -----------------------------------------------------------------------------
    // DB2Statement
    // -----------------------------------------------------------------------------

    void DB2Statement::checkResult(int rc) {
        CHECK_STATEMENT_RESULT(handle, stmt, rc);
    }

    DB2Statement::DB2Statement(SQLHANDLE handle, string sql) {
        this->sql     = sql;
        this->_rows   = 0;
        this->_rowno  = 0;
        this->handle  = handle;
        this->isasync = false;
        SQLAllocHandle(SQL_HANDLE_STMT, handle, &stmt);
        checkResult(SQLPrepare(stmt, (SQLCHAR*)sql.c_str(), SQL_NTS));
        fetchMeta();
    }

    DB2Statement::DB2Statement(SQLHANDLE handle) {
        this->_rows   = 0;
        this->_rowno  = 0;
        this->handle  = handle;
        this->isasync = false;
        SQLAllocHandle(SQL_HANDLE_STMT, handle, &stmt);
    }

    string DB2Statement::driver() {
        return DRIVER_NAME;
    }

    void DB2Statement::fetchMeta() {
        int i, j;
        SQLCHAR buffer[1024];
        SQLSMALLINT type, length;
        SQLLEN size;
        SQLNumResultCols(stmt, &_columns);

        for (i = 0; i < _columns; i++) {
            SQLDescribeCol(stmt, i+1, buffer, 1024, &length, &type, 0, 0, 0);
            for (j = 0; j < length; j++) buffer[j] = tolower(buffer[j]);
            _rsfields.push_back(string((char*)buffer, length));
            switch(type) {
                case SQL_BIGINT:
                case SQL_INTEGER:
                case SQL_SMALLINT:
                    _rstypes.push_back(DBI_TYPE_INT); break;

                case SQL_BINARY:
                case SQL_BLOB:
                case SQL_BLOB_LOCATOR:
                case SQL_VARBINARY:
                    _rstypes.push_back(DBI_TYPE_BLOB); break;

                case SQL_DECIMAL:
                case SQL_DOUBLE:
                case SQL_NUMERIC:
                    _rstypes.push_back(DBI_TYPE_NUMERIC); break;

                case SQL_DECFLOAT:
                case SQL_FLOAT:
                case SQL_REAL:
                    _rstypes.push_back(DBI_TYPE_FLOAT); break;

                case SQL_TYPE_DATE:
                    _rstypes.push_back(DBI_TYPE_DATE); break;

                case SQL_DATETIME:
                case SQL_TYPE_TIMESTAMP:
                    _rstypes.push_back(DBI_TYPE_TIMESTAMP); break;

                case SQL_TYPE_TIME:
                    _rstypes.push_back(DBI_TYPE_TIME); break;

                case SQL_CHAR:
                    SQLColAttribute(stmt, i+1, SQL_DESC_LENGTH, 0, 0, 0, &size);
                    _rstypes.push_back(size > 1 ? DBI_TYPE_TEXT : DBI_TYPE_BOOLEAN);
                    break;
                /*
                case SQL_GRAPHIC:
                case SQL_LONGVARBINARY:
                case SQL_LONGVARCHAR:
                case SQL_LONGVARGRAPHIC:
                case SQL_VARGRAPHIC:
                case SQL_XML:
                case SQL_CLOB:
                case SQL_CLOB_LOCATOR:
                case SQL_DBCLOB:
                case SQL_DBCLOB_LOCATOR:
                case SQL_VARCHAR:
                */
                default:
                    _rstypes.push_back(DBI_TYPE_TEXT); break;
            }
        }
    }

    // TODO Speed up this sucker.
    //      1. Splitting text data into fields takes up ~15-20% of the total time.
    //      2. Probably use SQLExtendedBind to bind multiple values at once and call SQLExecute ?
    uint64_t DB2Statement::write(string table, FieldSet &fields, IO *io) {
        unsigned int loaded, rejected;
        db2LoadIn *loadInput;
        db2LoadStruct *loadInfo;
        struct sqldcol *dataInfo;

        string sql = "insert into " + table + "(" + fields.join(", ") + ") values (";
        for (int i = 0; i < fields.size()-1; i++) sql += "?, ";
        sql += "?)";

        SQLEndTran(SQL_HANDLE_DBC, handle, SQL_ROLLBACK);
        checkResult(SQLPrepare(stmt, (SQLCHAR*)sql.c_str(), SQL_NTS));

        loadInput = new db2LoadIn;
        loadInfo  = new db2LoadStruct;
        dataInfo  = new sqldcol;

        memset(loadInput, 0, sizeof(db2LoadIn));
        memset(loadInfo,  0, sizeof(db2LoadStruct));
        memset(dataInfo,  0, sizeof(sqldcol));

        loadInfo->piDataDescriptor  = dataInfo;
        loadInfo->piLoadInfoIn      = loadInput;

        loadInput->iRestartphase    = ' ';
        loadInput->iNonrecoverable  = SQLU_NON_RECOVERABLE_LOAD;
        loadInput->iStatsOpt        = (char)SQLU_STATS_NONE;
        loadInput->iCpuParallelism  = 2;

        dataInfo->dcolmeth          = SQL_METH_D;

        SQLSetStmtAttr(stmt, SQL_ATTR_USE_LOAD_API,         (SQLPOINTER) SQL_USE_LOAD_INSERT, 0);
        SQLSetStmtAttr(stmt, SQL_ATTR_LOAD_INFO,            (SQLPOINTER) loadInfo,            0);
        SQLSetStmtAttr(stmt, SQL_ATTR_LOAD_MODIFIED_BY,     (SQLPOINTER) "anyorder",          SQL_NTS);

        SQLSetStmtAttr(stmt, SQL_ATTR_LOAD_ROWS_LOADED_PTR,   &loaded,   SQL_IS_POINTER);
        SQLSetStmtAttr(stmt, SQL_ATTR_LOAD_ROWS_REJECTED_PTR, &rejected, SQL_IS_POINTER);

        uint64_t n, length;
        vector<Param> arguments, bind;
        char *start, *end, *terminal, *anchor;
        string value, remaining = "", stream;
        char *buffer = new char[2097152];
        while ((n = io->read(buffer, 2097152)) > 0) {
            stream    = remaining + string(buffer, n);
            remaining = "";
            length    = stream.length();
            anchor    = (char*)stream.data();
            start     = anchor;
            while (anchor - start < length) {
                terminal = (char *)memchr(anchor, '\n', length - (anchor - start));
                terminal = terminal ? terminal : start + length;
                if (
                    (end = (char*)memchr(anchor, '\t', terminal - anchor + 1)) ||
                    (end = (char*)memchr(anchor, '\n', terminal - anchor + 1))
                ) {
                    value = string(anchor, (end-anchor));
                    arguments.push_back(value == "\\N" ? PARAM(null()) : PARAM(value));
                    anchor = end + 1;
                }
                else {
                    remaining = string(anchor, terminal - anchor);
                    break;
                }
            }

            try {
                uint64_t idx, size = fields.size(), max = arguments.size();
                for (n = 0; n < max; n += size) {
                    for (idx = n; idx < n + size && idx < max; idx++)
                        bind.push_back(arguments[idx]);
                    if (bind.size() == size) {
                        processBindArguments(bind, size);
                        checkResult(SQLExecute(stmt));
                        bind.clear();
                    }
                }
                arguments = bind;
                bind.clear();
            }
            catch (RuntimeError &e) {
                SQLSetStmtAttr(stmt, SQL_ATTR_USE_LOAD_API, (SQLPOINTER) SQL_USE_LOAD_TERMINATE, 0);
                delete [] buffer;
                delete dataInfo;
                delete loadInfo;
                delete loadInput;
                throw e;
            }
        }

        SQLSetStmtAttr(stmt, SQL_ATTR_USE_LOAD_API, (SQLPOINTER) SQL_USE_LOAD_OFF, 0);

        delete [] buffer;
        delete dataInfo;
        delete loadInfo;
        delete loadInput;
        return loaded;
    }

    uint32_t DB2Statement::rows() {
        return _rows;
    }

    uint32_t DB2Statement::columns() {
        return _columns;
    }

    vector<string> DB2Statement::fields() {
        return _rsfields;
    }

    bool DB2Statement::read(ResultRow &r) {
        if (_rowno < _rows) {
            r = results[_rowno++];
            return true;
        }
        return false;
    }

    bool DB2Statement::read(ResultRowHash &r) {
        if (_rowno < _rows) {
            r.clear();
            for (int i = 0; i < _columns; i++)
                r[_rsfields[i]] = results[_rowno][i];
            _rowno++;
            return true;
        }
        return false;
    }

    unsigned char* DB2Statement::read(uint32_t r, uint32_t c, uint64_t *l) {
        if (r < _rows && c < _columns) {
            if (l) *l = results[r][c].value.length();
            return results[r][c].isnull ? 0 : (unsigned char*)results[r][c].value.data();
        }
        return 0;
    }

    bool DB2Statement::finish() {
        SQLCloseCursor(stmt);
        SQLFreeStmt(stmt, SQL_UNBIND);
        SQLFreeStmt(stmt, SQL_RESET_PARAMS);
        return true;
    }

    uint32_t DB2Statement::tell() {
        return _rowno;
    }

    void DB2Statement::seek(uint32_t r) {
        _rowno = r >= 0 && r < _rows ? r : _rowno;
    }

    void DB2Statement::rewind() {
        _rowno = 0;
    }

    vector<int>& DB2Statement::types() {
        return _rstypes;
    }

    void DB2Statement::prepareResult() {
        // NOP
    }

    // TODO Handle LOB locators
    bool DB2Statement::consumeResult() {
        int rc, type;
        ResultRow r;
        SQLLEN length;

        if (isasync) {
            if (isBusy()) return true;
            _rows   = 0;
            _rowno  = 0;
            isasync = false;
            SQLSetStmtAttr(stmt, SQL_ATTR_ASYNC_ENABLE, (void*)SQL_ASYNC_ENABLE_OFF, SQL_IS_INTEGER);
            fetchMeta();
        }

        rc = SQLFetch(stmt);
        if (rc == SQL_ERROR || rc == SQL_NO_DATA_FOUND)
            return false;

        unsigned char *buffer  = (unsigned char*)malloc(8192);
        uint64_t offset, buffer_length = 8192;

        while (rc != SQL_ERROR && rc != SQL_NO_DATA_FOUND) {
            r.clear();
            for (int i = 0; i < _columns; i++ ) {
                type = _rstypes[i] == DBI_TYPE_BLOB ? SQL_C_BINARY : SQL_C_CHAR;
                SQLGetData(stmt, i+1, type, buffer, buffer_length, &length);
                if (length > buffer_length) {
                    offset        = buffer_length;
                    buffer_length = length + 1;
                    buffer        = (unsigned char*)realloc(buffer, buffer_length);
                    SQLGetData(stmt, i+1, type, buffer+offset, buffer_length-offset, &length);
                    length        = buffer_length - 1;
                }
                r.push_back(
                    length == SQL_NULL_DATA ? PARAM(null()) :
                    type == SQL_C_BINARY ? PARAM_BINARY(buffer, length) : PARAM(buffer, length)
                );
            }
            results.push_back(r);
            _rows++;
            rc = SQLFetch(stmt);
        }

        finish();
        free(buffer);
        return false;
    }

    // NOTE
    // You can only call this after doing an insert with a select as below,
    // select id from final table (insert into ...)
    uint64_t DB2Statement::lastInsertID() {
        if (_rows > 0)
            return atol(results[0][0].value.c_str());
        return 0;
    }

    uint32_t DB2Statement::execute() {
        SQLINTEGER cmdrows = 0;
        _rows = _rowno = 0;
        results.clear();
        checkResult(SQLExecute(stmt));
        SQLRowCount(stmt, &cmdrows);
        consumeResult();
        return cmdrows > 0 ? cmdrows : _rows;
    }

    // one shot query.
    uint32_t DB2Statement::execute(string sql) {
        SQLINTEGER cmdrows = 0;
        this->sql = sql;
        _rows = _rowno = 0;
        results.clear();
        checkResult(SQLExecDirect(stmt, (SQLCHAR*)sql.c_str(), sql.length()));
        fetchMeta();
        SQLRowCount(stmt, &cmdrows);
        consumeResult();
        SQLFreeHandle(SQL_HANDLE_STMT, stmt);
        SQLAllocHandle(SQL_HANDLE_STMT, handle, &stmt);
        return cmdrows > 0 ? cmdrows : _rows;
    }

    bool DB2Statement::cancel() {
        checkResult(SQLCancel(stmt));
        return true;
    }

    void DB2Statement::processBindArguments(vector<Param> &bind, int size) {
        void *dataptr;
        int i, ctype, sqltype;
        SQLLEN *indicator;

        size = size > 0 ? size : bind.size();
        for (i = 0; i < size; i++) {
            ctype     = SQL_C_CHAR;
            sqltype   = SQL_VARCHAR;
            dataptr   = (void*)bind[i].value.data();
            indicator = (SQLLEN*)&bind[i].length;

            if (bind[i].isnull) {
                ctype     = SQL_C_DEFAULT;
                indicator = &DB2_NULL_INDICATOR;
            }
            else if (bind[i].binary) {
                ctype   = SQL_C_BINARY;
                sqltype = SQL_BLOB;
            }
            checkResult(SQLBindParameter(stmt, i+1, SQL_PARAM_INPUT, ctype, sqltype, 20, 8, dataptr, 0, indicator));
        }
    }

    uint32_t DB2Statement::execute(vector<Param> &bind) {
        processBindArguments(bind);
        return execute();
    }

    uint32_t DB2Statement::execute(string sql, vector<Param> &bind) {
        processBindArguments(bind);
        return execute(sql);
    }

    void DB2Statement::aexecute(string sql, vector<Param> &bind) {
        int rc;
        this->sql     = sql;
        this->isasync = true;
        processBindArguments(bind);
        SQLSetStmtAttr(stmt, SQL_ATTR_ASYNC_ENABLE, (void*)SQL_ASYNC_ENABLE_ON, SQL_IS_INTEGER);
        rc = SQLExecDirect(stmt, (SQLCHAR*)sql.c_str(), sql.length());
        if (rc != SQL_STILL_EXECUTING) checkResult(rc);
    }

    bool DB2Statement::isBusy() {
        if (isasync) {
            int rc = SQLExecDirect(stmt, (SQLCHAR*)sql.c_str(), sql.length());
            if (rc != SQL_STILL_EXECUTING) checkResult(rc);
            return rc == SQL_STILL_EXECUTING;
        }

        throw RuntimeError("Invalid call. Did you execute an async query first ?");
    }

    void DB2Statement::cleanup() {
        if (stmt)
            SQLFreeHandle(SQL_HANDLE_STMT, stmt);
        stmt = 0;
    }

    DB2Statement::~DB2Statement() {
        cleanup();
    }

    string DB2Statement::command() {
        return sql;
    }


    // -----------------------------------------------------------------------------
    // DB2Handle
    // -----------------------------------------------------------------------------

    void DB2Handle::checkResult(int rc) {
        CHECK_HANDLE_RESULT(handle, rc);
    }

    // TODO error checks.
    void DB2Handle::setup() {
        stmt       = 0;
        tr_nesting = 0;
        SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &env);
        SQLSetEnvAttr(env, SQL_ATTR_ODBC_VERSION, (void *)SQL_OV_ODBC3, 0);
        SQLAllocHandle(SQL_HANDLE_DBC, env, &handle);
    }

    void DB2Handle::TCPIPConnect(string user, string pass, string dbname, string host, string port) {
        string dsn = "DRIVER={IBM DB2 ODBC DRIVER};DSN=" + dbname + ";DATABASE=" + dbname +
                     ";HOSTNAME=" + host + ";PORT=" + port + ";PROTOCOL=TCPIP;";

        if (user != "")
            dsn += "UID=" + user + ";";
        if (pass != "")
            dsn += "PWD=" + pass + ";";

        int rc = SQLDriverConnect(
            handle, 0,
            (SQLCHAR*)dsn.c_str(),    SQL_NTS,
            0, 0, 0, SQL_DRIVER_NOPROMPT
        );
        checkResult(rc);
    }

    DB2Handle::DB2Handle(string user, string pass, string dbname) {
        setup();

        if (user == string(getlogin()) && pass.length() == 0)
            user = "";

        int rc = SQLConnect(
            handle,
            (SQLCHAR*)dbname.c_str(), SQL_NTS,
            (SQLCHAR*)user.c_str(),   SQL_NTS,
            (SQLCHAR*)pass.c_str(),   SQL_NTS
        );
        checkResult(rc);
        SQLSetConnectAttr(handle, SQL_ATTR_AUTOCOMMIT, (SQLPOINTER)SQL_AUTOCOMMIT_ON, SQL_NTS);
    }

    DB2Handle::DB2Handle(string user, string pass, string dbname, string host, string port) {
        setup();
        TCPIPConnect(user, pass, dbname, host, port);
        SQLSetConnectAttr(handle, SQL_ATTR_AUTOCOMMIT, (SQLPOINTER)SQL_AUTOCOMMIT_ON, SQL_NTS);

        socketfd = -1;

        // TODO use inotify to monitor /proc/<pid>/fd
        // Figure out socket descriptor - this is hit and miss.
        SocketInfo info(host, atoi(port.c_str()), getpid());
        vector<ProcNet*> results = info.search();
        if (results.size()) {
            for (int i = 0; i < results.size(); i++) {
                if (OPENSOCKETS[results[i]->fd]) continue;
                socketfd = results[i]->fd;
                OPENSOCKETS[socketfd] = true;
                break;
            }
        }

        if (socketfd < 3 && _trace)
            logMessage(_trace_fd,
                "WARNING: Unable to determine socket fd, async apis will not work correctly.");
    }

    DB2Statement* DB2Handle::prepare(string sql) {
        return new DB2Statement(handle, sql);
    }

    uint32_t DB2Handle::execute(string sql) {
        if (!stmt) stmt = new DB2Statement(handle);
        return stmt->execute(sql);
    }

    uint32_t DB2Handle::execute(string sql, vector<Param> &bind) {
        if (!stmt) stmt = new DB2Statement(handle);
        return stmt->execute(sql, bind);
    }

    void DB2Handle::cleanup() {
        if (env) {
            if (stmt) { stmt->cleanup(); delete stmt; }
            SQLDisconnect(handle);
            SQLFreeHandle(SQL_HANDLE_DBC, handle);
            SQLFreeHandle(SQL_HANDLE_ENV, env);
        }
        env    = 0;
        stmt   = 0;
        handle = 0;
    }

    DB2Handle::~DB2Handle() {
        cleanup();
    }

    bool DB2Handle::close() {
        cleanup();
        return true;
    }

    string DB2Handle::escape(string value) {
        SQLINTEGER length;
        SQLCHAR *buffer = (SQLCHAR*)malloc(value.length()*2+1);
        SQLNativeSql(handle, (SQLCHAR*)value.data(), value.length(), buffer, value.length()*2, &length);
        value = string((char*)buffer, length);
        free(buffer);
        return value;
    }

    uint64_t DB2Handle::write(string table, FieldSet &fields, IO* io) {
        if (!stmt) stmt = new DB2Statement(handle);
        return stmt->write(table, fields, io);
    }

    bool DB2Handle::begin() {
        SQLEndTran(SQL_HANDLE_DBC, handle, SQL_ROLLBACK);
        checkResult(SQLSetConnectAttr(handle, SQL_ATTR_AUTOCOMMIT, (SQLPOINTER)SQL_AUTOCOMMIT_OFF, SQL_NTS));
        tr_nesting = 1;
        return true;
    }

    bool DB2Handle::rollback() {
        SQLEndTran(SQL_HANDLE_DBC, handle, SQL_ROLLBACK);
        SQLSetConnectAttr(handle, SQL_ATTR_AUTOCOMMIT, (SQLPOINTER)SQL_AUTOCOMMIT_ON, SQL_NTS);
        tr_nesting = 0;
        return true;
    }

    bool DB2Handle::commit() {
        SQLEndTran(SQL_HANDLE_DBC, handle, SQL_COMMIT);
        SQLSetConnectAttr(handle, SQL_ATTR_AUTOCOMMIT, (SQLPOINTER)SQL_AUTOCOMMIT_ON, SQL_NTS);
        tr_nesting = 0;
        return true;
    }

    bool DB2Handle::begin(string name) {
        if (tr_nesting == 0) {
            begin();
            tr_nesting = 0;
        }
        if (!stmt) stmt = new DB2Statement(handle);
        stmt->execute("SAVEPOINT " + name + " ON ROLLBACK RETAIN CURSORS");
        tr_nesting++;
        return true;
    }

    bool DB2Handle::rollback(string name) {
        if (!stmt) stmt = new DB2Statement(handle);
        stmt->execute("ROLLBACK TO SAVEPOINT " + name);
        tr_nesting--;
        if (tr_nesting == 0) rollback();
        return true;
    }

    bool DB2Handle::commit(string name) {
        if (!stmt) stmt = new DB2Statement(handle);
        stmt->execute("RELEASE SAVEPOINT " + name);
        tr_nesting--;
        if (tr_nesting == 0) commit();
        return true;
    }

    bool DB2Handle::cancel() {
        if (stmt)
            return stmt->cancel();
    }

    // NOT IMPLEMENTED
    void* DB2Handle::call(string name, void* args, uint64_t len) {
        return 0;
    }

    // NOT SUPPORTED - DB2 runs on system timezone.
    void DB2Handle::setTimeZoneOffset(int hour, int min) {
        throw RuntimeError("DB2 unsupported API setTimeZoneOffset");
    }

    // NOT SUPPORTED - DB2 runs on system timezone.
    void DB2Handle::setTimeZone(char *name) {
        throw RuntimeError("DB2 unsupported API setTimeZone");
    }

    AbstractResult* DB2Handle::results() {
        return stmt;
    }

    string DB2Handle::driver() {
        return DRIVER_NAME;
    }

    int DB2Handle::socket() {
        return socketfd;
    }

    AbstractResult* DB2Handle::aexecute(string sql, vector<Param> &bind) {
        DB2Statement* async_stmt = new DB2Statement(handle);
        async_stmt->aexecute(sql, bind);
        return (AbstractResult*)async_stmt;
    }

    void DB2Handle::initAsync() {
        // NOP
    }

    bool DB2Handle::isBusy() {
        if (stmt) return stmt->isBusy();
    }
};

using namespace dbi;
using namespace std;

extern "C" {
    DB2Handle* dbdConnect(string user, string pass, string dbname, string host, string port) {
        if (host == "")
            return new DB2Handle(user, pass, dbname);
        else {
            if (port == "") port = "50000";
            return new DB2Handle(user, pass, dbname, host, port);
        }
    }

    Driver* dbdInfo(void) {
        Driver *driver  = new Driver;
        driver->name    = DRIVER_NAME;
        driver->version = DRIVER_VERSION;
        return driver;
    }
}

#ifndef _DBICXX_PG_RESULT_H
#define _DBICXX_PG_RESULT_H

namespace dbi {
    class PgHandle;

    class PgResult : public AbstractResult {

        private:
        PGconn         *_conn;
        PGresult       *_result;
        string_list_t  _rsfields;
        int_list_t     _rstypes;
        uint32_t       _rowno, _rows, _cols, _affected_rows;
        unsigned char  *_bytea;
        string         _sql;

        void init();
        void fetchMeta();
        unsigned char* unescapeBytea(int, int, uint64_t*);

        public:
        PgResult(PGresult*, string sql, PGconn*);
        ~PgResult();

        void cleanup();
        bool consumeResult();
        void prepareResult();
        void boom(const char *);

        uint32_t        rows();
        uint32_t        columns();
        uint64_t        lastInsertID();
        string_list_t&  fields();
        int_list_t&     types();

        bool           read(ResultRow &r);
        bool           read(ResultRowHash &r);
        unsigned char* read(uint32_t r, uint32_t c, uint64_t *l = 0);

        bool     finish();
        uint32_t tell();
        void     rewind();
        void     seek(uint32_t);
    };
}

#endif

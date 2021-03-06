#pragma once

namespace dbi {

    using namespace std;
    using namespace pcrecpp;

    /*
        Class: IO
        Abstract dbic++ IO class for direct read and write operations on table.
        This is used by Handle::write() at this time to write rows directly to
        a table via the underlying database bulk loader api.

        Note:
        Row data needs to be '\t' delimited and terminated by '\n'.
        Databases will usually throw an exception when given CRLF terminated row data,
        so suffice to say don't use CRLF terminator.
    */
    class IO {
        public:
        virtual string&  read(void) = 0;
        virtual uint32_t read(char *buffer, uint32_t) = 0;

        /*
            Function: write(const char*)
            Appends the given '\0' terminated string to the io object data.

            Parameters:
            data - char pointer to string data (you cannot use this api to load blob)
        */
        virtual void write(const char *data) = 0;

        /*
            Function: write(const char*, uint64_t)
            Appends the given data to the io object.

            Parameters:
            data - char pointer to string data (you cannot use this api to load blob)
            len  - uint64_t value specifying the length.
        */
        virtual void write(const char *data, uint64_t len) = 0;

        /*
            Function: truncate
            Truncates existing data in io object.
        */
        virtual void truncate() = 0;

        virtual bool  readline(string &) = 0;
        virtual char* readline() = 0;
    };

}


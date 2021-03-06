#pragma once

namespace dbi {

    /*
        Class: AbstractStatement
        Pure virtual class that defines the api that individual database drivers need to support.
        Use this as a reference only. It is recommended to use the Statement class for any real work.
    */
    class AbstractStatement {
        public:
        /*
            Function: command
            Returns the SQL statement attached to the statement.
        */
        virtual string command() = 0;

        /*
            Function: execute
            Executes a SQL.

            Parameters:
            sql - SQL without placeholders.

            Returns:
            rows - number of rows affected or returned.
        */
        virtual uint32_t execute() = 0;

        /*
            Function: execute(param_list_t&)
            executes a SQL with bind values.

            Parameters:
            sql  - SQL with or without placeholders.
            bind - param_list_t that contains bind values. Refer to the <Param> struct and associated methods.

            Returns:
            rows - number of rows affected or returned.
        */
        virtual uint32_t execute(param_list_t &bind) = 0;

        /*
            Function: result
            Returns a pointer to a result object. This needs to be
            deallocated explicitly.

            Returns:
            AbstractResult* - Pointer to the Result set object.
        */
        virtual AbstractResult* result() = 0;

        /*
            Function: lastInsertID
            See <AbstractResult::lastInsertID()>
        */
        virtual uint64_t lastInsertID() = 0;

        virtual void finish()  = 0;
        virtual void cleanup() = 0;

        virtual ~AbstractStatement() {}
    };
}

// file      : odb/mssql/statement.cxx
// copyright : Copyright (c) 2005-2013 Code Synthesis Tools CC
// license   : ODB NCUEL; see accompanying LICENSE file

#include <cstring> // std::strlen, std::strstr
#include <cassert>

#include <odb/tracer.hxx>

#include <odb/mssql/mssql.hxx>
#include <odb/mssql/database.hxx>
#include <odb/mssql/connection.hxx>
#include <odb/mssql/statement.hxx>
#include <odb/mssql/exceptions.hxx>
#include <odb/mssql/error.hxx>

using namespace std;

namespace odb
{
  namespace mssql
  {
    // Mapping of bind::buffer_type to SQL_* SQL types.
    //
    static const SQLSMALLINT sql_type_lookup [bind::last] =
    {
      SQL_BIT,                // bind::bit
      SQL_TINYINT,            // bind::tinyint
      SQL_SMALLINT,           // bind::smallint
      SQL_INTEGER,            // bind::integer
      SQL_BIGINT,             // bind::bigint

      SQL_DECIMAL,            // bind::decimal
      SQL_DECIMAL,            // bind::smallmoney
      SQL_DECIMAL,            // bind::money

      SQL_FLOAT,              // bind::float4
      SQL_FLOAT,              // bind::float8

      SQL_VARCHAR,            // bind::string
      SQL_VARCHAR,            // bind::long_string

      SQL_WVARCHAR,           // bind::nstring
      SQL_WVARCHAR,           // bind::long_nstring

      SQL_VARBINARY,          // bind::binary
      SQL_VARBINARY,          // bind::long_binary

      SQL_TYPE_DATE,          // bind::date
      SQL_SS_TIME2,           // bind::time
      SQL_TYPE_TIMESTAMP,     // bind::datetime
      SQL_SS_TIMESTAMPOFFSET, // bind::datetimeoffset

      SQL_GUID,               // bind::uniqueidentifier
      SQL_BINARY              // bind::rowversion
    };

    // Mapping of bind::buffer_type to SQL_C_* C types.
    //
    static const SQLSMALLINT c_type_lookup [bind::last] =
    {
      SQL_C_BIT,            // bind::bit
      SQL_C_UTINYINT,       // bind::tinyint
      SQL_C_SSHORT,         // bind::smallint
      SQL_C_SLONG,          // bind::integer
      SQL_C_SBIGINT,        // bind::bigint

      SQL_C_NUMERIC,        // bind::decimal
      SQL_C_BINARY,         // bind::smallmoney
      SQL_C_BINARY,         // bind::money

      SQL_C_FLOAT,          // bind::float4
      SQL_C_DOUBLE,         // bind::float8

      SQL_C_CHAR,           // bind::string
      SQL_C_CHAR,           // bind::long_string

      SQL_C_WCHAR,          // bind::nstring
      SQL_C_WCHAR,          // bind::long_nstring

      SQL_C_BINARY,         // bind::binary
      SQL_C_BINARY,         // bind::long_binary

      SQL_C_TYPE_DATE,      // bind::date
      SQL_C_BINARY,         // bind::time
      SQL_C_TYPE_TIMESTAMP, // bind::datetime
      SQL_C_BINARY,         // bind::datetimeoffset

      SQL_C_GUID,           // bind::uniqueidentifier
      SQL_C_BINARY          // bind::rowversion
    };

    //
    // statement
    //

    statement::
    statement (connection_type& conn,
               const string& text,
               statement_kind sk,
               const binding* process,
               bool optimize)
        : conn_ (conn)
    {
      if (process == 0)
      {
        text_copy_ = text;
        text_ = text_copy_.c_str ();
      }
      else
        text_ = text.c_str (); // Temporary, see init().

      init (text.size (), sk, process, optimize);
    }

    statement::
    statement (connection_type& conn,
               const char* text,
               statement_kind sk,
               const binding* process,
               bool optimize,
               bool copy)
        : conn_ (conn)
    {
      size_t n;

      if (process == 0 && copy)
      {
        text_copy_ = text;
        text_ = text_copy_.c_str ();
        n = text_copy_.size ();
      }
      else
      {
        text_ = text;
        n = strlen (text_); // Potentially temporary, see init().
      }

      init (n, sk, process, optimize);
    }

    void statement::
    init (size_t text_size,
          statement_kind sk,
          const binding* proc,
          bool optimize)
    {
      if (proc != 0)
      {
        switch (sk)
        {
        case statement_select:
          process_select (text_,
                          proc->bind, proc->count,
                          optimize,
                          text_copy_);
          break;
        case statement_insert:
          process_insert (text_,
                          &proc->bind->buffer, proc->count, sizeof (bind),
                          '?',
                          text_copy_);
          break;
        case statement_update:
          process_update (text_,
                          &proc->bind->buffer, proc->count, sizeof (bind),
                          '?',
                          text_copy_);
          break;
        case statement_delete:
          assert (false);
        }

        text_ = text_copy_.c_str ();
        text_size = text_copy_.size ();
      }

      // Empty statement.
      //
      if (*text_ == '\0')
        return;

      SQLRETURN r;

      // Allocate the handle.
      //
      {
        SQLHANDLE h;
        r = SQLAllocHandle (SQL_HANDLE_STMT, conn_.handle (), &h);

        if (!SQL_SUCCEEDED (r))
          translate_error (r, conn_);

        stmt_.reset (h);
      }

      // Disable escape sequences.
      //
      r = SQLSetStmtAttr (stmt_,
                          SQL_ATTR_NOSCAN,
                          (SQLPOINTER) SQL_NOSCAN_OFF,
                          0);

      if (!SQL_SUCCEEDED (r))
        translate_error (r, conn_, stmt_);

      {
        odb::tracer* t;
        if ((t = conn_.transaction_tracer ()) ||
            (t = conn_.tracer ()) ||
            (t = conn_.database ().tracer ()))
          t->prepare (conn_, *this);
      }

      // Prepare the statement.
      //
      r = SQLPrepareA (stmt_, (SQLCHAR*) text_, (SQLINTEGER) text_size);

      if (!SQL_SUCCEEDED (r))
        translate_error (r, conn_, stmt_);
    }

    statement::
    ~statement ()
    {
      if (stmt_ != 0)
      {
        odb::tracer* t;
        if ((t = conn_.transaction_tracer ()) ||
            (t = conn_.tracer ()) ||
            (t = conn_.database ().tracer ()))
          t->deallocate (conn_, *this);
      }
    }

    const char* statement::
    text () const
    {
      return text_;
    }

    void statement::
    bind_param (bind* b, size_t n)
    {
      SQLRETURN r;

      SQLUSMALLINT i (0);
      for (bind* end (b + n); b != end; ++b)
      {
        if (b->buffer == 0) // Skip NULL entries.
          continue;

        i++; // Column index is 1-based.

        SQLULEN col_size (0);
        SQLSMALLINT digits (0);
        SQLPOINTER buf;

        switch (b->type)
        {
        case bind::decimal:
          {
            buf = (SQLPOINTER) b->buffer;
            col_size = (SQLULEN) (b->capacity / 100);   // precision
            digits = (SQLSMALLINT) (b->capacity % 100); // scale
            break;
          }
        case bind::smallmoney:
          {
            buf = (SQLPOINTER) b->buffer;
            col_size = 10;
            digits = 4;
            break;
          }
        case bind::money:
          {
            buf = (SQLPOINTER) b->buffer;
            col_size = 19;
            digits = 4;
            break;
          }
        case bind::float4:
        case bind::float8:
          {
            buf = (SQLPOINTER) b->buffer;
            col_size = (SQLULEN) b->capacity; // precision
            break;
          }
        case bind::long_string:
        case bind::long_binary:
          {
            buf = (SQLPOINTER) b;
            col_size = b->capacity != 0
              ? (SQLULEN) b->capacity
              : SQL_SS_LENGTH_UNLIMITED;
            break;
          }
        case bind::long_nstring:
          {
            buf = (SQLPOINTER) b;
            col_size = b->capacity != 0
              ? (SQLULEN) b->capacity / 2 // In characters, not bytes.
              : SQL_SS_LENGTH_UNLIMITED;
            break;
          }
        case bind::string:
          {
            buf = (SQLPOINTER) b->buffer;
            col_size = b->capacity != 0
              ? (SQLULEN) b->capacity - 1 // Sans the null-terminator.
              : SQL_SS_LENGTH_UNLIMITED;
            break;
          }
        case bind::binary:
          {
            buf = (SQLPOINTER) b->buffer;
            col_size = b->capacity != 0
              ? (SQLULEN) b->capacity
              : SQL_SS_LENGTH_UNLIMITED;
            break;
          }
        case bind::nstring:
          {
            buf = (SQLPOINTER) b->buffer;
            col_size = b->capacity != 0
              // In characters, not bytes, and sans the null-terminator.
              ? (SQLULEN) (b->capacity / 2 - 1)
              : SQL_SS_LENGTH_UNLIMITED;
            break;
          }
        case bind::date:
          {
            buf = (SQLPOINTER) b->buffer;
            // Native Client 10.0 requires the correct precision.
            //
            col_size = 10;
            break;
          }
        case bind::time:
          {
            buf = (SQLPOINTER) b->buffer;
            digits = (SQLSMALLINT) b->capacity;

            // Native Client 10.0 requires the correct precision.
            //
            if (digits == 0)
              col_size = 8;
            else
              col_size = (SQLULEN) (digits + 9);

            break;
          }
        case bind::datetime:
          {
            buf = (SQLPOINTER) b->buffer;
            digits = (SQLSMALLINT) b->capacity;

            // Native Client 10.0 requires the correct precision.
            //
            if (digits == 0)
              col_size = 19;
            else if (digits == 8)
            {
              // This is a SMALLDATETIME column which only has the minutes
              // precision. Documentation indicates that the correct numeric
              // precision value for this type is 16.
              //
              digits = 0;
              col_size = 16;
            }
            else
              col_size = (SQLULEN) (digits + 20);

            break;
          }
        case bind::datetimeoffset:
          {
            buf = (SQLPOINTER) b->buffer;
            digits = (SQLSMALLINT) b->capacity;

            // Native Client 10.0 requires the correct precision.
            //
            if (digits == 0)
              col_size = 26;
            else
              col_size = (SQLULEN) (digits + 27);

            break;
          }
        case bind::rowversion:
          {
            buf = (SQLPOINTER) b->buffer;
            col_size = 8;
            break;
          }
        default:
          {
            buf = (SQLPOINTER) b->buffer;
            break;
          }
        }

        r = SQLBindParameter (
          stmt_,
          i,
          SQL_PARAM_INPUT,
          c_type_lookup[b->type],
          sql_type_lookup[b->type],
          col_size,
          digits,
          buf,
          0, // buffer capacity (shouldn't be needed for input parameters)
          b->size_ind);

        if (!SQL_SUCCEEDED (r))
          translate_error (r, conn_, stmt_);
      }
    }

    SQLUSMALLINT statement::
    bind_result (bind* b, size_t n, SQLUSMALLINT& long_count)
    {
      long_count = 0;
      SQLRETURN r;

      SQLUSMALLINT i (0);
      for (bind* end (b + n); b != end; ++b)
      {
        if (b->buffer == 0) // Skip NULL entries.
          continue;

        SQLLEN cap (0);

        switch (b->type)
        {
        case bind::bit:
        case bind::tinyint:
          {
            cap = 1;
            break;
          }
        case bind::smallint:
          {
            cap = 2;
            break;
          }
        case bind::int_:
          {
            cap = 4;
            break;
          }
        case bind::bigint:
          {
            cap = 8;
            break;
          }
        case bind::decimal:
          {
            cap = (SQLLEN) sizeof (decimal);
            break;
          }
        case bind::smallmoney:
          {
            cap = 4;
            break;
          }
        case bind::money:
          {
            cap = 8;
            break;
          }
        case bind::float4:
          {
            cap = 4;
            break;
          }
        case bind::float8:
          {
            cap = 8;
            break;
          }
        case bind::string:
        case bind::nstring:
        case bind::binary:
          {
            cap = b->capacity;
            break;
          }
        case bind::long_string:
        case bind::long_nstring:
        case bind::long_binary:
          {
            // Long data is not bound.
            //
            long_count++;
            continue;
          }
        case bind::date:
          {
            cap = (SQLLEN) sizeof (date);
            break;
          }
        case bind::time:
          {
            cap = (SQLLEN) sizeof (time);
            break;
          }
        case bind::datetime:
          {
            cap = (SQLLEN) sizeof (datetime);
            break;
          }
        case bind::datetimeoffset:
          {
            cap = (SQLLEN) sizeof (datetimeoffset);
            break;
          }
        case bind::uniqueidentifier:
          {
            cap = 16;
            break;
          }
        case bind::rowversion:
          {
            cap = 8;
            break;
          }
        case bind::last:
          {
            assert (false);
            break;
          }
        }

        r = SQLBindCol (stmt_,
                        ++i, // Column index is 1-based.
                        c_type_lookup[b->type],
                        (SQLPOINTER) b->buffer,
                        cap,
                        b->size_ind);

        if (!SQL_SUCCEEDED (r))
          translate_error (r, conn_, stmt_);
      }

      return i;
    }

    SQLRETURN statement::
    execute ()
    {
      {
        odb::tracer* t;
        if ((t = conn_.transaction_tracer ()) ||
            (t = conn_.tracer ()) ||
            (t = conn_.database ().tracer ()))
          t->execute (conn_, *this);
      }

      SQLRETURN r (SQLExecute (stmt_));

      if (r == SQL_NEED_DATA)
      {
        details::buffer& tmp_buf (conn_.long_data_buffer ());

        if (tmp_buf.capacity () == 0)
          tmp_buf.capacity (4096);

        bind* b;
        for (;;)
        {
          r = SQLParamData (stmt_, (SQLPOINTER*) &b);

          // If we get anything other than SQL_NEED_DATA, then this is
          // the return code of SQLExecute().
          //
          if (r != SQL_NEED_DATA)
            break;

          long_callback& cb (*static_cast<long_callback*> (b->buffer));

          // Store the pointer to the long_callback struct in buf on the
          // first call to the callback. This allows the callback to
          // redirect further calls to some other callback.
          //
          const void* buf (&cb);

          size_t position (0);
          for (;;)
          {
            size_t n;
            chunk_type chunk;

            cb.callback.param (
              cb.context.param,
              &position,
              &buf,
              &n,
              &chunk,
              tmp_buf.data (),
              tmp_buf.capacity ());

            r = SQLPutData (
              stmt_,
              (SQLPOINTER) (buf != 0 ? buf : &buf), // Always pass non-NULL.
              chunk != chunk_null ? (SQLLEN) n : SQL_NULL_DATA);

            if (!SQL_SUCCEEDED (r))
              translate_error (r, conn_, stmt_);

            if (chunk == chunk_one ||
                chunk == chunk_last ||
                chunk == chunk_null)
              break;
          }
        }
      }

      return r;
    }

    void statement::
    stream_result (SQLUSMALLINT i, bind* b, size_t n, void* obase, void* nbase)
    {
      details::buffer& tmp_buf (conn_.long_data_buffer ());

      if (tmp_buf.capacity () == 0)
        tmp_buf.capacity (4096);

      SQLRETURN r;

      for (bind* end (b + n); b != end; ++b)
      {
        if (b->buffer == 0) // Skip NULL entries.
          continue;

        bool char_data;
        switch (b->type)
        {
        case bind::long_string:
        case bind::long_nstring:
          {
            char_data = true;
            break;
          }
        case bind::long_binary:
          {
            char_data = false;
            break;
          }
        default:
          {
            continue; // Not long data.
          }
        }

        void* cbp;

        if (obase == 0)
          cbp = b->buffer;
        else
        {
          // Re-base the pointer.
          //
          char* p (static_cast<char*> (b->buffer));
          char* ob (static_cast<char*> (obase));
          char* nb (static_cast<char*> (nbase));

          assert (ob <= p);
          cbp = nb + (p - ob);
        }

        long_callback& cb (*static_cast<long_callback*> (cbp));

        // First determine if the value is NULL as well as try to
        // get the total data size.
        //
        SQLLEN si;
        r = SQLGetData (stmt_,
                        ++i,
                        c_type_lookup[b->type],
                        tmp_buf.data (), // Dummy value.
                        0,
                        &si);

        if (!SQL_SUCCEEDED (r))
          translate_error (r, conn_, stmt_);

        // Store the pointer to the long_callback struct in buf on the
        // first call to the callback. This allows the callback to
        // redirect further calls to some other callback.
        //
        void* buf (&cb);
        size_t size (0);
        size_t position (0);
        size_t size_left (si == SQL_NO_TOTAL ? 0 : static_cast<size_t> (si));

        chunk_type c (si == SQL_NULL_DATA
                      ? chunk_null
                      : (si == 0 ? chunk_one : chunk_first));

        for (;;)
        {
          cb.callback.result (
                cb.context.result,
                &position,
                &buf,
                &size,
                c,
                size_left,
                tmp_buf.data (),
                tmp_buf.capacity ());

          if (c == chunk_last || c == chunk_one || c == chunk_null)
            break;

          // SQLGetData() can keep returning SQL_SUCCESS_WITH_INFO (truncated)
          // with SQL_NO_TOTAL for all the calls except the last one. For the
          // last call we should get SQL_SUCCESS and the size_indicator should
          // contain a valid value.
          //
          r = SQLGetData (stmt_,
                          i,
                          c_type_lookup[b->type],
                          (SQLPOINTER) buf,
                          (SQLLEN) size,
                          &si);

          if (r == SQL_SUCCESS)
          {
            assert (si != SQL_NO_TOTAL);

            // Actual amount of data copied to the buffer (appears not to
            // include the NULL terminator).
            //
            size = static_cast<size_t> (si);
            c = chunk_last;
          }
          else if (r == SQL_SUCCESS_WITH_INFO)
          {
            if (char_data)
              size--; // NULL terminator.

            c = chunk_next;
          }
          else
            translate_error (r, conn_, stmt_);

          // Update the total.
          //
          if (size_left != 0)
            size_left -= size;
        }
      }
    }

    //
    // select_statement
    //
    select_statement::
    ~select_statement ()
    {
    }

    select_statement::
    select_statement (connection_type& conn,
                      const string& text,
                      bool process,
                      bool optimize,
                      binding& param,
                      binding& result)
        : statement (conn,
                     text, statement_select,
                     (process ? &result : 0), optimize),
          result_ (result)
    {
      if (!empty ())
      {
        bind_param (param.bind, param.count);
        result_count_ = bind_result (result.bind, result.count, long_count_);
      }
    }

    select_statement::
    select_statement (connection_type& conn,
                      const char* text,
                      bool process,
                      bool optimize,
                      binding& param,
                      binding& result,
                      bool copy_text)
        : statement (conn,
                     text, statement_select,
                     (process ? &result : 0), optimize,
                     copy_text),
          result_ (result)
    {
      if (!empty ())
      {
        bind_param (param.bind, param.count);
        result_count_ = bind_result (result.bind, result.count, long_count_);
      }
    }

    select_statement::
    select_statement (connection_type& conn,
                      const string& text,
                      bool process,
                      bool optimize,
                      binding& result)
        : statement (conn,
                     text, statement_select,
                     (process ? &result : 0), optimize),
          result_ (result)
    {
      if (!empty ())
        result_count_ = bind_result (result.bind, result.count, long_count_);
    }

    select_statement::
    select_statement (connection_type& conn,
                      const char* text,
                      bool process,
                      bool optimize,
                      binding& result,
                      bool copy_text)
        : statement (conn,
                     text, statement_select,
                     (process ? &result : 0), optimize,
                     copy_text),
          result_ (result)
    {
      if (!empty ())
        result_count_ = bind_result (result.bind, result.count, long_count_);
    }

    void select_statement::
    execute ()
    {
      SQLRETURN r (statement::execute ());

      if (!SQL_SUCCEEDED (r))
        translate_error (r, conn_, stmt_);

#ifndef NDEBUG
      SQLSMALLINT cols;
      r = SQLNumResultCols (stmt_, &cols);

      if (!SQL_SUCCEEDED (r))
        translate_error (r, conn_, stmt_);

      // Make sure that the number of columns in the result returned by
      // the database matches the number that we expect. A common cause
      // of this assertion is a native view with a number of data members
      // not matching the number of columns in the SELECT-list.
      //
      assert (static_cast<SQLUSMALLINT> (cols) == result_count_ + long_count_);
#endif
    }

    select_statement::result select_statement::
    fetch ()
    {
      change_callback* cc (result_.change_callback);

      if (cc != 0 && cc->callback != 0)
        (cc->callback) (cc->context);

      SQLRETURN r (SQLFetch (stmt_));

      if (r == SQL_NO_DATA)
        return no_data;

      if (!SQL_SUCCEEDED (r))
        translate_error (r, conn_, stmt_);

      return success;
    }

    void select_statement::
    free_result ()
    {
      // Use SQLFreeStmt(SQL_CLOSE) instead of SQLCloseCursor() to avoid an
      // error if a cursor is already closed. This can happens, for example,
      // if we are trying to close the cursor after the transaction has been
      // committed (e.g., when destroying the query result) which also closes
      // the cursor.
      //
      SQLRETURN r (SQLFreeStmt (stmt_, SQL_CLOSE));

      if (!SQL_SUCCEEDED (r))
        translate_error (r, conn_, stmt_);
    }

    //
    // insert_statement
    //

    insert_statement::
    ~insert_statement ()
    {
    }

    insert_statement::
    insert_statement (connection_type& conn,
                      const string& text,
                      bool process,
                      binding& param,
                      bool returning_id,
                      bool returning_version)
        : statement (conn,
                     text, statement_insert,
                     (process ? &param : 0), false),
          returning_id_ (returning_id),
          returning_version_ (returning_version)
    {
      bind_param (param.bind, param.count);

      if (returning_id_ || returning_version_)
        init_result ();
    }

    insert_statement::
    insert_statement (connection_type& conn,
                      const char* text,
                      bool process,
                      binding& param,
                      bool returning_id,
                      bool returning_version,
                      bool copy_text)
        : statement (conn,
                     text, statement_insert,
                     (process ? &param : 0), false,
                     copy_text),
          returning_id_ (returning_id),
          returning_version_ (returning_version)
    {
      bind_param (param.bind, param.count);

      if (returning_id_ || returning_version_)
        init_result ();
    }

    void insert_statement::
    init_result ()
    {
      // Figure out if we are using the OUTPUT clause or a batch of
      // INSERT and SELECT statements. The latter is used to work
      // around a bug in SQL Server 2005 that causes it to fail
      // on an INSERT statement with the OUTPUT clause if data
      // for one of the inserted columns is supplied at execution
      // (long data).
      //
      batch_ = strstr (text_, "OUTPUT INSERTED.") == 0 &&
        strstr (text_, "output inserted.") == 0;

      SQLUSMALLINT col (1);

      if (returning_id_)
      {
        SQLRETURN r (
          SQLBindCol (stmt_,
                      col++,
                      SQL_C_SBIGINT,
                      (SQLPOINTER) &id_,
                      sizeof (id_),
                      &id_size_ind_));

        if (!SQL_SUCCEEDED (r))
          translate_error (r, conn_, stmt_);
      }

      if (returning_version_)
      {
        SQLRETURN r (
          SQLBindCol (stmt_,
                      col++,
                      SQL_C_BINARY,
                      (SQLPOINTER) &version_,
                      sizeof (version_),
                      &version_size_ind_));

        if (!SQL_SUCCEEDED (r))
          translate_error (r, conn_, stmt_);
      }
    }

    bool insert_statement::
    execute ()
    {
      SQLRETURN r (statement::execute ());

      if (!SQL_SUCCEEDED (r))
      {
        // Translate the integrity contraint violation (SQLSTATE 23000)
        // to the flase return value. This code is similar to that found
        // in translate_error().
        //
        char sqlstate[SQL_SQLSTATE_SIZE + 1];
        SQLINTEGER native_code;
        SQLSMALLINT msg_size;

        bool cv (false);

        for (SQLSMALLINT i (1);; ++i)
        {
          SQLRETURN r (SQLGetDiagRecA (SQL_HANDLE_STMT,
                                       stmt_,
                                       i,
                                       (SQLCHAR*) sqlstate,
                                       &native_code,
                                       0,
                                       0,
                                       &msg_size));

          if (r == SQL_NO_DATA)
            break;
          else if (SQL_SUCCEEDED (r))
          {
            string s (sqlstate);

            if (s == "23000")      // Integrity contraint violation.
              cv = true;
            else if (s != "01000") // General warning.
            {
              // Some other code.
              //
              cv = false;
              break;
            }
          }
          else
          {
            // SQLGetDiagRec() failure.
            //
            cv = false;
            break;
          }
        }

        if (cv)
          return false;
        else
          translate_error (r, conn_, stmt_);
      }

      // Fetch the row containing the id/version if this statement is
      // returning.
      //
      if (returning_id_ || returning_version_)
      {
        if (batch_)
        {
          r = SQLMoreResults (stmt_);

          if (r == SQL_NO_DATA)
          {
            throw database_exception (
            0,
            "?????",
            "multiple result sets expected from a batch of statements");
          }
          else if (!SQL_SUCCEEDED (r))
            translate_error (r, conn_, stmt_);
        }

        r = SQLFetch (stmt_);

        if (r != SQL_NO_DATA && !SQL_SUCCEEDED (r))
          translate_error (r, conn_, stmt_);

        {
          SQLRETURN r (SQLCloseCursor (stmt_)); // Don't overwrite r.

          if (!SQL_SUCCEEDED (r))
            translate_error (r, conn_, stmt_);
        }

        if (r == SQL_NO_DATA)
          throw database_exception (
            0,
            "?????",
            "result set expected from a statement with the OUTPUT clause");
      }

      return true;
    }

    //
    // update_statement
    //

    update_statement::
    ~update_statement ()
    {
    }

    update_statement::
    update_statement (connection_type& conn,
                      const string& text,
                      bool process,
                      binding& param,
                      bool returning_version)
        : statement (conn,
                     text, statement_update,
                     (process ? &param : 0), false),
          returning_version_ (returning_version)
    {
      if (!empty ())
      {
        bind_param (param.bind, param.count);

        if (returning_version_)
          init_result ();
      }
    }

    update_statement::
    update_statement (connection_type& conn,
                      const char* text,
                      bool process,
                      binding& param,
                      bool returning_version,
                      bool copy_text)
        : statement (conn,
                     text, statement_update,
                     (process ? &param : 0), false,
                     copy_text),
          returning_version_ (returning_version)
    {
      if (!empty ())
      {
        bind_param (param.bind, param.count);

        if (returning_version_)
          init_result ();
      }
    }

    void update_statement::
    init_result ()
    {
      SQLRETURN r (
        SQLBindCol (stmt_,
                    1,
                    SQL_C_BINARY,
                    (SQLPOINTER) &version_,
                    sizeof (version_),
                    &version_size_ind_));

      if (!SQL_SUCCEEDED (r))
        translate_error (r, conn_, stmt_);
    }

    unsigned long long update_statement::
    execute ()
    {
      SQLRETURN r (statement::execute ());

      // SQL_NO_DATA indicates that the statement hasn't affected any rows.
      //
      if (r == SQL_NO_DATA)
        return 0;

      if (!SQL_SUCCEEDED (r))
        translate_error (r, conn_, stmt_);

      // Get the number of affected rows.
      //
      SQLLEN rows;
      r = SQLRowCount (stmt_, &rows);

      if (!SQL_SUCCEEDED (r))
        translate_error (r, conn_, stmt_);

      // Fetch the row containing the version if this statement is
      // returning. We still need to close the cursor even if we
      // haven't updated any rows.
      //
      if (returning_version_)
      {
        r = SQLFetch (stmt_);

        if (r != SQL_NO_DATA && !SQL_SUCCEEDED (r))
          translate_error (r, conn_, stmt_);

        {
          SQLRETURN r (SQLCloseCursor (stmt_)); // Don't overwrite r.

          if (!SQL_SUCCEEDED (r))
            translate_error (r, conn_, stmt_);
        }

        if (rows != 0 && r == SQL_NO_DATA)
          throw database_exception (
            0,
            "?????",
            "result set expected from a statement with the OUTPUT clause");
      }

      return static_cast<unsigned long long> (rows);
    }

    //
    // delete_statement
    //

    delete_statement::
    ~delete_statement ()
    {
    }

    delete_statement::
    delete_statement (connection_type& conn,
                      const string& text,
                      binding& param)
        : statement (conn,
                     text, statement_delete,
                     0, false)
    {
      bind_param (param.bind, param.count);
    }

    delete_statement::
    delete_statement (connection_type& conn,
                      const char* text,
                      binding& param,
                      bool copy_text)
        : statement (conn,
                     text, statement_delete,
                     0, false,
                     copy_text)
    {
      bind_param (param.bind, param.count);
    }

    unsigned long long delete_statement::
    execute ()
    {
      SQLRETURN r (statement::execute ());

      // SQL_NO_DATA indicates that the statement hasn't affected any rows.
      //
      if (r == SQL_NO_DATA)
        return 0;

      if (!SQL_SUCCEEDED (r))
        translate_error (r, conn_, stmt_);

      // Get the number of affected rows.
      //
      SQLLEN rows;
      r = SQLRowCount (stmt_, &rows);

      if (!SQL_SUCCEEDED (r))
        translate_error (r, conn_, stmt_);

      return static_cast<unsigned long long> (rows);
    }
  }
}

// file      : odb/mssql/connection.hxx
// author    : Constantin Michael <constantin@codesynthesis.com>
// copyright : Copyright (c) 2005-2011 Code Synthesis Tools CC
// license   : ODB NCUEL; see accompanying LICENSE file

//@@ disabled functionality

#ifndef ODB_MSSQL_CONNECTION_HXX
#define ODB_MSSQL_CONNECTION_HXX

#include <odb/pre.hxx>

#include <memory> // std::auto_ptr

#include <odb/forward.hxx>
#include <odb/connection.hxx>

#include <odb/details/buffer.hxx>
#include <odb/details/shared-ptr.hxx>

#include <odb/mssql/mssql-fwd.hxx>
#include <odb/mssql/version.hxx>
#include <odb/mssql/forward.hxx>
#include <odb/mssql/tracer.hxx>
#include <odb/mssql/transaction-impl.hxx>
#include <odb/mssql/auto-handle.hxx>

#include <odb/mssql/details/export.hxx>

namespace odb
{
  namespace mssql
  {
    class statement_cache;

    class connection;
    typedef details::shared_ptr<connection> connection_ptr;

    class LIBODB_MSSQL_EXPORT connection: public odb::connection
    {
    public:
      typedef mssql::statement_cache statement_cache_type;
      typedef mssql::database database_type;

      virtual
      ~connection ();

      connection (database_type&);
      connection (database_type&, SQLHDBC handle);

      database_type&
      database ()
      {
        return db_;
      }

    public:
      virtual transaction_impl*
      begin ();

    public:
      using odb::connection::execute;

      virtual unsigned long long
      execute (const char* statement, std::size_t length);

      // SQL statement tracing.
      //
    public:
      typedef mssql::tracer tracer_type;

      void
      tracer (tracer_type& t)
      {
        odb::connection::tracer (t);
      }

      void
      tracer (tracer_type* t)
      {
        odb::connection::tracer (t);
      }

      using odb::connection::tracer;

    public:
      bool
      failed () const
      {
        return state_ == state_failed;
      }

      void
      mark_failed ()
      {
        state_ = state_failed;
      }

    public:
      SQLHDBC
      handle ()
      {
        return handle_;
      }

      /*
      statement_cache_type&
      statement_cache ()
      {
        return *statement_cache_;
      }
      */

      details::buffer&
      long_buffer ()
      {
        return long_buffer_;
      }

    private:
      connection (const connection&);
      connection& operator= (const connection&);

    private:
      database_type& db_;
      auto_handle<SQL_HANDLE_DBC> handle_;

      enum
      {
        state_disconnected,
        state_connected,
        state_failed
      } state_;

      // Statement handle for direct execution. It should be after the
      // connection handle to be destroyed in the correct order.
      //
      auto_handle<SQL_HANDLE_STMT> direct_stmt_;

      //std::auto_ptr<statement_cache_type> statement_cache_;

      details::buffer long_buffer_;
    };
  }
}

#include <odb/post.hxx>

#endif // ODB_MSSQL_CONNECTION_HXX
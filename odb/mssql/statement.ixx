// file      : odb/mssql/statement.ixx
// copyright : Copyright (c) 2005-2013 Code Synthesis Tools CC
// license   : ODB NCUEL; see accompanying LICENSE file

namespace odb
{
  namespace mssql
  {
    inline bulk_statement::
    bulk_statement (connection_type& c,
                    const std::string& text,
                    statement_kind k,
                    const binding* process,
                    bool optimize,
                    std::size_t batch,
                    std::size_t skip,
                    SQLUSMALLINT* status)
        : statement (c, text, k, process, optimize),
          status_ (batch == 1 ? 0 : status)
    {
      if (status_ != 0 && !empty ())
        init (skip);
    }

    inline bulk_statement::
    bulk_statement (connection_type& c,
                    const char* text,
                    statement_kind k,
                    const binding* process,
                    bool optimize,
                    std::size_t batch,
                    std::size_t skip,
                    SQLUSMALLINT* status,
                    bool copy_text)
        : statement (c, text, k, process, optimize, copy_text),
          status_ (batch == 1 ? 0 : status)
    {
      if (status_ != 0 && !empty ())
        init (skip);
    }

    // update_statement
    //
    inline unsigned long long update_statement::
    result (std::size_t i)
    {
      if (i != i_)
        mex_->current (++i_); // mex cannot be NULL since this is a batch.

      return result_;
    }

    // delete_statement
    //
    inline unsigned long long delete_statement::
    result (std::size_t i)
    {
      if (i != i_)
        mex_->current (++i_); // mex cannot be NULL since this is a batch.

      return result_;
    }
  }
}

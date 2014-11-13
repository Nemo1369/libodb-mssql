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
      if (batch != 1 && !empty ())
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
      if (batch != 1 && !empty ())
        init (skip);
    }

    inline unsigned long long update_statement::
    version ()
    {
      unsigned long long r;

      // The value is in the big-endian format.
      //
      unsigned char* p (reinterpret_cast<unsigned char*> (&r));
      p[0] = version_[7];
      p[1] = version_[6];
      p[2] = version_[5];
      p[3] = version_[4];
      p[4] = version_[3];
      p[5] = version_[2];
      p[6] = version_[1];
      p[7] = version_[0];

      return r;
    }
  }
}

// file      : odb/mssql/polymorphic-object-statements.hxx
// copyright : Copyright (c) 2005-2012 Code Synthesis Tools CC
// license   : ODB NCUEL; see accompanying LICENSE file

#ifndef ODB_MSSQL_POLYMORPHIC_OBJECT_STATEMENTS_HXX
#define ODB_MSSQL_POLYMORPHIC_OBJECT_STATEMENTS_HXX

#include <odb/pre.hxx>

#include <cstddef> // std::size_t

#include <odb/forward.hxx>
#include <odb/traits.hxx>

#include <odb/details/shared-ptr.hxx>

#include <odb/mssql/version.hxx>
#include <odb/mssql/forward.hxx>
#include <odb/mssql/mssql-types.hxx>
#include <odb/mssql/binding.hxx>
#include <odb/mssql/statement.hxx>
#include <odb/mssql/statements-base.hxx>
#include <odb/mssql/simple-object-statements.hxx>

namespace odb
{
  namespace mssql
  {
    //
    // Implementation for polymorphic objects.
    //

    template <typename T>
    class polymorphic_root_object_statements: public object_statements<T>
    {
    public:
      typedef typename object_statements<T>::connection_type connection_type;
      typedef typename object_statements<T>::object_traits object_traits;
      typedef typename object_statements<T>::id_image_type id_image_type;

      typedef
      typename object_traits::discriminator_image_type
      discriminator_image_type;

      typedef
      typename object_statements<T>::select_statement_type
      select_statement_type;

    public:
      // Interface compatibility with derived_object_statements.
      //
      typedef polymorphic_root_object_statements root_statements_type;

      root_statements_type&
      root_statements ()
      {
        return *this;
      }

    public:
      // Discriminator binding.
      //
      discriminator_image_type&
      discriminator_image () {return discriminator_image_;}

      std::size_t
      discriminator_image_version () const
      {return discriminator_image_version_;}

      void
      discriminator_image_version (std::size_t v)
      {discriminator_image_version_ = v;}

      binding&
      discriminator_image_binding () {return discriminator_image_binding_;}

      // Id binding for discriminator retrieval.
      //
      id_image_type&
      discriminator_id_image () {return discriminator_id_image_;}

      std::size_t
      discriminator_id_image_version () const
      {return discriminator_id_image_version_;}

      void
      discriminator_id_image_version (std::size_t v)
      {discriminator_id_image_version_ = v;}

      binding&
      discriminator_id_image_binding ()
      {return discriminator_id_image_binding_;}

      //
      //
      select_statement_type&
      find_discriminator_statement ()
      {
        if (find_discriminator_ == 0)
          find_discriminator_.reset (
            new (details::shared) select_statement_type (
              this->conn_,
              object_traits::find_discriminator_statement,
              discriminator_id_image_binding_,
              discriminator_image_binding_));

        return *find_discriminator_;
      }

    public:
      polymorphic_root_object_statements (connection_type&);

      virtual
      ~polymorphic_root_object_statements ();

    public:
      static const std::size_t id_column_count =
        object_statements<T>::id_column_count;

      static const std::size_t discriminator_column_count =
        object_traits::discriminator_column_count;

      static const std::size_t managed_optimistic_column_count =
        object_traits::managed_optimistic_column_count;

    private:
      // Discriminator image.
      //
      discriminator_image_type discriminator_image_;
      std::size_t discriminator_image_version_;
      binding discriminator_image_binding_;
      bind discriminator_image_bind_[discriminator_column_count +
                                     managed_optimistic_column_count];

      // Id image for discriminator retrieval (only used as a parameter).
      //
      id_image_type discriminator_id_image_;
      std::size_t discriminator_id_image_version_;
      binding discriminator_id_image_binding_;
      bind discriminator_id_image_bind_[id_column_count];

      details::shared_ptr<select_statement_type> find_discriminator_;
    };

    template <typename T>
    class polymorphic_derived_object_statements: public statements_base
    {
    public:
      typedef T object_type;
      typedef odb::object_traits<object_type> object_traits;
      typedef typename object_traits::id_type id_type;
      typedef typename object_traits::pointer_type pointer_type;
      typedef typename object_traits::id_image_type id_image_type;
      typedef typename object_traits::image_type image_type;

      typedef typename object_traits::root_type root_type;
      typedef
      polymorphic_root_object_statements<root_type>
      root_statements_type;

      typedef typename object_traits::base_type base_type;
      typedef
      typename object_traits::base_traits::statements_type
      base_statements_type;

      typedef
      typename object_traits::container_statement_cache_type
      container_statement_cache_type;

      typedef mssql::insert_statement insert_statement_type;
      typedef mssql::select_statement select_statement_type;
      typedef mssql::update_statement update_statement_type;
      typedef mssql::delete_statement delete_statement_type;

      typedef typename root_statements_type::auto_lock auto_lock;

    public:
      polymorphic_derived_object_statements (connection_type&);

      virtual
      ~polymorphic_derived_object_statements ();

    public:
      // Delayed loading.
      //
      static void
      delayed_loader (odb::database&, const id_type&, root_type&);

    public:
      // Root and immediate base statements.
      //
      root_statements_type&
      root_statements ()
      {
        return root_statements_;
      }

      base_statements_type&
      base_statements ()
      {
        return base_statements_;
      }

    public:
      // Object image.
      //
      image_type&
      image ()
      {
        return image_;
      }

      // Insert binding.
      //
      std::size_t
      insert_image_version () const { return insert_image_version_;}

      void
      insert_image_version (std::size_t v) {insert_image_version_ = v;}

      std::size_t
      insert_id_binding_version () const { return insert_id_binding_version_;}

      void
      insert_id_binding_version (std::size_t v) {insert_id_binding_version_ = v;}

      binding&
      insert_image_binding () {return insert_image_binding_;}

      // Update binding.
      //
      std::size_t
      update_image_version () const { return update_image_version_;}

      void
      update_image_version (std::size_t v) {update_image_version_ = v;}

      std::size_t
      update_id_binding_version () const { return update_id_binding_version_;}

      void
      update_id_binding_version (std::size_t v) {update_id_binding_version_ = v;}

      binding&
      update_image_binding () {return update_image_binding_;}

      // Select binding.
      //
      std::size_t*
      select_image_versions () { return select_image_versions_;}

      binding*
      select_image_bindings () {return select_image_bindings_;}

      binding&
      select_image_binding (std::size_t d)
      {
        return select_image_bindings_[object_traits::depth - d];
      }

      // Object id binding (comes from the root statements).
      //
      id_image_type&
      id_image () {return root_statements_.id_image ();}

      std::size_t
      id_image_version () const {return root_statements_.id_image_version ();}

      void
      id_image_version (std::size_t v) {root_statements_.id_image_version (v);}

      binding&
      id_image_binding () {return root_statements_.id_image_binding ();}

      // Statements.
      //
      insert_statement_type&
      persist_statement ()
      {
        if (persist_ == 0)
          persist_.reset (
            new (details::shared) insert_statement_type (
              conn_,
              object_traits::persist_statement,
              insert_image_binding_,
              false));

        return *persist_;
      }

      select_statement_type&
      find_statement (std::size_t d)
      {
        std::size_t i (object_traits::depth - d);
        details::shared_ptr<select_statement_type>& p (find_[i]);

        if (p == 0)
          p.reset (
            new (details::shared) select_statement_type (
              conn_,
              object_traits::find_statements[i],
              root_statements_.id_image_binding (),
              select_image_bindings_[i]));

        return *p;
      }

      update_statement_type&
      update_statement ()
      {
        if (update_ == 0)
          update_.reset (
            new (details::shared) update_statement_type (
              conn_,
              object_traits::update_statement,
              update_image_binding_));

        return *update_;
      }

      delete_statement_type&
      erase_statement ()
      {
        if (erase_ == 0)
          erase_.reset (
            new (details::shared) delete_statement_type (
              conn_,
              object_traits::erase_statement,
              root_statements_.id_image_binding ()));

        return *erase_;
      }

      // Container statement cache.
      //
      container_statement_cache_type&
      container_statment_cache ()
      {
        return container_statement_cache_.get (conn_);
      }

    public:
      // select = total - id + base::select
      // insert = total - inverse
      // update = total - inverse - id - readonly
      //
      static const std::size_t id_column_count =
        object_traits::id_column_count;

      static const std::size_t select_column_count =
        object_traits::column_count - id_column_count +
        base_statements_type::select_column_count;

      static const std::size_t insert_column_count =
        object_traits::column_count - object_traits::inverse_column_count;

      static const std::size_t update_column_count = insert_column_count -
        object_traits::id_column_count - object_traits::readonly_column_count;

    private:
      polymorphic_derived_object_statements (
        const polymorphic_derived_object_statements&);

      polymorphic_derived_object_statements&
      operator= (const polymorphic_derived_object_statements&);

    private:
      root_statements_type& root_statements_;
      base_statements_type& base_statements_;

      container_statement_cache_ptr<container_statement_cache_type>
      container_statement_cache_;

      image_type image_;

      // Select binding. Here we are have an array of statements/bindings
      // one for each depth. In other words, if we have classes root, base,
      // and derived, then we have the following array of statements:
      //
      // [0] d + b + r
      // [1] d + b
      // [2] d
      //
      // Also, because we have a chain of images bound to these statements,
      // we have an array of versions, one entry for each base plus one for
      // our own image.
      //
      // A poly-abstract class only needs the first statement and in this
      // case we have only one entry in the the bindings and statements
      // arrays (but not versions; we still have a chain of images).
      //
      std::size_t select_image_versions_[object_traits::depth];
      binding select_image_bindings_[
        object_traits::abstract ? 1 : object_traits::depth];
      bind select_image_bind_[select_column_count];

      // Insert binding. The id binding is copied from the hierarchy root.
      //
      std::size_t insert_image_version_;
      std::size_t insert_id_binding_version_;
      binding insert_image_binding_;
      bind insert_image_bind_[insert_column_count];

      // Update binding. The id suffix binding is copied from the hierarchy
      // root.
      //
      std::size_t update_image_version_;
      std::size_t update_id_binding_version_;
      binding update_image_binding_;
      bind update_image_bind_[update_column_count + id_column_count];

      details::shared_ptr<insert_statement_type> persist_;
      details::shared_ptr<select_statement_type> find_[
        object_traits::abstract ? 1 : object_traits::depth];
      details::shared_ptr<update_statement_type> update_;
      details::shared_ptr<delete_statement_type> erase_;
    };
  }
}

#include <odb/mssql/polymorphic-object-statements.txx>

#include <odb/post.hxx>

#endif // ODB_MSSQL_POLYMORPHIC_OBJECT_STATEMENTS_HXX

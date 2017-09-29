/* Copyright (c) 2014, 2017, Oracle and/or its affiliates. All rights reserved.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   51 Franklin Street, Suite 500, Boston, MA 02110-1335 USA */

#ifndef DD__PARTITION_IMPL_INCLUDED
#define DD__PARTITION_IMPL_INCLUDED

#include <sys/types.h>
#include <memory>
#include <new>

#include "sql/dd/collection.h"
#include "sql/dd/impl/types/entity_object_impl.h" // dd::Entity_object_impl
#include "sql/dd/impl/types/weak_object_impl.h"
#include "sql/dd/object_id.h"
#include "sql/dd/properties.h"
#include "sql/dd/sdi_fwd.h"
#include "sql/dd/string_type.h"
#include "sql/dd/types/object_type.h"          // dd::Object_type
#include "sql/dd/types/partition.h"            // dd::Partition
#include "sql/dd/types/partition_index.h"      // IWYU pragma: keep
#include "sql/dd/types/partition_value.h"      // IWYU pragma: keep
#include "sql/dd/types/table.h"

namespace dd {

///////////////////////////////////////////////////////////////////////////

class Index;
class Object_table;
class Open_dictionary_tables_ctx;
class Properties;
class Raw_record;
class Sdi_rcontext;
class Sdi_wcontext;
class Table;
class Table_impl;
class Weak_object;

///////////////////////////////////////////////////////////////////////////

class Partition_impl : public Entity_object_impl,
                       public Partition
{
public:
  Partition_impl();

  Partition_impl(Table_impl *table);

  Partition_impl(Table_impl *parent, Partition_impl *partition);

  Partition_impl(const Partition_impl &src, Table_impl *parent);

  Partition_impl(const Partition_impl &src, Partition_impl *partition);

  virtual ~Partition_impl();

public:
  virtual const Object_table &object_table() const
  { return Partition::OBJECT_TABLE(); }

  virtual bool validate() const;

  virtual bool restore_children(Open_dictionary_tables_ctx *otx);

  virtual bool store_children(Open_dictionary_tables_ctx *otx);

  virtual bool drop_children(Open_dictionary_tables_ctx *otx) const;

  virtual bool restore_attributes(const Raw_record &r);

  virtual bool store_attributes(Raw_record *r);

  void serialize(Sdi_wcontext *wctx, Sdi_writer *w) const;

  bool deserialize(Sdi_rcontext *rctx, const RJ_Value &val);

  void debug_print(String_type &outb) const;

  void set_ordinal_position(uint)
  { }

  virtual uint ordinal_position() const
  { return -1; }

public:
  /////////////////////////////////////////////////////////////////////////
  // Table.
  /////////////////////////////////////////////////////////////////////////

  virtual const Table &table() const;

  virtual Table &table();

  /* non-virtual */ const Table_impl &table_impl() const
  { return *m_table; }

  /* non-virtual */ Table_impl &table_impl()
  { return *m_table; }

  /////////////////////////////////////////////////////////////////////////
  // Parent partition.
  /////////////////////////////////////////////////////////////////////////

  virtual const Partition *parent_partition() const
  { return m_parent; }

  virtual Partition *parent_partition()
  { return const_cast<dd::Partition*>(m_parent); }

  /////////////////////////////////////////////////////////////////////////
  // parent_partition_id
  /////////////////////////////////////////////////////////////////////////

  virtual Object_id parent_partition_id() const
  { return m_parent_partition_id; }

  virtual void set_parent_partition_id(Object_id parent_partition_id)
  { m_parent_partition_id= parent_partition_id; }

  /////////////////////////////////////////////////////////////////////////
  // number.
  /////////////////////////////////////////////////////////////////////////

  virtual uint number() const
  { return m_number; }

  virtual void set_number(uint number)
  { m_number= number; }

  /////////////////////////////////////////////////////////////////////////
  // description_utf8.
  /////////////////////////////////////////////////////////////////////////

  virtual const String_type &description_utf8() const
  { return m_description_utf8; }

  virtual void set_description_utf8(const String_type &description_utf8)
  { m_description_utf8= description_utf8; }

  /////////////////////////////////////////////////////////////////////////
  // engine.
  /////////////////////////////////////////////////////////////////////////

  virtual const String_type &engine() const
  { return m_engine; }

  virtual void set_engine(const String_type &engine)
  { m_engine= engine; }

  /////////////////////////////////////////////////////////////////////////
  // comment.
  /////////////////////////////////////////////////////////////////////////

  virtual const String_type &comment() const
  { return m_comment; }

  virtual void set_comment(const String_type &comment)
  { m_comment= comment; }

  /////////////////////////////////////////////////////////////////////////
  // Options.
  /////////////////////////////////////////////////////////////////////////

  virtual const Properties &options() const
  { return *m_options; }

  virtual Properties &options()
  { return *m_options; }

  virtual bool set_options_raw(const String_type &options_raw);

  /////////////////////////////////////////////////////////////////////////
  // se_private_data.
  /////////////////////////////////////////////////////////////////////////

  virtual const Properties &se_private_data() const
  { return *m_se_private_data; }

  virtual Properties &se_private_data()
  { return *m_se_private_data; }

  virtual bool set_se_private_data_raw(const String_type &se_private_data_raw);

  virtual void set_se_private_data(const Properties &se_private_data);

  /////////////////////////////////////////////////////////////////////////
  // se_private_id.
  /////////////////////////////////////////////////////////////////////////

  virtual Object_id se_private_id() const
  { return m_se_private_id; }

  virtual void set_se_private_id(Object_id se_private_id)
  { m_se_private_id= se_private_id; }

  /////////////////////////////////////////////////////////////////////////
  // Tablespace.
  /////////////////////////////////////////////////////////////////////////

  virtual Object_id tablespace_id() const
  { return m_tablespace_id; }

  virtual void set_tablespace_id(Object_id tablespace_id)
  { m_tablespace_id= tablespace_id; }

  /////////////////////////////////////////////////////////////////////////
  // Partition-value collection
  /////////////////////////////////////////////////////////////////////////

  virtual Partition_value *add_value();

  virtual const Partition_values &values() const
  { return m_values; }

  /////////////////////////////////////////////////////////////////////////
  // Partition-index collection
  /////////////////////////////////////////////////////////////////////////

  virtual Partition_index *add_index(Index *idx);

  virtual const Partition_indexes &indexes() const
  { return m_indexes; }

  /* purecov: begin deadcode */
  virtual Partition_indexes *indexes()
  { return &m_indexes; }
  /* purecov: end */

  /////////////////////////////////////////////////////////////////////////
  // Sub Partition collection.
  /////////////////////////////////////////////////////////////////////////

  virtual Partition *add_sub_partition();

  virtual const Table::Partition_collection &sub_partitions() const
  { return m_sub_partitions; }

  virtual Table::Partition_collection *sub_partitions()
  { return &m_sub_partitions; }

  virtual const Partition *parent() const
  { return m_parent; }
  virtual void set_parent(const Partition *parent)
  { m_parent= parent; }

  // Fix "inherits ... via dominance" warnings
  virtual Entity_object_impl *impl()
  { return Entity_object_impl::impl(); }
  virtual const Entity_object_impl *impl() const
  { return Entity_object_impl::impl(); }
  virtual Object_id id() const
  { return Entity_object_impl::id(); }
  virtual bool is_persistent() const
  { return Entity_object_impl::is_persistent(); }
  virtual const String_type &name() const
  { return Entity_object_impl::name(); }
  virtual void set_name(const String_type &name)
  { Entity_object_impl::set_name(name); }

public:
  static Partition_impl *restore_item(Table_impl *table)
  {
    return new (std::nothrow) Partition_impl(table);
  }

  static Partition_impl *restore_item(Partition_impl *part)
  {
    Partition_impl *p= new (std::nothrow) Partition_impl(&part->table_impl(),
                                                         part);
    p->set_parent(part);

    return p;
  }

  static Partition_impl *clone(const Partition_impl &other,
                               Table_impl *table)
  {
    return new (std::nothrow) Partition_impl(other, table);
  }

  static Partition_impl *clone(const Partition_impl &other,
                               Partition_impl *part)
  {
    return new (std::nothrow) Partition_impl(other, part);
  }

private:
  // Fields.

  Object_id m_parent_partition_id;
  uint m_number;
  Object_id m_se_private_id;

  String_type m_description_utf8;
  String_type m_engine;
  String_type m_comment;
  std::unique_ptr<Properties> m_options;
  std::unique_ptr<Properties> m_se_private_data;

  // References to tightly-coupled objects.

  Table_impl *m_table;

  const Partition *m_parent;

  Partition_values m_values;
  Partition_indexes m_indexes;
  Table::Partition_collection m_sub_partitions;

  // References to loosely-coupled objects.

  Object_id m_tablespace_id;
};

///////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////

class Partition_type : public Object_type
{
public:
  virtual void register_tables(Open_dictionary_tables_ctx *otx) const;

  virtual Weak_object *create_object() const
  { return new (std::nothrow) Partition_impl(); }
};

///////////////////////////////////////////////////////////////////////////

/** Used to compare two partition elements. */
struct Partition_order_comparator
{
  // TODO : do we really need this ordering now ?
  bool operator() (const dd::Partition* p1, const dd::Partition* p2) const
  {
    if (p1->parent_partition_id() == p2->parent_partition_id())
      return p1->number() < p2->number();
    return p1->parent_partition_id() < p2->parent_partition_id();
  }
};

}

#endif // DD__PARTITION_IMPL_INCLUDED

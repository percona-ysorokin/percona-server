/* Copyright (c) 2006, 2015, Oracle and/or its affiliates. All rights reserved.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA */

#ifndef KEY_INCLUDED
#define KEY_INCLUDED

#include "my_global.h"                          /* uchar */
#include "my_base.h"                   /* ha_rows, ha_key_alg */
#include "sql_plugin_ref.h"            /* plugin_ref */

class Field;
class String;
struct TABLE;
typedef struct st_bitmap MY_BITMAP;

class KEY_PART_INFO {	/* Info about a key part */
public:
  Field *field;
  uint	offset;				/* offset in record (from 0) */
  uint	null_offset;			/* Offset to null_bit in record */
  /* Length of key part in bytes, excluding NULL flag and length bytes */
  uint16 length;
  /*
    Number of bytes required to store the keypart value. This may be
    different from the "length" field as it also counts
     - possible NULL-flag byte (see HA_KEY_NULL_LENGTH)
     - possible HA_KEY_BLOB_LENGTH bytes needed to store actual value length.
  */
  uint16 store_length;
  uint16 key_type;
  uint16 fieldnr;			/* Fieldnum in UNIREG */
  uint16 key_part_flag;			/* 0 or HA_REVERSE_SORT */
  uint8 type;
  uint8 null_bit;			/* Position to null_bit */
  void init_from_field(Field *fld);     /** Fill data from given field */
  void init_flags();                    /** Set key_part_flag from field */
};

/**
  Data type for records per key estimates that are stored in the 
  KEY::rec_per_key_float[] array.
*/
typedef float rec_per_key_t;

/**
  If an entry for a key part in KEY::rec_per_key_float[] has this value,
  then the storage engine has not provided a value for it and the rec_per_key
  value for this key part is unknown.
*/
#define REC_PER_KEY_UNKNOWN -1.0f

typedef struct st_key {
  /** Tot length of key */
  uint	key_length;
  /** dupp key and pack flags */
  ulong flags;
  /** dupp key and pack flags for actual key parts */
  ulong actual_flags;
  /** How many key_parts */
  uint  user_defined_key_parts;
  /** How many key_parts including hidden parts */
  uint  actual_key_parts;
  /**
     Key parts allocated for primary key parts extension but
     not used due to some reasons(no primary key, duplicated key parts)
  */
  uint  unused_key_parts;
  /** Should normally be = key_parts */
  uint	usable_key_parts;
  uint  block_size;
  enum  ha_key_alg algorithm;
  /**
    Note that parser is used when the table is opened for use, and
    parser_name is used when the table is being created.
  */
  union
  {
    /** Fulltext [pre]parser */
    plugin_ref parser;
    /** Fulltext [pre]parser name */
    LEX_STRING *parser_name;
  };
  KEY_PART_INFO *key_part;
  /** Name of key */
  char	*name;
  /**
    Array of AVG(#records with the same field value) for 1st ... Nth key part.
    0 means 'not known'.
    For internally created temporary tables this member is NULL.
  */
  ulong *rec_per_key;

private:
  /**
    Array of AVG(#records with the same field value) for 1st ... Nth
    key part. For internally created temporary tables this member is
    NULL. This is the same information as stored in the above
    rec_per_key array but using float values instead of integer
    values. If the storage engine has supplied values in this array,
    these will be used. Otherwise the value in rec_per_key will be
    used.  @todo In the next release the rec_per_key array above
    should be removed and only this should be used.
  */
  rec_per_key_t *rec_per_key_float;
public:
  union {
    int  bdb_return_if_eq;
  } handler;
  TABLE *table;
  LEX_STRING comment;

  /** 
    Check if records per key estimate is available for given key part.

    @param key_part_no key part number, must be in [0, KEY::actual_key_parts)

    @return true if records per key estimate is available, false otherwise
  */

  bool has_records_per_key(uint key_part_no) const
  {
    DBUG_ASSERT(key_part_no < actual_key_parts);

    return ((rec_per_key_float && rec_per_key_float[key_part_no] !=
             REC_PER_KEY_UNKNOWN) || 
            (rec_per_key && rec_per_key[key_part_no] != 0));
  }

  /**
    Retrieve an estimate for the average number of records per distinct value,
    when looking only at the first key_part_no+1 columns.

    If no record per key estimate is available for this key part,
    REC_PER_KEY_UNKNOWN is returned.

    @param key_part_no key part number, must be in [0, KEY::actual_key_parts)

    @return Number of records having the same key value
      @retval REC_PER_KEY_UNKNOWN    no records per key estimate available
      @retval != REC_PER_KEY_UNKNOWN record per key estimate
  */

  rec_per_key_t records_per_key(uint key_part_no) const
  {
    DBUG_ASSERT(key_part_no < actual_key_parts);

    /*
      If the storage engine has provided rec per key estimates as float
      then use this. If not, use the integer version.
    */
    if (rec_per_key_float[key_part_no] != REC_PER_KEY_UNKNOWN)
      return rec_per_key_float[key_part_no];

    return (rec_per_key[key_part_no] != 0) ? 
      static_cast<rec_per_key_t>(rec_per_key[key_part_no]) :
      REC_PER_KEY_UNKNOWN;
  }

  /**
    Set the records per key estimate for a key part.

    The records per key estimate must be in [1.0,..> or take the value
    REC_PER_KEY_UNKNOWN.

    @param key_part_no     the number of key parts that the estimate includes,
                           must be in [0, KEY::actual_key_parts)
    @param rec_per_key_est new records per key estimate
  */

  void set_records_per_key(uint key_part_no, rec_per_key_t rec_per_key_est)
  {
    DBUG_ASSERT(key_part_no < actual_key_parts);
    DBUG_ASSERT(rec_per_key_est == REC_PER_KEY_UNKNOWN ||
                rec_per_key_est >= 1.0);
    DBUG_ASSERT(rec_per_key_float != NULL);

    rec_per_key_float[key_part_no]= rec_per_key_est;
  }

  /**
    Check if this key supports storing records per key information.

    @return true if it has support for storing records per key information,
            false otherwise.
  */

  bool supports_records_per_key() const
  {
    if (rec_per_key_float != NULL && rec_per_key != NULL)
      return true;

    return false;
  }

  /**
    Assign storage for the rec per key arrays to the KEY object.

    This is used when allocating memory and creating KEY objects. The
    caller is responsible for allocating the correct size for the
    two arrays. If needed, the caller is also responsible for
    de-allocating the memory when the KEY object is no longer used.

    @param rec_per_key_arg       pointer to allocated array for storing
                                 records per key using ulong
    @param rec_per_key_float_arg pointer to allocated array for storing
                                 records per key using float
  */

  void set_rec_per_key_array(ulong *rec_per_key_arg,
                             rec_per_key_t *rec_per_key_float_arg)
  {
    rec_per_key= rec_per_key_arg;
    rec_per_key_float= rec_per_key_float_arg;
  }
} KEY;


int find_ref_key(KEY *key, uint key_count, uchar *record, Field *field,
                 uint *key_length, uint *keypart);
void key_copy(uchar *to_key, uchar *from_record, KEY *key_info, uint key_length);
void key_restore(uchar *to_record, uchar *from_key, KEY *key_info,
                 uint key_length);
bool key_cmp_if_same(TABLE *form,const uchar *key,uint index,uint key_length);
void key_unpack(String *to, TABLE *table, KEY *key);
void field_unpack(String *to, Field *field, const uchar *rec, uint max_length,
                  bool prefix_key);
bool is_key_used(TABLE *table, uint idx, const MY_BITMAP *fields);
int key_cmp(KEY_PART_INFO *key_part, const uchar *key, uint key_length);
int key_cmp2(KEY_PART_INFO *key_part,
             const uchar *key1, uint key1_length,
             const uchar *key2, uint key2_length);
int key_rec_cmp(KEY **key_info, uchar *a, uchar *b);

#endif /* KEY_INCLUDED */

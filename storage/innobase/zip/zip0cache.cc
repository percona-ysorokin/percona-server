/*****************************************************************************

Copyright (c) 2010-2016, Percona Inc. All Rights Reserved.

This program is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation; version 2 of the License.

This program is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Suite 500, Boston, MA 02110-1335 USA

*****************************************************************************/

#include "zip0cache.h"

namespace zip
{
  const compression_dictionary_cache::item& compression_dictionary_cache::operator [] (const compression_dictionary_cache::key& k) const
  {
    // The list of the possible cases:
    // 1. The item already exists in the "items" container
    //    a. It is has "item_state_does_not_exist" state. "dictionary_ref" must not be dereferenced.
    //    b. It is has "item_state_exists" state. "dictionary_ref" can be dereferenced to obtain dictionary id/blob.
    // 2. The item does not exist in the "items" container
    //    a. item (table_id, column_id) does not exist in the SYS_ZIP_DICT_COLS InnoDB system table
    //    b. item (table_id, column_id) exists in the SYS_ZIP_DICT_COLS InnoDB system table.
    //      1. a dictionary with acquired id does not exist in the "dictionary_records". Acquired dictionary id must exist in the SYS_ZIP_DICT InnoDB system table.
    //      2. a dictionary with acquired id exists in the "dictionary_records"
    const item* res = 0;
    bool need_items_alteration = false;
    std::pair<item_container::iterator, item_container::iterator> items_fnd;

    {
      // acquire shared lock on items mutex and try to find an item in the cache
      // for the given (table_id, column_id) combination
      mysql::shared_lock<mysql::rwlock> guard(items_mutex_);
      items_fnd = items_.equal_range(k);
      if(items_fnd.first == items_fnd.second)
      {
        // 2.* cases
        need_items_alteration = true;
      }
      else
      {
        // assigning result for 1.a or 1.b cases
        res = &items_fnd.first->second;
      }
    }

    if(need_items_alteration)
    {
      // we get here only when on 2.* cases
      dictionary_id_type dictionary_id;
      item_state_type state = item_state_not_checked;
      bool need_dictionary_records_alteration = false;
      std::pair<dictionary_record_container::iterator, dictionary_record_container::iterator> dictionary_records_fnd;

      // here, out of the lock, try to find dictionary id for the given
      // (table_id, column_id) combination from SYS_ZIP_DICT_COLS InnoDB
      // system table
      if(get_dictionary_id_by_key(k, dictionary_id))
      {
        // 2.b.* cases
        state = item_state_exists;
        {
          // acquire shared lock on dictionary_records mutex and try to find a record
          // in the dictionary_record container by id 
          mysql::shared_lock<mysql::rwlock> guard(dictionary_records_mutex_);
          dictionary_records_fnd = dictionary_records_.equal_range(dictionary_id);
          if (dictionary_records_fnd.first == dictionary_records_fnd.second)
          {
            // case 2.b.1
            need_dictionary_records_alteration = true;
          }
          // else case 2.b.2
        }
        
        if(need_dictionary_records_alteration)
        {
          // we get here only when on 2.b.1 case
          blob_type blob;
          // here, out of the lock, try to get dictionary blob by id from the
          // SYS_ZIP_DICT InnoDB system table
          assert(get_dictionary_blob_by_id(dictionary_id, blob));

          {
            // acquire exclusive lock on dictionary_records mutex and try add a
            // new element there using previously found position as a hint
            mysql::unique_lock<mysql::rwlock> guard(dictionary_records_mutex_);
            dictionary_records_fnd.first = dictionary_records_.insert(/* hint */ dictionary_records_fnd.second, std::make_pair(dictionary_id, dictionary_record()));
            if(dictionary_records_fnd.first->second.ref_counter == dictionary_record::ref_counter_special_value)
            {
              // a new object was indeed inserted as it has default state (ref_counter == <special_value>)
              
              // it is safe here to use non-atomic ref_counter assignment here as
              // the returned iterator (dictionary_records_fnd.first) hasn't been
              // exposed to other containers yet
              dictionary_records_fnd.first->second.ref_counter = 0;
              dictionary_records_fnd.first->second.blob.swap(blob);
            }
            else
            {
              // if because of locking granularity two concurrent threads are inserting
              // elements to the "dictionary_records", make sure that dictionary blob
              // is the same in this case.
              assert(dictionary_records_fnd.first->second.blob == blob);
            }
          }
        }
      }
      else
      {
        // 2.a case
        state = item_state_does_not_exist;
      }

      {
        // we get here only when on 2.* cases

        // acquire exclusive lock on items mutex and try to add a new
        // element there using previously found position as a hint
        mysql::unique_lock<mysql::rwlock> guard(items_mutex_);
        items_fnd.first = items_.insert(/* hint */ items_fnd.second, std::make_pair(k, item()));
        if(items_fnd.first->second.get_state() == item_state_not_checked)
        {
          // dictionary_records_fnd.first will be default constructed if 
          // state == item_state_does_not_exist
          if(state == item_state_does_not_exist)
            items_fnd.first->second.init(state);
          else
          {
            items_fnd.first->second.init(state, dictionary_records_fnd.first);
            os_atomic_increment_uint32(&dictionary_records_fnd.first->second.ref_counter, 1);
          }
        }
        else
        {
          // if because of locking granularity two concurrent threads are inserting
          // elements to the "items", make sure that the state is the same and if it
          // is not "item_state_does_not_exist", the referred dictionary id is also
          // the same
          assert(items_fnd.first->second.get_state() == state && (state == item_state_does_not_exist || items_fnd.first->second.dictionary_ref_->first == dictionary_id));
        }
        
        // assigning result for 2.* cases
        res = &items_fnd.first->second;
      }
    }
    return *res;
  }

  /*static*/
  bool compression_dictionary_cache::get_dictionary_id_by_key(const compression_dictionary_cache::key& k, compression_dictionary_cache::dictionary_id_type& id)
  {
    id = 0;
    return true;
  }

  /*static*/
  bool compression_dictionary_cache::get_dictionary_blob_by_id(compression_dictionary_cache::dictionary_id_type id, compression_dictionary_cache::blob_type& blob)
  {
    return true;
  }

} // namespace zip

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

#ifndef ZIP_CACHE_H
#define ZIP_CACHE_H

#include <string>
#include <map>

#include "univ.i"
#include "my_sys.h"


namespace mysql
{
  namespace detail
  {
    template<typename T>
    T* addressof(T& arg) 
    {
      return reinterpret_cast<T*>(
        &const_cast<char&>(reinterpret_cast<const volatile char&>(arg))
      );
    }

    // base class for mysql allocator
    template<typename T>
    struct allocator_base
    {	
      typedef T value_type;
    };

    // allocator_base specialization for const types
    template<typename T>
    struct allocator_base<const T>
    {	
      typedef T value_type;
    };
  } // namespace mysql::detail

  // generic my_malloc_allocator for objects of class T
  template<typename T>
  class my_malloc_allocator: public detail::allocator_base<T>
  {	
    public:
      typedef my_malloc_allocator<T> other;

      typedef detail::allocator_base<T> base_type;
      typedef typename base_type::value_type value_type;

      typedef value_type*       pointer;
      typedef const value_type* const_pointer;
      typedef void*             void_pointer;
      typedef const void*       const_void_pointer;

      typedef value_type&       reference;
      typedef const value_type& const_reference;

      typedef size_t    size_type;
      typedef ptrdiff_t difference_type;

      // convert this type to allocator<Other>
      template<typename Other>
      struct rebind
      {	
        typedef my_malloc_allocator<Other> other;
      };

      // return address of mutable value
      pointer address(reference value) const
      {
        return detail::addressof(value);
      }

      // return address of nonmutable value
      const_pointer address(const_reference value) const
      {	
        return detail::addressof(value);
      }

      // construct default allocator (do nothing)
      my_malloc_allocator()
      {}

      // construct by copying (do nothing)
      my_malloc_allocator(const my_malloc_allocator<T>&)
      {}

      // assignment operator (do nothing)
      my_malloc_allocator<T>& operator = (const my_malloc_allocator<T>&)
      {
        return *this;
      }

      // construct from a related allocator (do nothing)
      template<typename Other>
      my_malloc_allocator(const my_malloc_allocator<Other>&)
      {}

      // assign from a related allocator (do nothing)
      template<typename Other>
      my_malloc_allocator<T>& operator = (const my_malloc_allocator<Other>&)
      {
        return *this;
      }

      // allocate array of count elements, ignore hint
      pointer allocate(size_type count, const_void_pointer = 0)
      {
        void_pointer ptr = 0;

        if (count != 0 && count <= max_size())
          ptr = my_malloc(count * sizeof(T), MYF(0));

        return static_cast<pointer>(ptr);
      }

      // deallocate object at ptr, ignore size
      void deallocate(pointer ptr, size_type)
      {
        my_free(ptr);
      }

      // default construct object at ptr
      void construct(pointer ptr)
      {
        ::new (static_cast<void_pointer>(ptr)) T();
      }

      // construct object at ptr with value value
      void construct(pointer ptr, const_reference value)
      {
        ::new (static_cast<void_pointer>(ptr)) T(value);
      }

      // destroy object at ptr
      void destroy(pointer ptr)
      {
        ptr->~T();
      }

      // estimate maximum array size
      size_t max_size() const
      {
        return (~static_cast<std::size_t>(0) / sizeof(T));
      }
  };

  // generic allocator for type void
  template<>
  class my_malloc_allocator<void>
  {
    public:
      typedef my_malloc_allocator<void> other;

      typedef void value_type;

      typedef void*       pointer;
      typedef const void* const_pointer;
      typedef void*       void_pointer;
      typedef const void* const_void_pointer;

      // convert this type to an allocator<Other>
      template<typename Other>
      struct rebind
      {
        typedef my_malloc_allocator<Other> other;
      };

      // construct default allocator (do nothing)
      my_malloc_allocator()
      {}

      // construct by copying (do nothing)
      my_malloc_allocator(const my_malloc_allocator<void>&)
      {}

      // assignment operator (do nothing)
      my_malloc_allocator<void>& operator = (const my_malloc_allocator<void>&)
      {
        return *this;
      }

      // construct from related allocator (do nothing)
      template<typename Other>
      my_malloc_allocator(const my_malloc_allocator<Other>&)
      {}

      // assign from a related allocator (do nothing)
      template<typename Other>
      my_malloc_allocator<void>& operator=(const my_malloc_allocator<Other>&)
      {
        return *this;
      }
  };

  // test for allocator equality
  template<typename T, typename Other>
  inline
  bool operator == (const my_malloc_allocator<T>&, const my_malloc_allocator<Other>&)
  {
    return true;
  }

  // test for allocator inequality
  template<typename T, typename Other>
  inline
  bool operator != (const my_malloc_allocator<T>& x, const my_malloc_allocator<Other>& y)
  {
    return !(x == y);
  }

  // Read-write lock based on rw_lock_t
  class rwlock
  {
    template<typename Mutex>
    friend class shared_lock;
    template<typename Mutex>
    friend class unique_lock;

    //noncopyable
    private:
      rwlock(const rwlock&);
      rwlock& operator = (const rwlock&);

    public:
      rwlock():
        impl_()
      {
        my_rwlock_init(&impl_, NULL);
      }

      ~rwlock()
      {
        rwlock_destroy(&impl_);
      }

    private:
      rw_lock_t impl_;

      void lock()
      {
        rw_wrlock(&impl_);
      }

      void unlock()
      {
        rw_unlock(&impl_);
      }

      void lock_shared()
      {
        rw_rdlock(&impl_);
      }

      void unlock_shared()
      {
        rw_unlock(&impl_);
      }
  };

  // Shared ownership guard
  template<typename Mutex>
  class shared_lock
  {
    //noncopyable
    private:
      shared_lock(const shared_lock<Mutex>&);
      shared_lock<Mutex>& operator = (const shared_lock<Mutex>&);

    public:
      shared_lock(Mutex& mutex):
        mutex_(mutex)
      {
        mutex_.lock_shared();
      }

      ~shared_lock()
      {
        mutex_.unlock_shared();
      }

    private:
      Mutex& mutex_;
  };

  // Exclusive ownership guard
  template<typename Mutex>
  class unique_lock
  {
    //noncopyable
    private:
      unique_lock(const unique_lock<Mutex>&);
      unique_lock<Mutex>& operator = (const unique_lock<Mutex>&);

    public:
      unique_lock(Mutex& mutex):
        mutex_(mutex)
      {
        mutex_.lock();
      }

      ~unique_lock()
      {
        mutex_.unlock();
      }

    private:
      Mutex& mutex_;
  };
} // namespace mysql

namespace zip
{
  // A class for storing cached compression dictionary data
  class compression_dictionary_cache
  {
    private:
      // noncopyable
      compression_dictionary_cache(const compression_dictionary_cache&);
      compression_dictionary_cache& operator = (const compression_dictionary_cache&);

    public:
      // like std::string but with my_malloc_allocator allocator
      typedef std::basic_string<char, std::char_traits<char>, mysql::my_malloc_allocator<char> > blob_type;

    private:
      typedef ib_uint32_t ref_counter_type;
      typedef ib_uint64_t dictionary_id_type;

      // private struct for holding dictionary blob with reference counter
      struct dictionary_record
      {
        static const ref_counter_type ref_counter_special_value = ~static_cast<ref_counter_type>(0);

        dictionary_record():
          ref_counter(ref_counter_special_value),
          blob()
        {}

        ref_counter_type ref_counter;
        blob_type blob;
      };
      typedef mysql::my_malloc_allocator<std::pair<const dictionary_id_type, dictionary_record> > dictionary_record_map_allocator;
      typedef std::map<dictionary_id_type, dictionary_record, std::less<dictionary_id_type>, dictionary_record_map_allocator> dictionary_record_container;

    public:
      typedef ib_uint64_t table_id_type;
      typedef ib_uint64_t column_id_type;

      // public struct representing dictionary cache composite key (table_id, column_id)
      struct key
      {
        key():
          table_id(),
          column_id()
        {}

        key(
          table_id_type  table_id_value,
          column_id_type column_id_value
        ):
          table_id(table_id_value),
          column_id(column_id_value)
        {}

        table_id_type  table_id;
        column_id_type column_id;

        friend bool operator == (const key& x, const key& y)
        {
          return x.table_id == y.table_id && x.column_id == y.column_id;
        }
        friend bool operator < (const key& x, const key& y)
        {
	        return x.table_id < y.table_id || (x.table_id == y.table_id && x.column_id < y.column_id);
        }
      };

      // enum describing cashed item state
      enum item_state_type
      {
        item_state_not_checked,
        item_state_does_not_exist,
        item_state_exists
      };

      // a class representing the result of the cache lookup
      class item
      {
        friend class compression_dictionary_cache;

        public:
          item_state_type  get_state() const {  return state_;  }
          const blob_type& get_blob () const {  return dictionary_ref_->second.blob;  }

        private:
          typedef dictionary_record_container::const_iterator dictionary_iterator;
          item():
            state_(item_state_not_checked),
            dictionary_ref_()
          {}

          void init(item_state_type state)
          {
            state_ = state;
          }

          void init(
            item_state_type state,
            dictionary_iterator dictionary_ref
          )
          {
            state_ = state;
            dictionary_ref_ = dictionary_ref;
          }

          item_state_type state_;
          dictionary_iterator dictionary_ref_;
      };

    public:
      compression_dictionary_cache():
        dictionary_records_mutex_(),
        dictionary_records_(),
        items_mutex_(),
        items_()
      {}

      const item& operator [] (const key& k) const;

    private:
      mutable mysql::rwlock dictionary_records_mutex_;
      mutable dictionary_record_container dictionary_records_;
      
      typedef std::map<key, item, std::less<key>, mysql::my_malloc_allocator<std::pair<const key, item> > > item_container;
      mutable mysql::rwlock items_mutex_;
      mutable item_container items_;

      static bool get_dictionary_id_by_key(const key& k, dictionary_id_type& id);
      static bool get_dictionary_blob_by_id(dictionary_id_type id, blob_type& blob);
  };
} // namespace zip

#endif /* ZIP_CACHE_H */

#ifndef ATOMIC_RWLOCK_INCLUDED
#define ATOMIC_RWLOCK_INCLUDED

/* Copyright (c) 2006, 2013 Oracle and/or its affiliates. All rights reserved.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA */

#define MY_ATOMIC_MODE_RWLOCKS 1

#ifdef MY_ATOMIC_MODE_DUMMY
/*
  the following can never be enabled by ./configure, one need to put #define in
  a source to trigger the following warning. The resulting code will be broken,
  it only makes sense to do it to see now test_atomic detects broken
  implementations (another way is to run a UP build on an SMP box).
*/
#warning MY_ATOMIC_MODE_DUMMY and MY_ATOMIC_MODE_RWLOCKS are incompatible

typedef char my_atomic_rwlock_t;

#define my_atomic_rwlock_destroy(name)
#define my_atomic_rwlock_init(name)
#define my_atomic_rwlock_rdlock(name)
#define my_atomic_rwlock_wrlock(name)
#define my_atomic_rwlock_rdunlock(name)
#define my_atomic_rwlock_wrunlock(name)
#define MY_ATOMIC_MODE "dummy (non-atomic)"
#else /* not MY_ATOMIC_MODE_DUMMY */

typedef struct {pthread_mutex_t rw;} my_atomic_rwlock_t;

/*
  we're using read-write lock macros but map them to mutex locks, and they're
  faster. Still, having semantically rich API we can change the
  underlying implementation, if necessary.
*/
#define my_atomic_rwlock_destroy(name)     pthread_mutex_destroy(& (name)->rw)
#define my_atomic_rwlock_init(name)        pthread_mutex_init(& (name)->rw, 0)
#define my_atomic_rwlock_rdlock(name)      pthread_mutex_lock(& (name)->rw)
#define my_atomic_rwlock_wrlock(name)      pthread_mutex_lock(& (name)->rw)
#define my_atomic_rwlock_rdunlock(name)    pthread_mutex_unlock(& (name)->rw)
#define my_atomic_rwlock_wrunlock(name)    pthread_mutex_unlock(& (name)->rw)

#define MY_ATOMIC_MODE "mutex"
#ifndef MY_ATOMIC_MODE_RWLOCKS
#define MY_ATOMIC_MODE_RWLOCKS 1
#endif
#endif

#define make_atomic_add_body(S)     int ## S sav; sav= *a; *a+= v; v=sav;
#define make_atomic_fas_body(S)     int ## S sav; sav= *a; *a= v; v=sav;
#define make_atomic_cas_body(S)     if ((ret= (*a == *cmp))) *a= set; else *cmp=*a;
#define make_atomic_load_body(S)    ret= *a;
#define make_atomic_store_body(S)   *a= v;

#endif /* ATOMIC_RWLOCK_INCLUDED */

/* Copyright (C) 2000, 2002, 2005 MySQL AB

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */

/* 
 * sql_manager.cc
 * This thread manages various maintenance tasks.
 *
 *   o Flushing the tables every flush_time seconds.
 *   o Berkeley DB: removing unneeded log files.
 */

#include "mysql_priv.h"

ulong volatile manager_status;
bool volatile manager_thread_in_use;

pthread_t manager_thread;
pthread_mutex_t LOCK_manager;
pthread_cond_t COND_manager;

struct handler_cb {
   struct handler_cb *next;
   void (*action)(void);
};

static struct handler_cb * volatile cb_list;

bool mysql_manager_submit(void (*action)())
{
  bool result= FALSE;
  struct handler_cb * volatile *cb;
  pthread_mutex_lock(&LOCK_manager);
  cb= &cb_list;
  while (*cb && (*cb)->action != action)
    cb= &(*cb)->next;
  if (!*cb)
  {
    *cb= (struct handler_cb *)my_malloc(sizeof(struct handler_cb), MYF(MY_WME));
    if (!*cb)
      result= TRUE;
    else
    {
      (*cb)->next= NULL;
      (*cb)->action= action;
    }
  }
  pthread_mutex_unlock(&LOCK_manager);
  return result;
}

pthread_handler_t handle_manager(void *arg __attribute__((unused)))
{
  int error = 0;
  ulong status;
  struct timespec abstime;
  bool reset_flush_time = TRUE;
  struct handler_cb *cb= NULL;
  my_thread_init();
  DBUG_ENTER("handle_manager");

  pthread_detach_this_thread();
  manager_thread = pthread_self();
  manager_status = 0;
  manager_thread_in_use = 1;

  for (;;)
  {
    pthread_mutex_lock(&LOCK_manager);
    /* XXX: This will need to be made more general to handle different
     * polling needs. */
    if (flush_time)
    {
      if (reset_flush_time)
      {
	set_timespec(abstime, flush_time);
        reset_flush_time = FALSE;
      }
      while (!manager_status && (!error || error == EINTR) && !abort_loop)
        error= pthread_cond_timedwait(&COND_manager, &LOCK_manager, &abstime);
    }
    else
    {
      while (!manager_status && (!error || error == EINTR) && !abort_loop)
        error= pthread_cond_wait(&COND_manager, &LOCK_manager);
    }
    status = manager_status;
    manager_status = 0;
    if (cb == NULL)
    {
      cb= cb_list;
      cb_list= NULL;
    }
    pthread_mutex_unlock(&LOCK_manager);

    if (abort_loop)
      break;

    if (error == ETIMEDOUT || error == ETIME)
    {
      flush_tables();
      error = 0;
      reset_flush_time = TRUE;
    }

    while (cb)
    {
      struct handler_cb *next= cb->next;
      cb->action();
      my_free((gptr)cb, MYF(0));
      cb= next;
    }

    if (status)
      DBUG_PRINT("error", ("manager did not handle something: %lx", status));
  }
  manager_thread_in_use = 0;
  my_thread_end();
  DBUG_RETURN(NULL);
}

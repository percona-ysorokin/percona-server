/* Copyright (c) 2000, 2010, Oracle and/or its affiliates. All rights reserved.

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

#include "sql_priv.h"
#include "unireg.h"                             // HAVE_*
#include "rpl_mi.h"
#include "rpl_rli.h"
#include "sql_base.h"                        // close_thread_tables
#include <my_dir.h>    // For MY_STAT
#include "log_event.h" // Format_description_log_event, Log_event,
                       // FORMAT_DESCRIPTION_LOG_EVENT, ROTATE_EVENT,
                       // PREFIX_SQL_LOAD
#include "rpl_slave.h"
#include "rpl_utility.h"
#include "transaction.h"
#include "sql_parse.h"                          // end_trans, ROLLBACK
#include "rpl_slave.h"

/*
  Please every time you add a new field to the relay log info, update
  what follows. For now, this is just used to get the number of
  fields.
*/
const char* info_rli_fields[]=
{
  "number_of_lines",
  "group_relay_log_name",
  "group_relay_log_pos",
  "group_master_log_name",
  "group_master_log_pos",
  "sql_delay"
};

Relay_log_info::Relay_log_info(bool is_slave_recovery
#ifdef HAVE_PSI_INTERFACE
                               ,PSI_mutex_key *param_key_info_run_lock,
                               PSI_mutex_key *param_key_info_data_lock,
                               PSI_mutex_key *param_key_info_sleep_lock,
                               PSI_mutex_key *param_key_info_data_cond,
                               PSI_mutex_key *param_key_info_start_cond,
                               PSI_mutex_key *param_key_info_stop_cond,
                               PSI_mutex_key *param_key_info_sleep_cond
#endif
                              )
   :Rpl_info("SQL"
#ifdef HAVE_PSI_INTERFACE
             ,param_key_info_run_lock, param_key_info_data_lock,
             param_key_info_sleep_lock,
             param_key_info_data_cond, param_key_info_start_cond,
             param_key_info_stop_cond, param_key_info_sleep_cond
#endif
            ),
   replicate_same_server_id(::replicate_same_server_id),
   cur_log_fd(-1), relay_log(&sync_relaylog_period),
   is_relay_log_recovery(is_slave_recovery),
   save_temporary_tables(0),
   cur_log_old_open_count(0), group_relay_log_pos(0), event_relay_log_pos(0),
   group_master_log_pos(0), log_space_total(0), ignore_log_space_limit(0),
   last_master_timestamp(0), slave_skip_counter(0),
   abort_pos_wait(0), until_condition(UNTIL_NONE),
   until_log_pos(0), retried_trans(0),
   tables_to_lock(0), tables_to_lock_count(0),
   rows_query_ev(NULL), last_event_start_time(0),
   sql_delay(0), sql_delay_end(0),
   m_flags(0)
{
  DBUG_ENTER("Relay_log_info::Relay_log_info");

#ifdef HAVE_PSI_INTERFACE
  relay_log.set_psi_keys(key_RELAYLOG_LOCK_index,
                         key_RELAYLOG_update_cond,
                         key_file_relaylog,
                         key_file_relaylog_index);
#endif

  group_relay_log_name[0]= event_relay_log_name[0]=
    group_master_log_name[0]= 0;
  until_log_name[0]= ign_master_log_name_end[0]= 0;
  bzero((char*) &cache_buf, sizeof(cache_buf));
  cached_charset_invalidate();
  mysql_mutex_init(key_relay_log_info_log_space_lock,
                   &log_space_lock, MY_MUTEX_INIT_FAST);
  mysql_cond_init(key_relay_log_info_log_space_cond, &log_space_cond, NULL);
  relay_log.init_pthread_objects();
  DBUG_VOID_RETURN;
}

Relay_log_info::~Relay_log_info()
{
  DBUG_ENTER("Relay_log_info::~Relay_log_info");

  mysql_mutex_destroy(&log_space_lock);
  mysql_cond_destroy(&log_space_cond);
  relay_log.cleanup();

  DBUG_VOID_RETURN;
}

static inline int add_relay_log(Relay_log_info* rli,LOG_INFO* linfo)
{
  MY_STAT s;
  DBUG_ENTER("add_relay_log");
  if (!mysql_file_stat(key_file_relaylog,
                       linfo->log_file_name, &s, MYF(0)))
  {
    sql_print_error("log %s listed in the index, but failed to stat.",
                    linfo->log_file_name);
    DBUG_RETURN(1);
  }
  rli->log_space_total += s.st_size;
#ifndef DBUG_OFF
  char buf[22];
  DBUG_PRINT("info",("log_space_total: %s", llstr(rli->log_space_total,buf)));
#endif
  DBUG_RETURN(0);
}

int Relay_log_info::count_relay_log_space()
{
  LOG_INFO flinfo;
  DBUG_ENTER("Relay_log_info::count_relay_log_space");
  log_space_total= 0;
  if (relay_log.find_log_pos(&flinfo, NullS, 1))
  {
    sql_print_error("Could not find first log while counting relay log space.");
    DBUG_RETURN(1);
  }
  do
  {
    if (add_relay_log(this, &flinfo))
      DBUG_RETURN(1);
  } while (!relay_log.find_next_log(&flinfo, 1));
  /*
     As we have counted everything, including what may have written in a
     preceding write, we must reset bytes_written, or we may count some space
     twice.
  */
  relay_log.reset_bytes_written();
  DBUG_RETURN(0);
}

/**
   Resets UNTIL condition for Relay_log_info
 */

void Relay_log_info::clear_until_condition()
{
  DBUG_ENTER("clear_until_condition");

  until_condition= Relay_log_info::UNTIL_NONE;
  until_log_name[0]= 0;
  until_log_pos= 0;
  DBUG_VOID_RETURN;
}

/**
  Opens and intialize the given relay log. Specifically, it does what follows:

  - Closes old open relay log files.
  - If we are using the same relay log as the running IO-thread, then sets.
    rli->cur_log to point to the same IO_CACHE entry.
  - If not, opens the 'log' binary file.

  @todo check proper initialization of group_master_log_name/group_master_log_pos.


  @param  rli[in]                 Relay information (will be initialized)
  @param  log[in]                 Name of relay log file to read from. NULL = First log
  @param  pos[in]                 Position in relay log file
  @param  need_data_lock[in]      Set to 1 if this functions should do mutex locks
  @param  errmsg[out]             Store pointer to error message here
  @param  look_for_description_event[in]
                                  1 if we should look for such an event. We only need
                                  this when the SQL thread starts and opens an existing
                                  relay log and has to execute it (possibly from an
                                  offset >4); then we need to read the first event of
                                  the relay log to be able to parse the events we have
                                  to execute.
  @retval 0 ok,
  @retval 1 otherwise error, where errmsg is set to point to the error message.
*/

int Relay_log_info::init_relay_log_pos(const char* log,
                                       ulonglong pos, bool need_data_lock,
                                       const char** errmsg,
                                       bool look_for_description_event)
{
  DBUG_ENTER("Relay_log_info::init_relay_log_pos");
  DBUG_PRINT("info", ("pos: %lu", (ulong) pos));

  *errmsg=0;
  mysql_mutex_t *log_lock= relay_log.get_log_lock();

  if (need_data_lock)
    mysql_mutex_lock(&data_lock);

  /*
    Slave threads are not the only users of init_relay_log_pos(). CHANGE MASTER
    is, too, and init_slave() too; these 2 functions allocate a description
    event in init_relay_log_pos, which is not freed by the terminating SQL slave
    thread as that thread is not started by these functions. So we have to free
    the description_event here, in case, so that there is no memory leak in
    running, say, CHANGE MASTER.
  */
  delete relay_log.description_event_for_exec;
  /*
    By default the relay log is in binlog format 3 (4.0).
    Even if format is 4, this will work enough to read the first event
    (Format_desc) (remember that format 4 is just lenghtened compared to format
    3; format 3 is a prefix of format 4).
  */
  relay_log.description_event_for_exec= new
    Format_description_log_event(3);

  mysql_mutex_lock(log_lock);

  /* Close log file and free buffers if it's already open */
  if (cur_log_fd >= 0)
  {
    end_io_cache(&cache_buf);
    mysql_file_close(cur_log_fd, MYF(MY_WME));
    cur_log_fd = -1;
  }

  group_relay_log_pos= event_relay_log_pos= pos;

  /*
    Test to see if the previous run was with the skip of purging
    If yes, we do not purge when we restart
  */
  if (relay_log.find_log_pos(&linfo, NullS, 1))
  {
    *errmsg="Could not find first log during relay log initialization";
    goto err;
  }

  if (log && relay_log.find_log_pos(&linfo, log, 1))
  {
    *errmsg="Could not find target log during relay log initialization";
    goto err;
  }

  strmake(group_relay_log_name, linfo.log_file_name,
          sizeof(group_relay_log_name) - 1);
  strmake(event_relay_log_name, linfo.log_file_name,
          sizeof(event_relay_log_name) - 1);

  if (relay_log.is_active(linfo.log_file_name))
  {
    /*
      The IO thread is using this log file.
      In this case, we will use the same IO_CACHE pointer to
      read data as the IO thread is using to write data.
    */
    my_b_seek((cur_log=relay_log.get_log_file()), (off_t)0);
    if (check_binlog_magic(cur_log, errmsg))
      goto err;
    cur_log_old_open_count=relay_log.get_open_count();
  }
  else
  {
    /*
      Open the relay log and set cur_log to point at this one
    */
    if ((cur_log_fd=open_binlog(&cache_buf,
                                linfo.log_file_name,errmsg)) < 0)
      goto err;
    cur_log = &cache_buf;
  }
  /*
    In all cases, check_binlog_magic() has been called so we're at offset 4 for
    sure.
  */
  if (pos > BIN_LOG_HEADER_SIZE) /* If pos<=4, we stay at 4 */
  {
    Log_event* ev;
    while (look_for_description_event)
    {
      /*
        Read the possible Format_description_log_event; if position
        was 4, no need, it will be read naturally.
      */
      DBUG_PRINT("info",("looking for a Format_description_log_event"));

      if (my_b_tell(cur_log) >= pos)
        break;

      /*
        Because of we have data_lock and log_lock, we can safely read an
        event
      */
      if (!(ev= Log_event::read_log_event(cur_log, 0,
                                          relay_log.description_event_for_exec,
                                          opt_slave_sql_verify_checksum)))
      {
        DBUG_PRINT("info",("could not read event, cur_log->error=%d",
                           cur_log->error));
        if (cur_log->error) /* not EOF */
        {
          *errmsg= "I/O error reading event at position 4";
          goto err;
        }
        break;
      }
      else if (ev->get_type_code() == FORMAT_DESCRIPTION_EVENT)
      {
        DBUG_PRINT("info",("found Format_description_log_event"));
        delete relay_log.description_event_for_exec;
        relay_log.description_event_for_exec= (Format_description_log_event*) ev;
        /*
          As ev was returned by read_log_event, it has passed is_valid(), so
          my_malloc() in ctor worked, no need to check again.
        */
        /*
          Ok, we found a Format_description event. But it is not sure that this
          describes the whole relay log; indeed, one can have this sequence
          (starting from position 4):
          Format_desc (of slave)
          Rotate (of master)
          Format_desc (of master)
          So the Format_desc which really describes the rest of the relay log
          is the 3rd event (it can't be further than that, because we rotate
          the relay log when we queue a Rotate event from the master).
          But what describes the Rotate is the first Format_desc.
          So what we do is:
          go on searching for Format_description events, until you exceed the
          position (argument 'pos') or until you find another event than Rotate
          or Format_desc.
        */
      }
      else
      {
        DBUG_PRINT("info",("found event of another type=%d",
                           ev->get_type_code()));
        look_for_description_event= (ev->get_type_code() == ROTATE_EVENT);
        delete ev;
      }
    }
    my_b_seek(cur_log,(off_t)pos);
#ifndef DBUG_OFF
  {
    char llbuf1[22], llbuf2[22];
    DBUG_PRINT("info", ("my_b_tell(cur_log)=%s >event_relay_log_pos=%s",
                        llstr(my_b_tell(cur_log),llbuf1),
                        llstr(get_event_relay_log_pos(),llbuf2)));
  }
#endif

  }

err:
  /*
    If we don't purge, we can't honour relay_log_space_limit ;
    silently discard it
  */
  if (!relay_log_purge)
    log_space_limit= 0;
  mysql_cond_broadcast(&data_cond);

  mysql_mutex_unlock(log_lock);

  if (need_data_lock)
    mysql_mutex_unlock(&data_lock);
  if (!relay_log.description_event_for_exec->is_valid() && !*errmsg)
    *errmsg= "Invalid Format_description log event; could be out of memory";

  DBUG_RETURN ((*errmsg) ? 1 : 0);
}

/**
  Waits until the SQL thread reaches (has executed up to) the
  log/position or timed out.

  SYNOPSIS
  @param[in]  thd             client thread that sent @c SELECT @c MASTER_POS_WAIT,
  @param[in]  log_name        log name to wait for,
  @param[in]  log_pos         position to wait for,
  @param[in]  timeout         @c timeout in seconds before giving up waiting.
                              @c timeout is longlong whereas it should be ulong; but this is
                              to catch if the user submitted a negative timeout.

  @retval  -2   improper arguments (log_pos<0)
                or slave not running, or master info changed
                during the function's execution,
                or client thread killed. -2 is translated to NULL by caller,
  @retval  -1   timed out
  @retval  >=0  number of log events the function had to wait
                before reaching the desired log/position
 */

int Relay_log_info::wait_for_pos(THD* thd, String* log_name,
                                    longlong log_pos,
                                    longlong timeout)
{
  int event_count = 0;
  ulong init_abort_pos_wait;
  int error=0;
  struct timespec abstime; // for timeout checking
  const char *msg;
  DBUG_ENTER("Relay_log_info::wait_for_pos");

  if (!inited)
    DBUG_RETURN(-2);

  DBUG_PRINT("enter",("log_name: '%s'  log_pos: %lu  timeout: %lu",
                      log_name->c_ptr(), (ulong) log_pos, (ulong) timeout));

  set_timespec(abstime,timeout);
  mysql_mutex_lock(&data_lock);
  msg= thd->enter_cond(&data_cond, &data_lock,
                       "Waiting for the slave SQL thread to "
                       "advance position");
  /*
     This function will abort when it notices that some CHANGE MASTER or
     RESET MASTER has changed the master info.
     To catch this, these commands modify abort_pos_wait ; We just monitor
     abort_pos_wait and see if it has changed.
     Why do we have this mechanism instead of simply monitoring slave_running
     in the loop (we do this too), as CHANGE MASTER/RESET SLAVE require that
     the SQL thread be stopped?
     This is becasue if someones does:
     STOP SLAVE;CHANGE MASTER/RESET SLAVE; START SLAVE;
     the change may happen very quickly and we may not notice that
     slave_running briefly switches between 1/0/1.
  */
  init_abort_pos_wait= abort_pos_wait;

  /*
    We'll need to
    handle all possible log names comparisons (e.g. 999 vs 1000).
    We use ulong for string->number conversion ; this is no
    stronger limitation than in find_uniq_filename in sql/log.cc
  */
  ulong log_name_extension;
  char log_name_tmp[FN_REFLEN]; //make a char[] from String

  strmake(log_name_tmp, log_name->ptr(), min(log_name->length(), FN_REFLEN-1));

  char *p= fn_ext(log_name_tmp);
  char *p_end;
  if (!*p || log_pos<0)
  {
    error= -2; //means improper arguments
    goto err;
  }
  // Convert 0-3 to 4
  log_pos= max(log_pos, BIN_LOG_HEADER_SIZE);
  /* p points to '.' */
  log_name_extension= strtoul(++p, &p_end, 10);
  /*
    p_end points to the first invalid character.
    If it equals to p, no digits were found, error.
    If it contains '\0' it means conversion went ok.
  */
  if (p_end==p || *p_end)
  {
    error= -2;
    goto err;
  }

  /* The "compare and wait" main loop */
  while (!thd->killed &&
         init_abort_pos_wait == abort_pos_wait &&
         slave_running)
  {
    bool pos_reached;
    int cmp_result= 0;

    DBUG_PRINT("info",
               ("init_abort_pos_wait: %ld  abort_pos_wait: %ld",
                init_abort_pos_wait, abort_pos_wait));
    DBUG_PRINT("info",("group_master_log_name: '%s'  pos: %lu",
                       group_master_log_name, (ulong) group_master_log_pos));

    /*
      group_master_log_name can be "", if we are just after a fresh
      replication start or after a CHANGE MASTER TO MASTER_HOST/PORT
      (before we have executed one Rotate event from the master) or
      (rare) if the user is doing a weird slave setup (see next
      paragraph).  If group_master_log_name is "", we assume we don't
      have enough info to do the comparison yet, so we just wait until
      more data. In this case master_log_pos is always 0 except if
      somebody (wrongly) sets this slave to be a slave of itself
      without using --replicate-same-server-id (an unsupported
      configuration which does nothing), then group_master_log_pos
      will grow and group_master_log_name will stay "".
    */
    if (*group_master_log_name)
    {
      char *basename= (group_master_log_name +
                       dirname_length(group_master_log_name));
      /*
        First compare the parts before the extension.
        Find the dot in the master's log basename,
        and protect against user's input error :
        if the names do not match up to '.' included, return error
      */
      char *q= (char*)(fn_ext(basename)+1);
      if (strncmp(basename, log_name_tmp, (int)(q-basename)))
      {
        error= -2;
        break;
      }
      // Now compare extensions.
      char *q_end;
      ulong group_master_log_name_extension= strtoul(q, &q_end, 10);
      if (group_master_log_name_extension < log_name_extension)
        cmp_result= -1 ;
      else
        cmp_result= (group_master_log_name_extension > log_name_extension) ? 1 : 0 ;

      pos_reached= ((!cmp_result && group_master_log_pos >= (ulonglong)log_pos) ||
                    cmp_result > 0);
      if (pos_reached || thd->killed)
        break;
    }

    //wait for master update, with optional timeout.

    DBUG_PRINT("info",("Waiting for master update"));
    /*
      We are going to mysql_cond_(timed)wait(); if the SQL thread stops it
      will wake us up.
    */
    if (timeout > 0)
    {
      /*
        Note that mysql_cond_timedwait checks for the timeout
        before for the condition ; i.e. it returns ETIMEDOUT
        if the system time equals or exceeds the time specified by abstime
        before the condition variable is signaled or broadcast, _or_ if
        the absolute time specified by abstime has already passed at the time
        of the call.
        For that reason, mysql_cond_timedwait will do the "timeoutting" job
        even if its condition is always immediately signaled (case of a loaded
        master).
      */
      error= mysql_cond_timedwait(&data_cond, &data_lock, &abstime);
    }
    else
      mysql_cond_wait(&data_cond, &data_lock);
    DBUG_PRINT("info",("Got signal of master update or timed out"));
    if (error == ETIMEDOUT || error == ETIME)
    {
      error= -1;
      break;
    }
    error=0;
    event_count++;
    DBUG_PRINT("info",("Testing if killed or SQL thread not running"));
  }

err:
  thd->exit_cond(msg);
  DBUG_PRINT("exit",("killed: %d  abort: %d  slave_running: %d \
improper_arguments: %d  timed_out: %d",
                     thd->killed_errno(),
                     (int) (init_abort_pos_wait != abort_pos_wait),
                     (int) slave_running,
                     (int) (error == -2),
                     (int) (error == -1)));
  if (thd->killed || init_abort_pos_wait != abort_pos_wait ||
      !slave_running)
  {
    error= -2;
  }
  DBUG_RETURN( error ? error : event_count );
}

void Relay_log_info::inc_group_relay_log_pos(ulonglong log_pos,
                                                bool skip_lock)
{
  DBUG_ENTER("Relay_log_info::inc_group_relay_log_pos");

  if (skip_lock)
    mysql_mutex_assert_owner(&data_lock);
  else
    mysql_mutex_lock(&data_lock);

  inc_event_relay_log_pos();
  group_relay_log_pos= event_relay_log_pos;
  strmake(group_relay_log_name,event_relay_log_name,
          sizeof(group_relay_log_name)-1);

  notify_group_relay_log_name_update();

  /*
    If the slave does not support transactions and replicates a transaction,
    users should not trust group_master_log_pos (which they can display with
    SHOW SLAVE STATUS or read from relay-log.info), because to compute
    group_master_log_pos the slave relies on log_pos stored in the master's
    binlog, but if we are in a master's transaction these positions are always
    the BEGIN's one (excepted for the COMMIT), so group_master_log_pos does
    not advance as it should on the non-transactional slave (it advances by
    big leaps, whereas it should advance by small leaps).
  */
  /*
    In 4.x we used the event's len to compute the positions here. This is
    wrong if the event was 3.23/4.0 and has been converted to 5.0, because
    then the event's len is not what is was in the master's binlog, so this
    will make a wrong group_master_log_pos (yes it's a bug in 3.23->4.0
    replication: Exec_master_log_pos is wrong). Only way to solve this is to
    have the original offset of the end of the event the relay log. This is
    what we do in 5.0: log_pos has become "end_log_pos" (because the real use
    of log_pos in 4.0 was to compute the end_log_pos; so better to store
    end_log_pos instead of begin_log_pos.
    If we had not done this fix here, the problem would also have appeared
    when the slave and master are 5.0 but with different event length (for
    example the slave is more recent than the master and features the event
    UID). It would give false MASTER_POS_WAIT, false Exec_master_log_pos in
    SHOW SLAVE STATUS, and so the user would do some CHANGE MASTER using this
    value which would lead to badly broken replication.
    Even the relay_log_pos will be corrupted in this case, because the len is
    the relay log is not "val".
    With the end_log_pos solution, we avoid computations involving lengthes.
  */
  DBUG_PRINT("info", ("log_pos: %lu  group_master_log_pos: %lu",
                      (long) log_pos, (long) group_master_log_pos));
  if (log_pos) // 3.23 binlogs don't have log_posx
  {
    group_master_log_pos= log_pos;
  }
  mysql_cond_broadcast(&data_cond);
  if (!skip_lock)
    mysql_mutex_unlock(&data_lock);
  DBUG_VOID_RETURN;
}


void Relay_log_info::close_temporary_tables()
{
  TABLE *table,*next;
  DBUG_ENTER("Relay_log_info::close_temporary_tables");

  for (table=save_temporary_tables ; table ; table=next)
  {
    next=table->next;
    /*
      Don't ask for disk deletion. For now, anyway they will be deleted when
      slave restarts, but it is a better intention to not delete them.
    */
    DBUG_PRINT("info", ("table: 0x%lx", (long) table));
    close_temporary(table, 1, 0);
  }
  save_temporary_tables= 0;
  slave_open_temp_tables= 0;
  DBUG_VOID_RETURN;
}

/**
  Purges relay logs. It assumes to have a run lock on rli and that no
  slave thread are running.

  @param[in]   THD         connection,
  @param[in]   just_reset  if false, it tells that logs should be purged
                           and @c init_relay_log_pos() should be called,
  @errmsg[out] errmsg      store pointer to an error message.

  @retval 0 successfuly executed,
  @retval 1 otherwise error, where errmsg is set to point to the error message.
*/

int Relay_log_info::purge_relay_logs(THD *thd, bool just_reset,
                                     const char** errmsg)
{
  int error=0;
  DBUG_ENTER("purge_relay_logs");

  /*
    Even if inited==0, we still try to empty master_log_* variables. Indeed,
    inited==0 does not imply that they already are empty.

    It could be that slave's info initialization partly succeeded: for example
    if relay-log.info existed but *relay-bin*.* have been manually removed,
    init_info reads the old relay-log.info and fills rli->master_log_*, then
    init_info checks for the existence of the relay log, this fails and 
    init_info leaves inited to 0.
    In that pathological case, master_log_pos* will be properly reinited at
    the next START SLAVE (as RESET SLAVE or CHANGE MASTER, the callers of
    purge_relay_logs, will delete bogus *.info files or replace them with
    correct files), however if the user does SHOW SLAVE STATUS before START
    SLAVE, he will see old, confusing master_log_*. In other words, we reinit
    master_log_* for SHOW SLAVE STATUS to display fine in any case.
  */
  group_master_log_name[0]= 0;
  group_master_log_pos= 0;

  if (!inited)
  {
    DBUG_PRINT("info", ("inited == 0"));
    DBUG_RETURN(0);
  }

  DBUG_ASSERT(slave_running == 0);
  DBUG_ASSERT(mi->slave_running == 0);

  slave_skip_counter= 0;
  mysql_mutex_lock(&data_lock);

  /*
    we close the relay log fd possibly left open by the slave SQL thread,
    to be able to delete it; the relay log fd possibly left open by the slave
    I/O thread will be closed naturally in reset_logs() by the
    close(LOG_CLOSE_TO_BE_OPENED) call
  */
  if (cur_log_fd >= 0)
  {
    end_io_cache(&cache_buf);
    my_close(cur_log_fd, MYF(MY_WME));
    cur_log_fd= -1;
  }

  if (relay_log.reset_logs(thd))
  {
    *errmsg = "Failed during log reset";
    error=1;
    goto err;
  }
  /* Save name of used relay log file */
  strmake(group_relay_log_name, relay_log.get_log_fname(),
          sizeof(group_relay_log_name)-1);
  strmake(event_relay_log_name, relay_log.get_log_fname(),
          sizeof(event_relay_log_name)-1);
  group_relay_log_pos= event_relay_log_pos= BIN_LOG_HEADER_SIZE;
  if (count_relay_log_space())
  {
    *errmsg= "Error counting relay log space";
    error= 1;
    goto err;
  }
  if (!just_reset)
    error= init_relay_log_pos(group_relay_log_name,
                              group_relay_log_pos,
                              0 /* do not need data lock */, errmsg, 0);

err:
#ifndef DBUG_OFF
  char buf[22];
#endif
  DBUG_PRINT("info",("log_space_total: %s",llstr(log_space_total,buf)));
  mysql_mutex_unlock(&data_lock);
  DBUG_RETURN(error);
}


/**
     Checks if condition stated in UNTIL clause of START SLAVE is reached.

     Specifically, it checks if UNTIL condition is reached. Uses caching result
     of last comparison of current log file name and target log file name. So
     cached value should be invalidated if current log file name changes (see
     @c Relay_log_info::notify_... functions).

     This caching is needed to avoid of expensive string comparisons and
     @c strtol() conversions needed for log names comparison. We don't need to
     compare them each time this function is called, we only need to do this
     when current log name changes. If we have @c UNTIL_MASTER_POS condition we
     need to do this only after @c Rotate_log_event::do_apply_event() (which is
     rare, so caching gives real benifit), and if we have @c UNTIL_RELAY_POS
     condition then we should invalidate cached comarison value after
     @c inc_group_relay_log_pos() which called for each group of events (so we
     have some benefit if we have something like queries that use
     autoincrement or if we have transactions).

     Should be called ONLY if @c until_condition @c != @c UNTIL_NONE !

     @param master_beg_pos    position of the beginning of to be executed event
                              (not @c log_pos member of the event that points to
                              the beginning of the following event)

     @retval true   condition met or error happened (condition seems to have
                    bad log file name),
     @retval false  condition not met.
*/

bool Relay_log_info::is_until_satisfied(THD *thd, Log_event *ev)
{
  const char *log_name;
  ulonglong log_pos;
  DBUG_ENTER("Relay_log_info::is_until_satisfied");

  DBUG_ASSERT(until_condition != UNTIL_NONE);

  if (until_condition == UNTIL_MASTER_POS)
  {
    if (ev && ev->server_id == (uint32) ::server_id && !replicate_same_server_id)
      DBUG_RETURN(FALSE);
    log_name= group_master_log_name;
    log_pos= (!ev)? group_master_log_pos :
      ((thd->variables.option_bits & OPTION_BEGIN || !ev->log_pos) ?
       group_master_log_pos : ev->log_pos - ev->data_written);
  }
  else
  { /* until_condition == UNTIL_RELAY_POS */
    log_name= group_relay_log_name;
    log_pos= group_relay_log_pos;
  }

#ifndef DBUG_OFF
  {
    char buf[32];
    DBUG_PRINT("info", ("group_master_log_name='%s', group_master_log_pos=%s",
                        group_master_log_name, llstr(group_master_log_pos, buf)));
    DBUG_PRINT("info", ("group_relay_log_name='%s', group_relay_log_pos=%s",
                        group_relay_log_name, llstr(group_relay_log_pos, buf)));
    DBUG_PRINT("info", ("(%s) log_name='%s', log_pos=%s",
                        until_condition == UNTIL_MASTER_POS ? "master" : "relay",
                        log_name, llstr(log_pos, buf)));
    DBUG_PRINT("info", ("(%s) until_log_name='%s', until_log_pos=%s",
                        until_condition == UNTIL_MASTER_POS ? "master" : "relay",
                        until_log_name, llstr(until_log_pos, buf)));
  }
#endif

  if (until_log_names_cmp_result == UNTIL_LOG_NAMES_CMP_UNKNOWN)
  {
    /*
      We have no cached comparison results so we should compare log names
      and cache result.
      If we are after RESET SLAVE, and the SQL slave thread has not processed
      any event yet, it could be that group_master_log_name is "". In that case,
      just wait for more events (as there is no sensible comparison to do).
    */

    if (*log_name)
    {
      const char *basename= log_name + dirname_length(log_name);

      const char *q= (const char*)(fn_ext(basename)+1);
      if (strncmp(basename, until_log_name, (int)(q-basename)) == 0)
      {
        /* Now compare extensions. */
        char *q_end;
        ulong log_name_extension= strtoul(q, &q_end, 10);
        if (log_name_extension < until_log_name_extension)
          until_log_names_cmp_result= UNTIL_LOG_NAMES_CMP_LESS;
        else
          until_log_names_cmp_result=
            (log_name_extension > until_log_name_extension) ?
            UNTIL_LOG_NAMES_CMP_GREATER : UNTIL_LOG_NAMES_CMP_EQUAL ;
      }
      else
      {
        /* Probably error so we aborting */
        sql_print_error("Slave SQL thread is stopped because UNTIL "
                        "condition is bad.");
        DBUG_RETURN(TRUE);
      }
    }
    else
      DBUG_RETURN(until_log_pos == 0);
  }

  DBUG_RETURN(((until_log_names_cmp_result == UNTIL_LOG_NAMES_CMP_EQUAL &&
           log_pos >= until_log_pos) ||
          until_log_names_cmp_result == UNTIL_LOG_NAMES_CMP_GREATER));
}


void Relay_log_info::cached_charset_invalidate()
{
  DBUG_ENTER("Relay_log_info::cached_charset_invalidate");

  /* Full of zeroes means uninitialized. */
  bzero(cached_charset, sizeof(cached_charset));
  DBUG_VOID_RETURN;
}


bool Relay_log_info::cached_charset_compare(char *charset) const
{
  DBUG_ENTER("Relay_log_info::cached_charset_compare");

  if (memcmp(cached_charset, charset, sizeof(cached_charset)))
  {
    memcpy(const_cast<char*>(cached_charset), charset, sizeof(cached_charset));
    DBUG_RETURN(1);
  }
  DBUG_RETURN(0);
}


void Relay_log_info::stmt_done(my_off_t event_master_log_pos)
{
  clear_flag(IN_STMT);
  DBUG_ASSERT(!belongs_to_client());

  /*
    If in a transaction, and if the slave supports transactions, just
    inc_event_relay_log_pos(). We only have to check for OPTION_BEGIN
    (not OPTION_NOT_AUTOCOMMIT) as transactions are logged with
    BEGIN/COMMIT, not with SET AUTOCOMMIT= .

    CAUTION: opt_using_transactions means innodb || bdb ; suppose the
    master supports InnoDB and BDB, but the slave supports only BDB,
    problems will arise: - suppose an InnoDB table is created on the
    master, - then it will be MyISAM on the slave - but as
    opt_using_transactions is true, the slave will believe he is
    transactional with the MyISAM table. And problems will come when
    one does START SLAVE; STOP SLAVE; START SLAVE; (the slave will
    resume at BEGIN whereas there has not been any rollback).  This is
    the problem of using opt_using_transactions instead of a finer
    "does the slave support _transactional handler used on the
    master_".

    More generally, we'll have problems when a query mixes a
    transactional handler and MyISAM and STOP SLAVE is issued in the
    middle of the "transaction". START SLAVE will resume at BEGIN
    while the MyISAM table has already been updated.
  */
  if ((info_thd->variables.option_bits & OPTION_BEGIN) && opt_using_transactions)
    inc_event_relay_log_pos();
  else
  {
    inc_group_relay_log_pos(event_master_log_pos);
    flush_info(is_transactional() ? TRUE : FALSE);
  }
}

#if !defined(MYSQL_CLIENT) && defined(HAVE_REPLICATION)
void Relay_log_info::cleanup_context(THD *thd, bool error)
{
  DBUG_ENTER("Relay_log_info::cleanup_context");

  DBUG_ASSERT(info_thd == thd);
  /*
    1) Instances of Table_map_log_event, if ::do_apply_event() was called on them,
    may have opened tables, which we cannot be sure have been closed (because
    maybe the Rows_log_event have not been found or will not be, because slave
    SQL thread is stopping, or relay log has a missing tail etc). So we close
    all thread's tables. And so the table mappings have to be cancelled.
    2) Rows_log_event::do_apply_event() may even have started statements or
    transactions on them, which we need to rollback in case of error.
    3) If finding a Format_description_log_event after a BEGIN, we also need
    to rollback before continuing with the next events.
    4) so we need this "context cleanup" function.
  */
  if (error)
  {
    trans_rollback_stmt(thd); // if a "statement transaction"
    trans_rollback(thd);      // if a "real transaction"
    if (rows_query_ev)
    {
      delete rows_query_ev;
      rows_query_ev= NULL;
    }
  }
  m_table_map.clear_tables();
  slave_close_thread_tables(thd);
  if (error)
    thd->mdl_context.release_transactional_locks();
  clear_flag(IN_STMT);
  /*
    Cleanup for the flags that have been set at do_apply_event.
  */
  thd->variables.option_bits&= ~OPTION_NO_FOREIGN_KEY_CHECKS;
  thd->variables.option_bits&= ~OPTION_RELAXED_UNIQUE_CHECKS;
  DBUG_VOID_RETURN;
}

void Relay_log_info::clear_tables_to_lock()
{
  while (tables_to_lock)
  {
    uchar* to_free= reinterpret_cast<uchar*>(tables_to_lock);
    if (tables_to_lock->m_tabledef_valid)
    {
      tables_to_lock->m_tabledef.table_def::~table_def();
      tables_to_lock->m_tabledef_valid= FALSE;
    }
    tables_to_lock=
      static_cast<RPL_TABLE_LIST*>(tables_to_lock->next_global);
    tables_to_lock_count--;
    my_free(to_free);
  }
  DBUG_ASSERT(tables_to_lock == NULL && tables_to_lock_count == 0);
}

void Relay_log_info::slave_close_thread_tables(THD *thd)
{
  thd->stmt_da->can_overwrite_status= TRUE;
  thd->is_error() ? trans_rollback_stmt(thd) : trans_commit_stmt(thd);
  thd->stmt_da->can_overwrite_status= FALSE;

  close_thread_tables(thd);
  /*
    - If inside a multi-statement transaction,
    defer the release of metadata locks until the current
    transaction is either committed or rolled back. This prevents
    other statements from modifying the table for the entire
    duration of this transaction.  This provides commit ordering
    and guarantees serializability across multiple transactions.
    - If in autocommit mode, or outside a transactional context,
    automatically release metadata locks of the current statement.
  */
  if (! thd->in_multi_stmt_transaction_mode())
    thd->mdl_context.release_transactional_locks();
  else
    thd->mdl_context.release_statement_locks();

  clear_tables_to_lock();
}
/**
  Execute a SHOW RELAYLOG EVENTS statement.

  @param thd Pointer to THD object for the client thread executing the
  statement.

  @retval FALSE success
  @retval TRUE failure
*/
bool mysql_show_relaylog_events(THD* thd)
{
  Protocol *protocol= thd->protocol;
  List<Item> field_list;
  DBUG_ENTER("mysql_show_relaylog_events");

  DBUG_ASSERT(thd->lex->sql_command == SQLCOM_SHOW_RELAYLOG_EVENTS);

  Log_event::init_show_field_list(&field_list);
  if (protocol->send_result_set_metadata(&field_list,
                            Protocol::SEND_NUM_ROWS | Protocol::SEND_EOF))
    DBUG_RETURN(TRUE);

  if (!active_mi)
    DBUG_RETURN(TRUE);
  
  DBUG_RETURN(show_binlog_events(thd, &active_mi->rli->relay_log));
}

#endif

int Relay_log_info::init_info()
{
  int error= 0;
  const char *msg= NULL;

  DBUG_ENTER("Relay_log_info::init_info");

  if (inited)
  {
    /*
      We have to reset read position of relay-log-bin as we may have
      already been reading from 'hotlog' when the slave was stopped
      last time. If this case pos_in_file would be set and we would
      get a crash when trying to read the signature for the binary
      relay log.

      We only rewind the read position if we are starting the SQL
      thread. The handle_slave_sql thread assumes that the read
      position is at the beginning of the file, and will read the
      "signature" and then fast-forward to the last position read.
    */
    bool hot_log= FALSE;
    /* 
      my_b_seek does an implicit flush_io_cache, so we need to:

      1. check if this log is active (hot)
      2. if it is we keep log_lock until the seek ends, otherwise 
         release it right away.

      If we did not take log_lock, SQL thread might race with IO
      thread for the IO_CACHE mutex.

    */
    mysql_mutex_t *log_lock= relay_log.get_log_lock();
    mysql_mutex_lock(log_lock);
    hot_log= relay_log.is_active(linfo.log_file_name);

    if (!hot_log)
      mysql_mutex_unlock(log_lock);

    my_b_seek(cur_log, (my_off_t) 0);

    if (hot_log)
      mysql_mutex_unlock(log_lock);

    DBUG_RETURN(0);
  }

  cur_log_fd = -1;
  slave_skip_counter= 0;
  abort_pos_wait= 0;
  log_space_limit= relay_log_space_limit;
  log_space_total= 0;
  tables_to_lock= 0;
  tables_to_lock_count= 0;

  char pattern[FN_REFLEN];
  (void) my_realpath(pattern, slave_load_tmpdir, 0);
  if (fn_format(pattern, PREFIX_SQL_LOAD, pattern, "",
                MY_SAFE_PATH | MY_RETURN_REAL_PATH) == NullS)
  {
    sql_print_error("Unable to use slave's temporary directory '%s'.",
                    slave_load_tmpdir);
    DBUG_RETURN(1);
  }
  unpack_filename(slave_patternload_file, pattern);
  slave_patternload_file_size= strlen(slave_patternload_file);

  /*
    The relay log will now be opened, as a SEQ_READ_APPEND IO_CACHE.
    Note that the I/O thread flushes it to disk after writing every
    event, in flush_info within the master info.
  */
  /*
    For the maximum log size, we choose max_relay_log_size if it is
    non-zero, max_binlog_size otherwise. If later the user does SET
    GLOBAL on one of these variables, fix_max_binlog_size and
    fix_max_relay_log_size will reconsider the choice (for example
    if the user changes max_relay_log_size to zero, we have to
    switch to using max_binlog_size for the relay log) and update
    relay_log.max_size (and mysql_bin_log.max_size).
  */
  {
    /* Reports an error and returns, if the --relay-log's path
       is a directory.*/
    if (opt_relay_logname &&
        opt_relay_logname[strlen(opt_relay_logname) - 1] == FN_LIBCHAR)
    {
      sql_print_error("Path '%s' is a directory name, please specify \
a file name for --relay-log option.", opt_relay_logname);
      DBUG_RETURN(1);
    }

    /* Reports an error and returns, if the --relay-log-index's path
       is a directory.*/
    if (opt_relaylog_index_name &&
        opt_relaylog_index_name[strlen(opt_relaylog_index_name) - 1]
        == FN_LIBCHAR)
    {
      sql_print_error("Path '%s' is a directory name, please specify \
a file name for --relay-log-index option.", opt_relaylog_index_name);
      DBUG_RETURN(1);
    }

    char buf[FN_REFLEN];
    const char *ln;
    static bool name_warning_sent= 0;
    ln= relay_log.generate_name(opt_relay_logname, "-relay-bin",
                                1, buf);
    /* We send the warning only at startup, not after every RESET SLAVE */
    if (!opt_relay_logname && !opt_relaylog_index_name && !name_warning_sent)
    {
      /*
        User didn't give us info to name the relay log index file.
        Picking `hostname`-relay-bin.index like we do, causes replication to
        fail if this slave's hostname is changed later. So, we would like to
        instead require a name. But as we don't want to break many existing
        setups, we only give warning, not error.
      */
      sql_print_warning("Neither --relay-log nor --relay-log-index were used;"
                        " so replication "
                        "may break when this MySQL server acts as a "
                        "slave and has his hostname changed!! Please "
                        "use '--relay-log=%s' to avoid this problem.", ln);
      name_warning_sent= 1;
    }

    relay_log.is_relay_log= TRUE;

    /*
      note, that if open() fails, we'll still have index file open
      but a destructor will take care of that
    */
    if (relay_log.open_index_file(opt_relaylog_index_name, ln, TRUE) ||
        relay_log.open(ln, LOG_BIN, 0, SEQ_READ_APPEND, 0,
                       (max_relay_log_size ? max_relay_log_size :
                        max_binlog_size), 1, TRUE))
    {
      sql_print_error("Failed in open_log() called from Relay_log_info::init_info().");
      DBUG_RETURN(1);
    }
  }

  /*
    This checks if the repository was created before and thus there
    will be values to be read. Please, do not move this call after
    the handler->init_info().
  */
  int necessary_to_configure= check_info();
  if ((error= handler->init_info()))
  {
    msg= "Error reading relay log configuration";
    error= 1;
    goto err;
  }

  if (necessary_to_configure)
  {
    /* Init relay log with first entry in the relay index file */
    if (init_relay_log_pos(NullS, BIN_LOG_HEADER_SIZE, 0 /* no data lock */,
                           &msg, 0))
    {
      error= 1;
      goto err;
    }
    group_master_log_name[0]= 0;
    group_master_log_pos= 0;
  }
  else
  {
    if (read_info(handler))
    {
      msg= "Error reading relay log configuration";
      error= 1;
      goto err;
    }

    if (is_relay_log_recovery && init_recovery(mi, &msg))
    {
      error= 1;
      goto err;
    }

    if (init_relay_log_pos(group_relay_log_name,
                           group_relay_log_pos,
                           0 /* no data lock*/,
                           &msg, 0))
    {
      char llbuf[22];
      sql_print_error("Failed to open the relay log '%s' (relay_log_pos %s).",
                      group_relay_log_name,
                      llstr(group_relay_log_pos, llbuf));
      error= 1;
      goto err;
    }

#ifndef DBUG_OFF
    {
      char llbuf1[22], llbuf2[22];
      DBUG_PRINT("info", ("my_b_tell(cur_log)=%s event_relay_log_pos=%s",
                          llstr(my_b_tell(cur_log),llbuf1),
                          llstr(event_relay_log_pos,llbuf2)));
      DBUG_ASSERT(event_relay_log_pos >= BIN_LOG_HEADER_SIZE);
      DBUG_ASSERT((my_b_tell(cur_log) == event_relay_log_pos));
    }
#endif
  }

  if ((error= flush_info(TRUE)))
  {
    msg= "Error reading relay log configuration";
    error= 1;
    goto err;
  }

  if (count_relay_log_space())
  {
    msg= "Error counting relay log space";
    error= 1;
    goto err;
  }

  inited= 1;
  is_relay_log_recovery= FALSE;
  DBUG_RETURN(error);

err:
  sql_print_error("%s.", msg);
  relay_log.close(LOG_CLOSE_INDEX | LOG_CLOSE_STOP_EVENT);
  DBUG_RETURN(error);
}

void Relay_log_info::end_info()
{
  DBUG_ENTER("Relay_log_info::end_info");

  if (!inited)
    DBUG_VOID_RETURN;

  handler->end_info();

  if (cur_log_fd >= 0)
  {
    end_io_cache(&cache_buf);
    (void)my_close(cur_log_fd, MYF(MY_WME));
    cur_log_fd= -1;
  }
  inited = 0;
  relay_log.close(LOG_CLOSE_INDEX | LOG_CLOSE_STOP_EVENT);
  relay_log.harvest_bytes_written(&log_space_total);
  /*
    Delete the slave's temporary tables from memory.
    In the future there will be other actions than this, to ensure persistance
    of slave's temp tables after shutdown.
  */
  close_temporary_tables();

  DBUG_VOID_RETURN;
}

int Relay_log_info::flush_current_log()
{
  DBUG_ENTER("Relay_log_info::flush_current_log");
  /*
    When we come to this place in code, relay log may or not be initialized;
    the caller is responsible for setting 'flush_relay_log_cache' accordingly.
  */
  IO_CACHE *log_file= relay_log.get_log_file();
  if (flush_io_cache(log_file))
    DBUG_RETURN(2);

  DBUG_RETURN(0);
}

void Relay_log_info::set_master_info(Master_info* info)
{
  mi= info;
}

/**
  Stores the file and position where the execute-slave thread are in the
  relay log:

    - As this is only called by the slave thread or on STOP SLAVE, with the
      log_lock grabbed and the slave thread stopped, we don't need to have
      a lock here.
    - If there is an active transaction, then we don't update the position
      in the relay log.  This is to ensure that we re-execute statements
      if we die in the middle of an transaction that was rolled back.
    - As a transaction never spans binary logs, we don't have to handle the
      case where we do a relay-log-rotation in the middle of the transaction.
      If this would not be the case, we would have to ensure that we
      don't delete the relay log file where the transaction started when
      we switch to a new relay log file.

  @retval  0   ok,
  @retval  1   write error, otherwise.
*/

/**
  Store the file and position where the slave's SQL thread are in the
  relay log.

  Notes:

  - This function should be called either from the slave SQL thread,
    or when the slave thread is not running.  (It reads the
    group_{relay|master}_log_{pos|name} and delay fields in the rli
    object.  These may only be modified by the slave SQL thread or by
    a client thread when the slave SQL thread is not running.)

  - If there is an active transaction, then we do not update the
    position in the relay log.  This is to ensure that we re-execute
    statements if we die in the middle of an transaction that was
    rolled back.

  - As a transaction never spans binary logs, we don't have to handle
    the case where we do a relay-log-rotation in the middle of the
    transaction.  If transactions could span several binlogs, we would
    have to ensure that we do not delete the relay log file where the
    transaction started before switching to a new relay log file.

  - Error can happen if writing to file fails or if flushing the file
    fails.

  @param rli The object representing the Relay_log_info.

  @todo Change the log file information to a binary format to avoid
  calling longlong2str.

  @return 0 on success, 1 on error.
*/
int Relay_log_info::flush_info(bool force)
{
  DBUG_ENTER("Relay_log_info::flush_info");

  /* 
    We update the sync_period at this point because only here we
    now that we are handling a relay log info. This needs to be
    update every time we call flush because the option maybe 
    dinamically set.
  */
  handler->set_sync_period(sync_relayloginfo_period);

  if (write_info(handler, force))
    goto err;

  DBUG_RETURN(0);

err:
  sql_print_error("Error writing relay log configuration.");
  DBUG_RETURN(1);
}

size_t Relay_log_info::get_number_info_rli_fields() 
{ 
  return sizeof(info_rli_fields)/sizeof(info_rli_fields[0]);
}

bool Relay_log_info::read_info(Rpl_info_handler *from)
{
  int lines= 0;
  char *first_non_digit= NULL;
  ulong temp_group_relay_log_pos= 0;
  ulong temp_group_master_log_pos= 0;
  int temp_sql_delay= 0;

  DBUG_ENTER("Relay_log_info::read_info");

  /*
    Should not read RLI from file in client threads. Client threads
    only use RLI to execute BINLOG statements.

    @todo Uncomment the following assertion. Currently,
    Relay_log_info::init() is called from init_master_info() before
    the THD object Relay_log_info::sql_thd is created. That means we
    cannot call belongs_to_client() since belongs_to_client()
    dereferences Relay_log_info::sql_thd. So we need to refactor
    slightly: the THD object should be created by Relay_log_info
    constructor (or passed to it), so that we are guaranteed that it
    exists at this point. /Sven
  */
  //DBUG_ASSERT(!belongs_to_client());

  /*
    Starting from 5.1.x, relay-log.info has a new format. Now, its
    first line contains the number of lines in the file. By reading
    this number we can determine which version our master.info comes
    from. We can't simply count the lines in the file, since
    versions before 5.1.x could generate files with more lines than
    needed. If first line doesn't contain a number, or if it
    contains a number less than LINES_IN_RELAY_LOG_INFO_WITH_DELAY,
    then the file is treated like a file from pre-5.1.x version.
    There is no ambiguity when reading an old master.info: before
    5.1.x, the first line contained the binlog's name, which is
    either empty or has an extension (contains a '.'), so can't be
    confused with an integer.

    So we're just reading first line and trying to figure which
    version is this.
  */

  /*
    The first row is temporarily stored in mi->master_log_name, if
    it is line count and not binlog name (new format) it will be
    overwritten by the second row later.
  */
  if (from->prepare_info_for_read() ||
      from->get_info(group_relay_log_name, sizeof(group_relay_log_name), ""))
    DBUG_RETURN(TRUE);

  lines= strtoul(group_relay_log_name, &first_non_digit, 10);

  if (group_relay_log_name[0]!='\0' &&
      *first_non_digit=='\0' && lines >= LINES_IN_RELAY_LOG_INFO_WITH_DELAY)
  {
    /* Seems to be new format => read group relay log name */
    if (from->get_info(group_relay_log_name,  sizeof(group_relay_log_name), ""))
      DBUG_RETURN(TRUE);
  }
  else
     DBUG_PRINT("info", ("relay_log_info file is in old format."));

  if (from->get_info((ulong *) &temp_group_relay_log_pos,
                        (ulong) BIN_LOG_HEADER_SIZE) ||
      from->get_info(group_master_log_name,
                        sizeof(group_relay_log_name), "") ||
      from->get_info((ulong *) &temp_group_master_log_pos,
                        (ulong) 0))
    DBUG_RETURN(TRUE);

  if (lines >= LINES_IN_RELAY_LOG_INFO_WITH_DELAY)
  {
    if (from->get_info((int *) &temp_sql_delay,(int) 0))
      DBUG_RETURN(TRUE);
  }

  group_relay_log_pos=  temp_group_relay_log_pos;
  group_master_log_pos= temp_group_master_log_pos;
  sql_delay= (int32) temp_sql_delay;

  DBUG_RETURN(FALSE);
}

bool Relay_log_info::write_info(Rpl_info_handler *to, bool force)
{
  DBUG_ENTER("Relay_log_info::write_info");

  /*
    @todo Uncomment the following assertion. See todo in
    Relay_log_info::read_info() for details. /Sven
  */
  //DBUG_ASSERT(!belongs_to_client());

  if (to->prepare_info_for_write() ||
      to->set_info((int) LINES_IN_RELAY_LOG_INFO_WITH_DELAY) ||
      to->set_info(group_relay_log_name) ||
      to->set_info((ulong) group_relay_log_pos) ||
      to->set_info(group_master_log_name) ||
      to->set_info((ulong) group_master_log_pos) ||
      to->set_info((int) sql_delay))
    DBUG_RETURN(TRUE);

  if (to->flush_info(force))
    DBUG_RETURN(TRUE);

  DBUG_RETURN(FALSE);
}

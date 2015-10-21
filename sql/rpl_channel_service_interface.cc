/* Copyright (c) 2015, Oracle and/or its affiliates. All rights reserved.

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

#include "my_global.h"
#include "log.h"
#include "rpl_channel_service_interface.h"

#include "current_thd.h"
#include "mysqld.h"          // opt_mts_slave_parallel_workers
#include "rpl_slave.h"
#include "rpl_info_factory.h"
#include "rpl_mi.h"
#include "rpl_msr.h"         /* Multisource replication */
#include "rpl_rli.h"
#include "rpl_rli_pdb.h"
#include "mysqld_thd_manager.h" // Global_THD_manager
#include "sql_parse.h"          // Find_thd_with_id

int initialize_channel_service_interface()
{
  DBUG_ENTER("initialize_channel_service_interface");

  //master info and relay log repositories must be TABLE
  if (opt_mi_repository_id != INFO_REPOSITORY_TABLE ||
      opt_rli_repository_id != INFO_REPOSITORY_TABLE)
  {
    sql_print_error("For the creation of replication channels the master info"
                    " and relay log info repositories must be set to TABLE");
    DBUG_RETURN(1);
  }

  //server id must be different from 0
  if (server_id == 0)
  {
    sql_print_error("For the creation of replication channels the server id"
                    " must be different from 0");
    DBUG_RETURN(1);
  }

  DBUG_RETURN(0);
}

#ifdef HAVE_REPLICATION

static void set_mi_settings(Master_info *mi, Channel_creation_info* channel_info)
{
  mysql_mutex_lock(&mi->data_lock);

  mi->rli->set_thd_tx_priority(channel_info->thd_tx_priority);

  mi->rli->replicate_same_server_id=
    (channel_info->replicate_same_server_id == RPL_SERVICE_SERVER_DEFAULT) ?
     replicate_same_server_id : channel_info->replicate_same_server_id;

  mi->rli->opt_slave_parallel_workers=
    (channel_info->channel_mts_parallel_workers == RPL_SERVICE_SERVER_DEFAULT) ?
    opt_mts_slave_parallel_workers : channel_info->channel_mts_parallel_workers;

  if (channel_info->channel_mts_parallel_type == RPL_SERVICE_SERVER_DEFAULT)
  {
    if (mts_parallel_option == MTS_PARALLEL_TYPE_DB_NAME)
      mi->rli->channel_mts_submode = MTS_PARALLEL_TYPE_DB_NAME;
    else
      mi->rli->channel_mts_submode = MTS_PARALLEL_TYPE_LOGICAL_CLOCK;
  }
  else
  {
   if (channel_info->channel_mts_parallel_type == CHANNEL_MTS_PARALLEL_TYPE_DB_NAME)
      mi->rli->channel_mts_submode = MTS_PARALLEL_TYPE_DB_NAME;
    else
      mi->rli->channel_mts_submode = MTS_PARALLEL_TYPE_LOGICAL_CLOCK;
  }

  mi->rli->checkpoint_group=
    (channel_info->channel_mts_checkpoint_group == RPL_SERVICE_SERVER_DEFAULT) ?
    opt_mts_checkpoint_group : channel_info->channel_mts_checkpoint_group;

  mi->set_mi_description_event(new Format_description_log_event(BINLOG_VERSION));

  mysql_mutex_unlock(&mi->data_lock);
}

static bool init_thread_context()
{
  return my_thread_init();
}

static void clean_thread_context()
{
  my_thread_end();
}

static THD *create_surrogate_thread()
{
  THD *thd= NULL;
  thd= new THD;
  thd->thread_stack= (char*) &thd;
  thd->store_globals();
  thd->security_context()->skip_grants();

  return(thd);
}

static void delete_surrogate_thread(THD *thd)
{
  thd->release_resources();
  delete thd;
  my_thread_set_THR_THD(NULL);
}

void
initialize_channel_creation_info(Channel_creation_info* channel_info)
{
  channel_info->type= SLAVE_REPLICATION_CHANNEL;
  channel_info->hostname= 0;
  channel_info->port= 0;
  channel_info->user= 0;
  channel_info->password= 0;
  channel_info->ssl_info= 0;
  channel_info->auto_position= RPL_SERVICE_SERVER_DEFAULT;
  channel_info->channel_mts_parallel_type= RPL_SERVICE_SERVER_DEFAULT;
  channel_info->channel_mts_parallel_workers= RPL_SERVICE_SERVER_DEFAULT;
  channel_info->channel_mts_checkpoint_group= RPL_SERVICE_SERVER_DEFAULT;
  channel_info->replicate_same_server_id= RPL_SERVICE_SERVER_DEFAULT;
  channel_info->thd_tx_priority= 0;
  channel_info->sql_delay= RPL_SERVICE_SERVER_DEFAULT;
  channel_info->preserve_relay_logs= false;
  channel_info->retry_count= 0;
  channel_info->connect_retry= 0;
}

void initialize_channel_ssl_info(Channel_ssl_info* channel_ssl_info)
{
  channel_ssl_info->use_ssl= 0;
  channel_ssl_info->ssl_ca_file_name= 0;
  channel_ssl_info->ssl_ca_directory= 0;
  channel_ssl_info->ssl_cert_file_name= 0;
  channel_ssl_info->ssl_crl_file_name= 0;
  channel_ssl_info->ssl_crl_directory= 0;
  channel_ssl_info->ssl_key= 0;
  channel_ssl_info->ssl_cipher= 0;
  channel_ssl_info->ssl_verify_server_cert= 0;
}

void
initialize_channel_connection_info(Channel_connection_info* channel_info)
{
  channel_info->until_condition= CHANNEL_NO_UNTIL_CONDITION;
  channel_info->gtid= 0;
  channel_info->view_id= 0;
}

static void set_mi_ssl_options(LEX_MASTER_INFO* lex_mi, Channel_ssl_info* channel_ssl_info)
{

  if (channel_ssl_info->use_ssl)
  {
    lex_mi->ssl= LEX_MASTER_INFO::LEX_MI_ENABLE;
  }

  if (channel_ssl_info->ssl_ca_file_name != NULL)
  {
    lex_mi->ssl_ca= channel_ssl_info->ssl_ca_file_name;
  }

  if (channel_ssl_info->ssl_ca_directory != NULL)
  {
    lex_mi->ssl_capath= channel_ssl_info->ssl_ca_file_name;
  }

  if (channel_ssl_info->ssl_cert_file_name != NULL)
  {
    lex_mi->ssl_cert= channel_ssl_info->ssl_cert_file_name;
  }

  if (channel_ssl_info->ssl_crl_file_name != NULL)
  {
    lex_mi->ssl_crl= channel_ssl_info->ssl_crl_file_name;
  }

  if (channel_ssl_info->ssl_crl_directory != NULL)
  {
    lex_mi->ssl_crlpath= channel_ssl_info->ssl_crl_directory;
  }

  if (channel_ssl_info->ssl_key != NULL)
  {
    lex_mi->ssl_key= channel_ssl_info->ssl_key;
  }

  if (channel_ssl_info->ssl_cipher != NULL)
  {
    lex_mi->ssl_cipher= channel_ssl_info->ssl_cipher;
  }

  if (channel_ssl_info->ssl_verify_server_cert)
  {
    lex_mi->ssl_verify_server_cert= LEX_MASTER_INFO::LEX_MI_ENABLE;
  }
}

int channel_create(const char* channel,
                   Channel_creation_info* channel_info)
{
  DBUG_ENTER("channel_create");

  Master_info *mi= NULL;
  int error= 0;
  LEX_MASTER_INFO* lex_mi= NULL;

  bool thd_created= false;
  THD *thd= current_thd;

  //Don't create default channels
  if (!strcmp(channel_map.get_default_channel(), channel))
    DBUG_RETURN(RPL_CHANNEL_SERVICE_DEFAULT_CHANNEL_CREATION_ERROR);

  /* Service channels are not supposed to use sql_slave_skip_counter */
  mysql_mutex_lock(&LOCK_sql_slave_skip_counter);
  if (sql_slave_skip_counter > 0)
    error= RPL_CHANNEL_SERVICE_SLAVE_SKIP_COUNTER_ACTIVE;
  mysql_mutex_unlock(&LOCK_sql_slave_skip_counter);
  if (error)
    DBUG_RETURN(error);

  channel_map.wrlock();

  /* Get the Master_info of the channel */
  mi= channel_map.get_mi(channel);

    /* create a new channel if doesn't exist */
  if (!mi)
  {
    if ((error= add_new_channel(&mi, channel,
                                channel_info->type)))
        goto err;
  }

  lex_mi= new st_lex_master_info();
  lex_mi->channel= channel;
  lex_mi->host= channel_info->hostname;
  lex_mi->port= channel_info->port;
  lex_mi->user= channel_info->user;
  lex_mi->password= channel_info->password;
  lex_mi->sql_delay= channel_info->sql_delay;
  lex_mi->connect_retry= channel_info->connect_retry;
  if (channel_info->retry_count)
  {
    lex_mi->retry_count_opt= LEX_MASTER_INFO::LEX_MI_ENABLE;
    lex_mi->retry_count= channel_info->retry_count;
  }

  if (channel_info->auto_position)
  {
    lex_mi->auto_position= LEX_MASTER_INFO::LEX_MI_ENABLE;
    if (mi && mi->is_auto_position())
    {
      //So change master allows new configurations with a running SQL thread
      lex_mi->auto_position= LEX_MASTER_INFO::LEX_MI_UNCHANGED;
    }
  }

  if (channel_info->ssl_info != NULL)
  {
    set_mi_ssl_options(lex_mi, channel_info->ssl_info);
  }

  if (mi)
  {
    if (!thd)
    {
      thd_created= true;
      thd= create_surrogate_thread();
    }

    if ((error= change_master(thd, mi, lex_mi,
                              channel_info->preserve_relay_logs)))
    {
      goto err;
    }
  }

  set_mi_settings(mi, channel_info);

err:
  channel_map.unlock();

  if (thd_created)
  {
    delete_surrogate_thread(thd);
  }

  delete lex_mi;

  DBUG_RETURN(error);
}

int channel_start(const char* channel,
                  Channel_connection_info* connection_info,
                  int threads_to_start,
                  int wait_for_connection)
{
  DBUG_ENTER("channel_start(channel, threads_to_start, wait_for_connection");
  int error= 0;
  int thread_mask= 0;
  LEX_MASTER_INFO lex_mi;
  ulong thread_start_id= 0;
  bool thd_created= false;
  THD* thd= current_thd;

  /* Service channels are not supposed to use sql_slave_skip_counter */
  mysql_mutex_lock(&LOCK_sql_slave_skip_counter);
  if (sql_slave_skip_counter > 0)
    error= RPL_CHANNEL_SERVICE_SLAVE_SKIP_COUNTER_ACTIVE;
  mysql_mutex_unlock(&LOCK_sql_slave_skip_counter);
  if (error)
    DBUG_RETURN(error);

  channel_map.wrlock();

  Master_info *mi= channel_map.get_mi(channel);

  if (mi == NULL)
  {
    error= RPL_CHANNEL_SERVICE_CHANNEL_DOES_NOT_EXISTS_ERROR;
    goto err;
  }

  if (threads_to_start & CHANNEL_APPLIER_THREAD)
  {
    thread_mask |= SLAVE_SQL;
  }
  if (threads_to_start & CHANNEL_RECEIVER_THREAD)
  {
    thread_mask |= SLAVE_IO;
  }

  //Nothing to be done here
  if (!thread_mask)
    goto err;

  LEX_SLAVE_CONNECTION lex_connection;
  lex_connection.reset();

  if (connection_info->until_condition != CHANNEL_NO_UNTIL_CONDITION)
  {
    switch (connection_info->until_condition)
    {
      case CHANNEL_UNTIL_APPLIER_AFTER_GTIDS:
        lex_mi.gtid_until_condition= LEX_MASTER_INFO::UNTIL_SQL_AFTER_GTIDS;
        lex_mi.gtid= connection_info->gtid;
        break;
      case CHANNEL_UNTIL_APPLIER_BEFORE_GTIDS:
        lex_mi.gtid_until_condition= LEX_MASTER_INFO::UNTIL_SQL_BEFORE_GTIDS;
        lex_mi.gtid= connection_info->gtid;
        break;
      case CHANNEL_UNTIL_APPLIER_AFTER_GAPS:
        lex_mi.until_after_gaps= true;
        break;
      case CHANNEL_UNTIL_VIEW_ID:
        DBUG_ASSERT((thread_mask & SLAVE_SQL) && connection_info->view_id);
        lex_mi.view_id= connection_info->view_id;
        break;
      default:
        DBUG_ASSERT(0);
    }
  }

  if (wait_for_connection && (thread_mask & SLAVE_IO))
    thread_start_id= mi->slave_run_id;

  if (!thd)
  {
    thd_created= true;
    thd= create_surrogate_thread();
  }

  error= start_slave(thd, &lex_connection, &lex_mi,
                     thread_mask, mi, false);

  if (wait_for_connection && (thread_mask & SLAVE_IO) && !error)
  {
    mysql_mutex_lock(&mi->run_lock);
    /*
      If the ids are still equal this means the start thread method did not
      wait for the thread to start
    */
    while (thread_start_id == mi->slave_run_id)
    {
      mysql_cond_wait(&mi->start_cond, &mi->run_lock);
    }
    mysql_mutex_unlock(&mi->run_lock);

    while (mi->slave_running != MYSQL_SLAVE_RUN_CONNECT)
    {
      //If there is such a state change then there was an error on connection
      if (mi->slave_running == MYSQL_SLAVE_NOT_RUN)
      {
        error= RPL_CHANNEL_SERVICE_RECEIVER_CONNECTION_ERROR;
        break;
      }
      my_sleep(100);
    }
  }

err:
  channel_map.unlock();

  if (thd_created)
  {
    delete_surrogate_thread(thd);
  }

  DBUG_RETURN(error);
}

int channel_stop(const char* channel,
                 int threads_to_stop,
                 long timeout)
{
  DBUG_ENTER("channel_stop(channel, stop_receiver, stop_applier, timeout");

  channel_map.rdlock();

  Master_info *mi= channel_map.get_mi(channel);

  if (mi == NULL)
  {
    channel_map.unlock();
    DBUG_RETURN(RPL_CHANNEL_SERVICE_CHANNEL_DOES_NOT_EXISTS_ERROR);
  }

  mi->channel_rdlock();

  int thread_mask= 0;
  int server_thd_mask= 0;
  lock_slave_threads(mi);

  init_thread_mask(&server_thd_mask, mi, 0 /* not inverse*/);

  if ((threads_to_stop & CHANNEL_APPLIER_THREAD)
          && (server_thd_mask & SLAVE_SQL))
  {
    thread_mask |= SLAVE_SQL;
  }
  if ((threads_to_stop & CHANNEL_RECEIVER_THREAD)
          && (server_thd_mask & SLAVE_IO))
  {
    thread_mask |= SLAVE_IO;
  }

  if (thread_mask == 0)
  {
    mi->channel_unlock();
    channel_map.unlock();
    DBUG_RETURN(0);
  }

  bool thd_init= init_thread_context();

  int error= terminate_slave_threads(mi, thread_mask, timeout, false);
  unlock_slave_threads(mi);

  mi->channel_unlock();
  channel_map.unlock();

  if (thd_init)
  {
    clean_thread_context();
  }

  DBUG_RETURN(error);
}

int channel_purge_queue(const char* channel, bool reset_all)
{
  DBUG_ENTER("channel_purge_queue(channel, only_purge");

  channel_map.wrlock();

  Master_info *mi= channel_map.get_mi(channel);

  if (mi == NULL)
  {
    channel_map.unlock();
    DBUG_RETURN(RPL_CHANNEL_SERVICE_CHANNEL_DOES_NOT_EXISTS_ERROR);
  }

  bool thd_init= init_thread_context();

  int error= reset_slave(current_thd, mi, reset_all);

  channel_map.unlock();

  if (thd_init)
  {
    clean_thread_context();
  }

  DBUG_RETURN(error);
}

bool channel_is_active(const char* channel, enum_channel_thread_types thd_type)
{
  int thread_mask= 0;
  DBUG_ENTER("channel_is_active(channel, thd_type");

  channel_map.rdlock();

  Master_info *mi= channel_map.get_mi(channel);

  if (mi == NULL)
  {
    channel_map.unlock();
    DBUG_RETURN(false);
  }

  mi->channel_rdlock();

  init_thread_mask(&thread_mask, mi, 0 /* not inverse*/);

  mi->channel_unlock();
  channel_map.unlock();

  switch(thd_type)
  {
    case CHANNEL_NO_THD:
      DBUG_RETURN(true); //return true as the channel exists
    case CHANNEL_RECEIVER_THREAD:
      DBUG_RETURN(thread_mask & SLAVE_IO);
    case CHANNEL_APPLIER_THREAD:
      DBUG_RETURN(thread_mask & SLAVE_SQL);
    default:
      DBUG_ASSERT(0);
  }
  DBUG_RETURN(false);
}

int channel_get_thread_id(const char* channel,
                          enum_channel_thread_types thd_type,
                          unsigned long** thread_id)
{
  DBUG_ENTER("channel_get_thread_id(channel, thread_type ,*thread_id");

  int number_threads= -1;

  channel_map.rdlock();

  Master_info *mi= channel_map.get_mi(channel);

  if (mi == NULL)
  {
    channel_map.unlock();
    DBUG_RETURN(RPL_CHANNEL_SERVICE_CHANNEL_DOES_NOT_EXISTS_ERROR);
  }

  mi->channel_rdlock();

  switch(thd_type)
  {
    case CHANNEL_RECEIVER_THREAD:
      mysql_mutex_lock(&mi->info_thd_lock);
      if (mi->info_thd != NULL)
      {
        *thread_id= (unsigned long*) my_malloc(PSI_NOT_INSTRUMENTED,
                                               sizeof(unsigned long),
                                               MYF(MY_WME));
        **thread_id= mi->info_thd->thread_id();
        number_threads= 1;
      }
      mysql_mutex_unlock(&mi->info_thd_lock);
      break;
    case CHANNEL_APPLIER_THREAD:
      if (mi->rli != NULL)
      {
        mysql_mutex_lock(&mi->rli->run_lock);

        if (mi->rli->slave_parallel_workers > 0)
        {
          // Parallel applier.
          size_t num_workers= mi->rli->get_worker_count();
          number_threads= 1 + num_workers;
          *thread_id=
              (unsigned long*) my_malloc(PSI_NOT_INSTRUMENTED,
                                         number_threads * sizeof(unsigned long),
                                         MYF(MY_WME));
          unsigned long *thread_id_pointer= *thread_id;

          // Set default values on thread_id array.
          for (int i= 0; i < number_threads; i++, thread_id_pointer++)
            *thread_id_pointer= -1;
          thread_id_pointer= *thread_id;

          // Coordinator thread id.
          if (mi->rli->info_thd != NULL)
          {
            mysql_mutex_lock(&mi->rli->info_thd_lock);
            *thread_id_pointer= mi->rli->info_thd->thread_id();
            mysql_mutex_unlock(&mi->rli->info_thd_lock);
            thread_id_pointer++;
          }

          // Workers thread id.
          if (mi->rli->workers_array_initialized)
          {
            for (size_t i= 0; i < num_workers; i++, thread_id_pointer++)
            {
              Slave_worker* worker= mi->rli->get_worker(i);
              if (worker != NULL)
              {
                mysql_mutex_lock(&worker->jobs_lock);
                if (worker->info_thd != NULL &&
                    worker->running_status != Slave_worker::NOT_RUNNING)
                {
                  mysql_mutex_lock(&worker->info_thd_lock);
                  *thread_id_pointer= worker->info_thd->thread_id();
                  mysql_mutex_unlock(&worker->info_thd_lock);
                }
                mysql_mutex_unlock(&worker->jobs_lock);
              }
            }
          }
        }
        else
        {
          // Sequential applier.
          if (mi->rli->info_thd != NULL)
          {
            *thread_id= (unsigned long*) my_malloc(PSI_NOT_INSTRUMENTED,
                                                     sizeof(unsigned long),
                                                     MYF(MY_WME));
            mysql_mutex_lock(&mi->rli->info_thd_lock);
            **thread_id= mi->rli->info_thd->thread_id();
            mysql_mutex_unlock(&mi->rli->info_thd_lock);
            number_threads= 1;
          }
        }
        mysql_mutex_unlock(&mi->rli->run_lock);
      }
      break;
    default:
      DBUG_RETURN(number_threads);
  }

  mi->channel_unlock();
  channel_map.unlock();

  DBUG_RETURN(number_threads);
}

long long channel_get_last_delivered_gno(const char* channel, int sidno)
{
  DBUG_ENTER("channel_get_last_delivered_gno(channel, sidno)");

  channel_map.rdlock();

  Master_info *mi= channel_map.get_mi(channel);

  if (mi == NULL)
  {
    channel_map.unlock();
    DBUG_RETURN(RPL_CHANNEL_SERVICE_CHANNEL_DOES_NOT_EXISTS_ERROR);
  }

  mi->channel_rdlock();
  rpl_gno last_gno= 0;

  global_sid_lock->rdlock();
  last_gno= mi->rli->get_gtid_set()->get_last_gno(sidno);
  global_sid_lock->unlock();

#if !defined(DBUG_OFF)
  const Gtid_set *retrieved_gtid_set= mi->rli->get_gtid_set();
  char *retrieved_gtid_set_string= NULL;
  global_sid_lock->wrlock();
  retrieved_gtid_set->to_string(&retrieved_gtid_set_string);
  global_sid_lock->unlock();
  DBUG_PRINT("info", ("get_last_delivered_gno retrieved_set_string: %s",
                      retrieved_gtid_set_string));
  my_free(retrieved_gtid_set_string);
#endif

  mi->channel_unlock();
  channel_map.unlock();

  DBUG_RETURN(last_gno);
}

int channel_queue_packet(const char* channel,
                         const char* buf,
                         unsigned long event_len)
{
  int result;
  DBUG_ENTER("channel_queue_packet(channel, event_buffer, event_len)");

  channel_map.rdlock();

  Master_info *mi= channel_map.get_mi(channel);

  if (mi == NULL)
  {
    channel_map.unlock();
    DBUG_RETURN(RPL_CHANNEL_SERVICE_CHANNEL_DOES_NOT_EXISTS_ERROR);
  }

  result= queue_event(mi, buf, event_len);

  channel_map.unlock();

  DBUG_RETURN(result);
}

int channel_wait_until_apply_queue_applied(char* channel, long long timeout)
{
  DBUG_ENTER("channel_wait_until_apply_queue_applied(channel, timeout)");

  channel_map.rdlock();

  Master_info *mi= channel_map.get_mi(channel);

  if (mi == NULL)
  {
    channel_map.unlock();
    DBUG_RETURN(RPL_CHANNEL_SERVICE_CHANNEL_DOES_NOT_EXISTS_ERROR);
  }

  channel_map.unlock();

  int error = mi->rli->wait_for_gtid_set(current_thd, mi->rli->get_gtid_set(),
                                         timeout);

  if (error == -1)
    DBUG_RETURN(REPLICATION_THREAD_WAIT_TIMEOUT_ERROR);
  if (error == -2)
    DBUG_RETURN(REPLICATION_THREAD_WAIT_NO_INFO_ERROR);

  DBUG_RETURN(error);
}

int channel_is_applier_waiting(char* channel)
{
  DBUG_ENTER("channel_is_applier_waiting(channel)");
  int result= RPL_CHANNEL_SERVICE_CHANNEL_DOES_NOT_EXISTS_ERROR;

  channel_map.rdlock();

  Master_info *mi= channel_map.get_mi(channel);

  if (mi == NULL)
  {
    channel_map.unlock();
    DBUG_RETURN(result);
  }

  unsigned long* thread_ids= NULL;
  int number_appliers= channel_get_thread_id(channel,
                                             CHANNEL_APPLIER_THREAD,
                                             &thread_ids);

  if (number_appliers <= 0)
  {
    goto end;
  }

  if (number_appliers == 1)
  {
    result= channel_is_applier_thread_waiting(*thread_ids);
  }
  else if (number_appliers > 1)
  {
    int waiting= 0;

    // Check if coordinator is waiting.
    waiting += channel_is_applier_thread_waiting(thread_ids[0]);

    // Check if workers are waiting.
    for (int i= 1; i < number_appliers; i++)
      waiting += channel_is_applier_thread_waiting(thread_ids[i], true);

    // Check if all are waiting.
    if (waiting == number_appliers)
      result= 1;
    else
      result= 0;
  }

end:
  channel_map.unlock();
  my_free(thread_ids);

  DBUG_RETURN(result);
}

int channel_is_applier_thread_waiting(unsigned long thread_id, bool worker)
{
  DBUG_ENTER("channel_is_applier_thread_waiting(thread_id, worker)");
  bool result= -1;

  Find_thd_with_id find_thd_with_id(thread_id);
  THD *thd= Global_THD_manager::get_instance()->find_thd(&find_thd_with_id);
  if (thd)
  {
    result= 0;

    const char *proc_info= thd->get_proc_info();
    if (proc_info)
    {
      const char* stage_name= stage_slave_has_read_all_relay_log.m_name;
      if (worker)
        stage_name= stage_slave_waiting_event_from_coordinator.m_name;

      if (!strcmp(proc_info, stage_name))
        result= 1;
    }
    mysql_mutex_unlock(&thd->LOCK_thd_data);
  }

  DBUG_RETURN(result);
}

int channel_flush(const char* channel)
{
  DBUG_ENTER("channel_flush(channel)");

  channel_map.rdlock();

  Master_info *mi= channel_map.get_mi(channel);

  if (mi == NULL)
  {
    channel_map.unlock();
    DBUG_RETURN(RPL_CHANNEL_SERVICE_CHANNEL_DOES_NOT_EXISTS_ERROR);
  }

  bool error= flush_relay_logs(mi);

  channel_map.unlock();

  DBUG_RETURN(error ? 1 : 0);
}

#endif /* HAVE_REPLICATION */

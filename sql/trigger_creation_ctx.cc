#include "my_global.h"
#include "trigger_creation_ctx.h"
#include "sql_db.h" // get_default_db_collation()

Trigger_creation_ctx *
Trigger_creation_ctx::create(THD *thd,
                             const char *db_name,
                             const char *table_name,
                             const LEX_STRING *client_cs_name,
                             const LEX_STRING *connection_cl_name,
                             const LEX_STRING *db_cl_name)
{
  const CHARSET_INFO *client_cs;
  const CHARSET_INFO *connection_cl;
  const CHARSET_INFO *db_cl;

  bool invalid_creation_ctx= FALSE;

  if (resolve_charset(client_cs_name->str,
                      thd->variables.character_set_client,
                      &client_cs))
  {
    sql_print_warning("Trigger for table '%s'.'%s': "
                      "invalid character_set_client value (%s).",
                      (const char *) db_name,
                      (const char *) table_name,
                      (const char *) client_cs_name->str);

    invalid_creation_ctx= TRUE;
  }

  if (resolve_collation(connection_cl_name->str,
                        thd->variables.collation_connection,
                        &connection_cl))
  {
    sql_print_warning("Trigger for table '%s'.'%s': "
                      "invalid collation_connection value (%s).",
                      (const char *) db_name,
                      (const char *) table_name,
                      (const char *) connection_cl_name->str);

    invalid_creation_ctx= TRUE;
  }

  if (resolve_collation(db_cl_name->str, NULL, &db_cl))
  {
    sql_print_warning("Trigger for table '%s'.'%s': "
                      "invalid database_collation value (%s).",
                      (const char *) db_name,
                      (const char *) table_name,
                      (const char *) db_cl_name->str);

    invalid_creation_ctx= TRUE;
  }

  if (invalid_creation_ctx)
  {
    push_warning_printf(thd,
                        Sql_condition::SL_WARNING,
                        ER_TRG_INVALID_CREATION_CTX,
                        ER(ER_TRG_INVALID_CREATION_CTX),
                        (const char *) db_name,
                        (const char *) table_name);
  }

  /*
    If we failed to resolve the database collation, load the default one
    from the disk.
  */

  if (!db_cl)
    db_cl= get_default_db_collation(thd, db_name);

  return new Trigger_creation_ctx(client_cs, connection_cl, db_cl);
}

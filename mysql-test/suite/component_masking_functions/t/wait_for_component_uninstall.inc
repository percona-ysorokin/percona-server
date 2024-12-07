# When flusher thread is enabled in masking functions component,
# 'UNINSTALL COMPONENT' statement is allowed to fail (in case when run too
# early and MySQL session in the background thread has not been initialized
# yet).

# This .inc file is supposed to make several attempts to execute
# 'UNINSTALL COMPONENT' in a loop until this statement succeeds.

--disable_query_log
--let $counter = 30
--let $uninstall_result = 1
while ($uninstall_result)
{
  --error 0,ER_COMPONENTS_UNLOAD_CANT_DEINITIALIZE
  UNINSTALL COMPONENT 'file://component_masking_functions';
  --let $uninstall_result = $mysql_errno

  if ($uninstall_result)
  {
    --dec $counter
    if (!$counter) {
      --let $uninstall_result = 0
    }
    --sleep 1
  }
}
if (!$counter)
{
  --die Unable to uninstall component
}
if ($counter)
{
  --echo Component successfully uninstalled
}
--enable_query_log

--let $indexed =
--let $generated_storage_type = VIRTUAL
--source suite/innodb/include/xtradb_compressed_columns_virtual.inc

--let $indexed =
--let $generated_storage_type = STORED
--source suite/innodb/include/xtradb_compressed_columns_virtual.inc

--let $indexed = 1
--let $generated_storage_type = VIRTUAL
--source suite/innodb/include/xtradb_compressed_columns_virtual.inc

--let $indexed = 1
--let $generated_storage_type = STORED
--source suite/innodb/include/xtradb_compressed_columns_virtual.inc



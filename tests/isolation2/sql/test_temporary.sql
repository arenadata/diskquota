1: CREATE SCHEMA temporary_schema;
1: SET search_path TO temporary_schema;
1: SELECT diskquota.set_schema_quota('temporary_schema', '1 MB');
1: SELECT diskquota.wait_for_worker_new_epoch();
1: CREATE TEMPORARY TABLE temporary_table(id int) DISTRIBUTED BY (id);
1: INSERT INTO temporary_table SELECT generate_series(1, 100000);
1: SELECT diskquota.wait_for_worker_new_epoch();
1q:

!\retcode gpstop -afr;

1: SELECT pg_table_size('temporary_table'::regclass::oid) size;
1: SELECT size FROM diskquota.table_size WHERE NOT EXISTS (SELECT 1 FROM pg_class WHERE tableid = oid) AND segid = -1;
1: DROP SCHEMA temporary_schema CASCADE;
1q:

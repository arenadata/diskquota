1: CREATE SCHEMA dropped_schema;
1: SET search_path TO dropped_schema;
1: SELECT diskquota.set_schema_quota('dropped_schema', '1 MB');
1: SELECT diskquota.wait_for_worker_new_epoch();
1: CREATE TABLE dropped_table(id int) DISTRIBUTED BY (id);
1: INSERT INTO dropped_table SELECT generate_series(1, 100000);
1: SELECT diskquota.wait_for_worker_new_epoch();
1: DROP TABLE dropped_table;
1q:

!\retcode gpstop -afr;

1: SELECT pg_table_size('dropped_table'::regclass::oid) size;
1: SELECT size FROM diskquota.table_size WHERE NOT EXISTS (SELECT 1 FROM pg_class WHERE tableid = oid) AND segid = -1;
1: DROP SCHEMA dropped_schema CASCADE;
1q:

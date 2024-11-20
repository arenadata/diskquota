-- check that diskquota ignores global flag appendonly=true

-- for GPDB 6
SET gp_default_storage_options='appendonly=true';
-- for GPDB 7
SET default_table_access_method='ao_row';

CREATE EXTENSION diskquota;
DROP EXTENSION diskquota;

-- for GPDB 6
SET gp_default_storage_options='appendonly=false';
-- for GPDB 7
SET default_table_access_method='heap';

CREATE EXTENSION diskquota;

SELECT diskquota.init_table_size_table();

-- Wait after init so that diskquota.state is clean
SELECT diskquota.wait_for_worker_new_epoch();

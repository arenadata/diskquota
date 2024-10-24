-- check that diskquota ignores global flag appendonly=true

SET gp_default_storage_options='appendonly=true';

CREATE EXTENSION diskquota;
DROP EXTENSION diskquota;

SET gp_default_storage_options='appendonly=false';

CREATE EXTENSION diskquota;

SELECT diskquota.init_table_size_table();

-- Wait after init so that diskquota.state is clean
SELECT diskquota.wait_for_worker_new_epoch();

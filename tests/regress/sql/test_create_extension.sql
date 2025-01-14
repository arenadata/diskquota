-- check that diskquota ignores global flag appendonly=true

-- start_ignore
SELECT CASE
    WHEN current_setting('server_version_num')::int > 120000
    THEN set_config('default_table_access_method', 'ao_row', false)
    ELSE set_config('gp_default_storage_options', 'appendonly=true', false)
END;
-- end_ignore

CREATE EXTENSION diskquota;
DROP EXTENSION diskquota;

-- start_ignore
SELECT CASE
    WHEN current_setting('server_version_num')::int > 120000
    THEN set_config('default_table_access_method', 'heap', false)
    ELSE set_config('gp_default_storage_options', 'appendonly=false', false)
END;
-- end_ignore

CREATE EXTENSION diskquota;

SELECT diskquota.init_table_size_table();

-- Wait after init so that diskquota.state is clean
SELECT diskquota.wait_for_worker_new_epoch();
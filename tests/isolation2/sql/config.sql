--start_ignore
CREATE DATABASE diskquota;
--end_ignore

!\retcode gpconfig -c shared_preload_libraries -v "$(psql -At -c "SELECT array_to_string(array_append(string_to_array(regexp_replace(current_setting('shared_preload_libraries'), '(,{0,1})diskquota(.*)\.so', ''), ','), '$(./data/current_binary_name)'), ',')" postgres)";
!\retcode gpconfig -c diskquota.naptime -v 0 --skipvalidation;
!\retcode gpconfig -c max_worker_processes -v 20 --skipvalidation;

!\retcode gpstop -raf;

-- Show the values of all GUC variables
--start_ignore
-- naptime cannot be 0 for release build
1: SHOW diskquota.naptime;
--end_ignore
1: SHOW diskquota.max_active_tables;
1: SHOW diskquota.worker_timeout;

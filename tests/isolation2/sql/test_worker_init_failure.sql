--
-- Tests for error handling when the worker catches the error during
-- its first run.
--

-- Function checking whether worker on given db is up
CREATE or REPLACE FUNCTION check_worker_presence(dbname text)
 RETURNS TABLE (
  is_alive boolean
  )
AS $$
   SELECT CASE WHEN EXISTS
   (SELECT 1 FROM pg_stat_activity
   WHERE datname = dbname AND application_name LIKE
   '%diskquota%') THEN TRUE else FALSE END; $$
LANGUAGE sql;

-- Test diskquota behavior when an error occurs during the worker's first run.
-- The error leads to process termination. Launcher gets SIGUSR1 when its
-- worker terminates and launcher won't start it again until extension reload
-- or SIGHUP signal.
CREATE EXTENSION diskquota;
SELECT gp_inject_fault('diskquota_worker_initialization', 'error', dbid)
  FROM gp_segment_configuration WHERE role='p' AND content=-1;
SELECT gp_inject_fault('diskquota_launcher_sigusr1', 'skip', '', '', '', 2, 3, 0, dbid)
  FROM gp_segment_configuration WHERE role='p' AND content=-1;
SELECT gp_inject_fault('diskquota_launcher_sighup', 'skip', dbid)
  FROM gp_segment_configuration WHERE role='p' AND content=-1;

SELECT diskquota.init_table_size_table();
SELECT gp_inject_fault('diskquota_launcher_sigusr1', 'wait_until_triggered', dbid)
  FROM gp_segment_configuration WHERE role='p' AND content=-1;
SELECT check_worker_presence(current_database());

-- Reload configuration and check that worker is up again
!\retcode gpstop -u;
SELECT gp_inject_fault('diskquota_launcher_sighup', 'wait_until_triggered', dbid)
  FROM gp_segment_configuration WHERE role='p' AND content=-1;
SELECT check_worker_presence(current_database());

SELECT gp_inject_fault('diskquota_worker_initialization', 'reset', dbid)
  FROM gp_segment_configuration WHERE role='p' AND content=-1;
SELECT gp_inject_fault('diskquota_launcher_sigusr1', 'reset', dbid)
  FROM gp_segment_configuration WHERE role='p' AND content=-1;
SELECT gp_inject_fault('diskquota_launcher_sighup', 'reset', dbid)
  FROM gp_segment_configuration WHERE role='p' AND content=-1;
DROP EXTENSION diskquota;

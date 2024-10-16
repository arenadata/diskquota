-- table in 'diskquota not enabled database' should not be activetable
\! gpconfig -c diskquota.max_active_tables -v 5 > /dev/null
\! gpconfig -c diskquota.naptime -v 1 > /dev/null
\! gpstop -arf > /dev/null

\c

CREATE DATABASE test_tablenum_limit_01;
CREATE DATABASE test_tablenum_limit_02;

\c test_tablenum_limit_01

CREATE TABLE a01(i int) DISTRIBUTED BY (i);
CREATE TABLE a02(i int) DISTRIBUTED BY (i);
CREATE TABLE a03(i int) DISTRIBUTED BY (i);

INSERT INTO a01 values(generate_series(0, 500));
INSERT INTO a02 values(generate_series(0, 500));
INSERT INTO a03 values(generate_series(0, 500));

\c test_tablenum_limit_02
CREATE EXTENSION diskquota;
-- we only read the current log file
CREATE EXTERNAL WEB TABLE segment_logs(line text)
    EXECUTE 'cat $GP_SEG_DATADIR/pg_log/$(ls -Art $GP_SEG_DATADIR/pg_log | tail -n 1)'
    ON ALL FORMAT 'TEXT' (DELIMITER 'OFF');

CREATE SCHEMA s;
SELECT diskquota.set_schema_quota('s', '1 MB');

SELECT diskquota.wait_for_worker_new_epoch();

-- We create twice as many tables as the limit to ensure that the active_tables table is overflow.
CREATE TABLE s.t1 (a int, b int) DISTRIBUTED BY (a)
    PARTITION BY RANGE (b) ( START (0) END (10) EVERY (1) );

SELECT count(*) FROM segment_logs WHERE line LIKE '%the number of active tables reached the limit%';

CREATE TABLE s.t2(i int) DISTRIBUTED BY (i);
INSERT INTO s.t2 SELECT generate_series(1, 100000);

SELECT diskquota.wait_for_worker_new_epoch();

INSERT INTO s.t1 SELECT a, a from generate_series(0, 9)a; -- should be successful
SELECT count(*) FROM s.t1;

-- altered reloid cache overflow check. expected warning.
VACUUM FULL;

SELECT count(*) FROM segment_logs WHERE line LIKE '%the number of altered reloid cache entries reached the limit%';

DROP EXTENSION diskquota;

\c contrib_regression
DROP DATABASE test_tablenum_limit_01;
DROP DATABASE test_tablenum_limit_02;

\! gpconfig -r diskquota.max_active_tables > /dev/null
\! gpconfig -c diskquota.naptime -v 0 > /dev/null
\! gpstop -arf > /dev/null

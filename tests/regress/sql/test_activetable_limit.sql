-- table in 'diskquota not enabled database' should not be activetable
\! gpconfig -c diskquota.max_active_tables -v 5 > /dev/null
\! gpconfig -c diskquota.naptime -v 2 > /dev/null
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
CREATE SCHEMA s;
SELECT diskquota.set_schema_quota('s', '1 MB');

SELECT diskquota.wait_for_worker_new_epoch();

-- the other two active tables are in a different database
CREATE TABLE s.t1(i int) DISTRIBUTED BY (i); -- activetable = 3
CREATE TABLE s.t2(i int) DISTRIBUTED BY (i); -- activetable = 4
CREATE TABLE s.t3(i int) DISTRIBUTED BY (i); -- activetable = 5. expected warning.
CREATE TABLE s.t4(i int) DISTRIBUTED BY (i);

INSERT INTO s.t4 SELECT generate_series(1, 100000);

SELECT diskquota.wait_for_worker_new_epoch();

INSERT INTO s.t1 SELECT generate_series(1, 10); -- should be successful
select count(*) from s.t1;

-- altered reloid cache overflow check. expected warning.
vacuum full;

DROP EXTENSION diskquota;

\c contrib_regression
DROP DATABASE test_tablenum_limit_01;
DROP DATABASE test_tablenum_limit_02;

\! gpconfig -r diskquota.max_active_tables > /dev/null
\! gpconfig -c diskquota.naptime -v 0 > /dev/null
\! gpstop -arf > /dev/null

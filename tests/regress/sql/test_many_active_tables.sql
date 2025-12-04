CREATE TABLE t1 (pk int, val int)
DISTRIBUTED BY (pk)
PARTITION BY RANGE (pk) (START (1) END (500) EVERY (1));

INSERT INTO t1 
SELECT pk, val
FROM generate_series(1, 10000) AS val, generate_series(1, 499) AS pk;

SELECT diskquota.wait_for_worker_new_epoch();

SELECT count(*) >= 499 FROM diskquota.table_size WHERE size > 0;

DROP TABLE t1;

SELECT diskquota.wait_for_worker_new_epoch();

SELECT count(*) < 499 FROM diskquota.table_size WHERE size > 0;

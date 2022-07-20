CREATE SCHEMA s1;

SELECT diskquota.set_schema_quota('s1', '1MB');

CREATE TABLE s1.a(i int);

INSERT INTO s1.a SELECT generate_series(1,100);

-- overflow, but not detected yet
INSERT INTO s1.a SELECT generate_series(1,10000000);

SELECT pg_sleep(5);

INSERT INTO s1.a SELECT generate_series(1,100);

SELECT diskquota.set_schema_quota('s1', '-1');

SELECT pg_sleep(5);
INSERT INTO s1.a SELECT generate_series(1,100);

DROP SCHEMA s1 CASCADE;

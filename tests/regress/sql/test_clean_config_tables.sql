-- Test auto cleaning of configuration tables (diskquota.quota_config and
-- diskquota.target) when schema, tablespace or role is dropped.

-- start_ignore
DROP SCHEMA IF EXISTS s1, s2, s3 CASCADE;
DROP ROLE IF EXISTS r1, r2, r3;
DROP TABLESPACE IF EXISTS ts1;
DROP TABLESPACE IF EXISTS ts2;
DROP TABLESPACE IF EXISTS ts3;
-- end_ignore
\! rm -rf /tmp/ts1 /tmp/ts2 /tmp/ts3
TRUNCATE TABLE diskquota.target;
TRUNCATE TABLE diskquota.quota_config;


-- NAMESPACE_QUOTA

CREATE SCHEMA s1;
CREATE SCHEMA s2;
CREATE SCHEMA s3;

SELECT diskquota.set_schema_quota(s, '1GB')
FROM (VALUES('s1'), ('s2'), ('s3')) v(s);

DROP SCHEMA s2;

SELECT diskquota.wait_for_worker_new_epoch();

SELECT n.nspname
FROM diskquota.quota_config c
LEFT JOIN pg_namespace n ON n.oid = c.targetoid
WHERE c.quotatype = 0;


-- ROLE_QUOTA

CREATE ROLE r1;
CREATE ROLE r2;
CREATE ROLE r3;

SELECT diskquota.set_role_quota(r, '100GB')
FROM (VALUES('r1'), ('r2'), ('r3')) v(r);

DROP ROLE r2;

SELECT diskquota.wait_for_worker_new_epoch();

SELECT a.rolname
FROM diskquota.quota_config c
LEFT JOIN pg_authid a ON a.oid = c.targetoid
WHERE c.quotatype = 1;


-- TABLESPACE_QUOTA

\! mkdir -p /tmp/ts1
\! mkdir -p /tmp/ts2
\! mkdir -p /tmp/ts3
CREATE TABLESPACE ts1 LOCATION '/tmp/ts1';
CREATE TABLESPACE ts2 LOCATION '/tmp/ts2';
CREATE TABLESPACE ts3 LOCATION '/tmp/ts3';

SELECT diskquota.set_per_segment_quota(ts, '10.0')
FROM (VALUES('ts1'), ('ts2'), ('ts3')) v(ts);

DROP TABLESPACE ts2;

SELECT diskquota.wait_for_worker_new_epoch();

SELECT s.spcname
FROM diskquota.quota_config c
LEFT JOIN pg_tablespace s ON s.oid = c.targetoid
WHERE c.quotatype = 4;


-- NAMESPACE_TABLESPACE_QUOTA

SELECT diskquota.set_schema_tablespace_quota(s, ts, '1GB')
FROM (VALUES ('s1'), ('s3')) AS vs(s), (VALUES ('ts1'), ('ts3')) AS vts(ts);

DROP SCHEMA s1;

SELECT diskquota.wait_for_worker_new_epoch();

SELECT count() FROM diskquota.quota_config WHERE quotatype = 2;

SELECT s.spcname, n.nspname
FROM diskquota.target c
LEFT JOIN pg_tablespace s ON s.oid = c.tablespaceoid
LEFT JOIN pg_namespace n ON n.oid = c.primaryoid
WHERE c.quotatype = 2;

DROP TABLESPACE ts1;

SELECT diskquota.wait_for_worker_new_epoch();

SELECT count() FROM diskquota.quota_config WHERE quotatype = 2;

SELECT s.spcname, n.nspname
FROM diskquota.target c
LEFT JOIN pg_tablespace s ON s.oid = c.tablespaceoid
LEFT JOIN pg_namespace n ON n.oid = c.primaryoid
WHERE c.quotatype = 2;


-- ROLE_TABLESPACE_QUOTA

CREATE TABLESPACE ts1 LOCATION '/tmp/ts1';

SELECT diskquota.set_role_tablespace_quota(r, ts, '1GB')
FROM (VALUES ('r1'), ('r3')) AS vr(r), (VALUES ('ts1'), ('ts3')) AS vts(ts);

DROP ROLE r1;

SELECT diskquota.wait_for_worker_new_epoch();

SELECT count() FROM diskquota.quota_config WHERE quotatype = 3;

SELECT s.spcname, a.rolname
FROM diskquota.target c
LEFT JOIN pg_tablespace s ON s.oid = c.tablespaceoid
LEFT JOIN pg_authid a ON a.oid = c.primaryoid
WHERE c.quotatype = 3;

DROP TABLESPACE ts1;

SELECT diskquota.wait_for_worker_new_epoch();

SELECT count() FROM diskquota.quota_config WHERE quotatype = 3;

SELECT s.spcname, a.rolname
FROM diskquota.target c
LEFT JOIN pg_tablespace s ON s.oid = c.tablespaceoid
LEFT JOIN pg_authid a ON a.oid = c.primaryoid
WHERE c.quotatype = 3;


-- Test invalid entries cleaning

TRUNCATE TABLE diskquota.target;
TRUNCATE TABLE diskquota.quota_config;

INSERT INTO diskquota.target VALUES (0, 0, 0, 0);
INSERT INTO diskquota.quota_config VALUES (0, 0, 0, 0);

SELECT diskquota.wait_for_worker_new_epoch();

SELECT count() FROM diskquota.target;
SELECT count() FROM diskquota.quota_config;


-- cleanup

TRUNCATE TABLE diskquota.target;
TRUNCATE TABLE diskquota.quota_config;
DROP SCHEMA s3;
DROP ROLE r3;
DROP TABLESPACE ts3;
\! rm -rf /tmp/ts1 /tmp/ts2 /tmp/ts3

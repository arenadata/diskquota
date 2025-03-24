-- start_ignore
DROP SCHEMA IF EXISTS s1 CASCADE;
-- end_ignore
CREATE SCHEMA s1;

DO $$BEGIN
    PERFORM diskquota.set_schema_quota('s1', '1 MB');
END$$;

DROP SCHEMA s1;

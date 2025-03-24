CREATE SCHEMA s1;

DO $plpgsql$BEGIN
    PERFORM diskquota.set_schema_quota('s1', '1 MB');
END;$plpgsql$;

DROP SCHEMA s1

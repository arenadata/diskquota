/* -------------------------------------------------------------------------
 *
 * gp_activetable.c
 *
 * This code is responsible for detecting active table for databases
 * quotamodel will call append_active_tables() to fetch the active tables
 * and their size information in each loop.
 *
 * Copyright (c) 2018-2020 Pivotal Software, Inc.
 * Copyright (c) 2020-Present VMware, Inc. or its affiliates
 *
 * IDENTIFICATION
 *		diskquota/gp_activetable.c
 *
 * -------------------------------------------------------------------------
 */
#include "postgres.h"

#include "access/htup_details.h"
#if GP_VERSION_NUM >= 70000
#include "access/relation.h"
#endif /* GP_VERSION_NUM */
#include "access/xact.h"
#include "catalog/catalog.h"
#include "catalog/objectaccess.h"
#include "catalog/pg_extension.h"
#include "cdb/cdbdisp_query.h"
#include "cdb/cdbdispatchresult.h"
#include "cdb/cdbvars.h"
#include "commands/dbcommands.h"
#include "commands/extension.h"
#include "executor/spi.h"
#include "funcapi.h"
#include "libpq-fe.h"
#include "storage/smgr.h"
#include "utils/faultinjector.h"
#include "utils/lsyscache.h"
#include "utils/syscache.h"
#include "utils/inval.h"

#include "gp_activetable.h"
#include "diskquota.h"
#include "relation_cache.h"

PG_FUNCTION_INFO_V1(diskquota_fetch_table_stat);

/* The results set cache for SRF call*/
typedef struct DiskQuotaSetOFCache
{
	HTAB           *result;
	HASH_SEQ_STATUS pos;
} DiskQuotaSetOFCache;

static HTAB *active_tables_map                      = NULL; // Set<DiskQuotaActiveTableFileEntry>
TimestampTz  active_tables_map_last_overflow_report = 0;

static const char *active_tables_map_warning =
        "the number of active tables reached the limit, please increase "
        "the GUC value for diskquota.max_active_tables.";

/*
 * monitored_dbid_cache is a allow list for diskquota
 * to know which databases it need to monitor.
 *
 * dbid will be added to it when creating diskquota extension
 * dbid will be removed from it when droping diskquota extension
 */
static HTAB       *altered_reloid_cache                      = NULL; // Set<Oid>
static TimestampTz altered_reloid_cache_last_overflow_report = 0;

static const char *altered_reloid_cache_warning =
        "the number of altered reloid cache entries reached the limit, please increase "
        "the GUC value for diskquota.max_active_tables.";

/* active table hooks which detect the disk file size change. */
static file_create_hook_type   prev_file_create_hook   = NULL;
static file_extend_hook_type   prev_file_extend_hook   = NULL;
static file_truncate_hook_type prev_file_truncate_hook = NULL;
static file_unlink_hook_type   prev_file_unlink_hook   = NULL;
static object_access_hook_type prev_object_access_hook = NULL;

static void active_table_hook_smgrcreate(RelFileNodeBackend rnode);
static void active_table_hook_smgrextend(RelFileNodeBackend rnode);
static void active_table_hook_smgrtruncate(RelFileNodeBackend rnode);
static void active_table_hook_smgrunlink(RelFileNodeBackend rnode);
static void object_access_hook_QuotaStmt(ObjectAccessType access, Oid classId, Oid objectId, int subId, void *arg);

static HTAB *get_active_tables_stats(ArrayType *array);
static HTAB *get_active_tables_oid(void);
static void  report_active_table_helper(const RelFileNodeBackend *relFileNode);
static void  remove_from_active_table_map(const RelFileNodeBackend *relFileNode);
static void  report_relation_cache_helper(Oid relid);
static void  report_altered_reloid(Oid reloid);
static Oid   get_dbid(ArrayType *array);

void init_active_table_hook(void);
void init_shm_worker_active_tables(void);
void init_lock_active_tables(void);

/*
 * Init active_tables_map shared memory
 */
void
init_shm_worker_active_tables(void)
{
	HASHCTL ctl;

	memset(&ctl, 0, sizeof(ctl));
	ctl.keysize       = sizeof(DiskQuotaActiveTableFileEntry);
	ctl.entrysize     = sizeof(DiskQuotaActiveTableFileEntry);
	active_tables_map = DiskquotaShmemInitHash("active_tables", diskquota_max_active_tables,
	                                           diskquota_max_active_tables, &ctl, HASH_ELEM, DISKQUOTA_TAG_HASH);

	memset(&ctl, 0, sizeof(ctl));
	ctl.keysize          = sizeof(Oid);
	ctl.entrysize        = sizeof(Oid);
	altered_reloid_cache = DiskquotaShmemInitHash("altered_reloid_cache", diskquota_max_active_tables,
	                                              diskquota_max_active_tables, &ctl, HASH_ELEM, DISKQUOTA_OID_HASH);
}

/*
 * Register disk file size change hook to detect active table.
 */
void
init_active_table_hook(void)
{
	prev_file_create_hook = file_create_hook;
	file_create_hook      = active_table_hook_smgrcreate;

	prev_file_extend_hook = file_extend_hook;
	file_extend_hook      = active_table_hook_smgrextend;

	prev_file_truncate_hook = file_truncate_hook;
	file_truncate_hook      = active_table_hook_smgrtruncate;

	prev_file_unlink_hook = file_unlink_hook;
	file_unlink_hook      = active_table_hook_smgrunlink;

	prev_object_access_hook = object_access_hook;
	object_access_hook      = object_access_hook_QuotaStmt;
}

/*
 * File create hook is used to monitor a new file create event
 */
static void
active_table_hook_smgrcreate(RelFileNodeBackend rnode)
{
	if (prev_file_create_hook) (*prev_file_create_hook)(rnode);

	SIMPLE_FAULT_INJECTOR("diskquota_after_smgrcreate");
	report_active_table_helper(&rnode);
}

/*
 * File extend hook is used to monitor file size extend event
 * it could be extending a page for heap table or just monitoring
 * file write for an append-optimize table.
 */
static void
active_table_hook_smgrextend(RelFileNodeBackend rnode)
{
	if (prev_file_extend_hook) (*prev_file_extend_hook)(rnode);

	report_active_table_helper(&rnode);
	quota_check_common(InvalidOid /*reloid*/, &rnode.node);
}

/*
 * File truncate hook is used to monitor a new file truncate event
 */
static void
active_table_hook_smgrtruncate(RelFileNodeBackend rnode)
{
	if (prev_file_truncate_hook) (*prev_file_truncate_hook)(rnode);

	report_active_table_helper(&rnode);
}

static void
active_table_hook_smgrunlink(RelFileNodeBackend rnode)
{
	if (prev_file_unlink_hook) (*prev_file_unlink_hook)(rnode);

	/*
	 * Since we do not remove the relfilenode if it does not map to any valid
	 * relation oid, we need to do the cleaning here to avoid memory leak
	 */
	remove_from_active_table_map(&rnode);
	remove_cache_entry(InvalidOid, rnode.node.relNode);
}

static void
object_access_hook_QuotaStmt(ObjectAccessType access, Oid classId, Oid objectId, int subId, void *arg)
{
	if (prev_object_access_hook) (*prev_object_access_hook)(access, classId, objectId, subId, arg);

	/* if is 'drop extension diskquota' */
	if (classId == ExtensionRelationId && access == OAT_DROP)
	{
		if (get_extension_oid("diskquota", true) == objectId)
		{
			invalidate_database_rejectmap(MyDatabaseId);
			diskquota_stop_worker();
		}
		return;
	}

	/* TODO: do we need to use "&&" instead of "||"? */
	if (classId != RelationRelationId || subId != 0)
	{
		return;
	}

	if (objectId < FirstNormalObjectId)
	{
		return;
	}

	switch (access)
	{
		case OAT_POST_CREATE:
			report_relation_cache_helper(objectId);
			break;
		case OAT_POST_ALTER:
			SIMPLE_FAULT_INJECTOR("object_access_post_alter");
			report_altered_reloid(objectId);
			break;
		default:
			break;
	}
}

static void
report_altered_reloid(Oid reloid)
{
	/*
	 * We don't collect altered relations' reloid on mirrors
	 * and QD.
	 */
	if (IsRoleMirror() || IS_QUERY_DISPATCHER()) return;

	LWLockAcquire(diskquota_locks.altered_reloid_cache_lock, LW_EXCLUSIVE);
	HASHACTION action = check_hash_fullness(altered_reloid_cache, diskquota_max_active_tables,
	                                        altered_reloid_cache_warning, &altered_reloid_cache_last_overflow_report);
	hash_search(altered_reloid_cache, &reloid, action, NULL);
	LWLockRelease(diskquota_locks.altered_reloid_cache_lock);
}

static void
report_relation_cache_helper(Oid relid)
{
	bool     found;
	Relation rel;
	char     relkind;

	/* We do not collect the active table in mirror segments  */
	if (IsRoleMirror())
	{
		return;
	}

	/*
	 * Do not collect active table info when the database is not under monitoring.
	 * this operation is read-only and does not require absolutely exact.
	 * read the cache with out shared lock.
	 */
	LWLockAcquire(diskquota_locks.monitored_dbid_cache_lock, LW_SHARED);
	hash_search(monitored_dbid_cache, &MyDatabaseId, HASH_FIND, &found);
	LWLockRelease(diskquota_locks.monitored_dbid_cache_lock);
	if (!found)
	{
		return;
	}

	rel = diskquota_relation_open(relid);
	if (rel == NULL)
	{
		return;
	}

	relkind = rel->rd_rel->relkind;

	RelationClose(rel);

	if (relkind != RELKIND_FOREIGN_TABLE && relkind != RELKIND_COMPOSITE_TYPE && relkind != RELKIND_VIEW)
		update_relation_cache(relid);
}

/*
 * Common function for reporting active tables
 * Currently, any file events(create, extend. truncate) are
 * treated the same and report_active_table_helper just put
 * the corresponding relFileNode into the active_tables_map
 */
static void
report_active_table_helper(const RelFileNodeBackend *relFileNode)
{
	DiskQuotaActiveTableFileEntry *entry;
	DiskQuotaActiveTableFileEntry  item;
	bool                           found = false;
	Oid                            dbid  = relFileNode->node.dbNode;

	/* We do not collect the active table in mirror segments  */
	if (IsRoleMirror())
	{
		return;
	}

	LWLockAcquire(diskquota_locks.monitored_dbid_cache_lock, LW_SHARED);
	/* do not collect active table info when the database is not under monitoring.
	 * this operation is read-only and does not require absolutely exact.
	 * read the cache with out shared lock */
	hash_search(monitored_dbid_cache, &dbid, HASH_FIND, &found);
	LWLockRelease(diskquota_locks.monitored_dbid_cache_lock);
	if (!found)
	{
		return;
	}
	found = false;

	MemSet(&item, 0, sizeof(DiskQuotaActiveTableFileEntry));
	item.dbid          = relFileNode->node.dbNode;
	item.relfilenode   = relFileNode->node.relNode;
	item.tablespaceoid = relFileNode->node.spcNode;

	LWLockAcquire(diskquota_locks.active_table_lock, LW_EXCLUSIVE);
	HASHACTION action = check_hash_fullness(active_tables_map, diskquota_max_active_tables, active_tables_map_warning,
	                                        &active_tables_map_last_overflow_report);
	entry             = hash_search(active_tables_map, &item, action, &found);
	if (entry && !found) *entry = item;

	LWLockRelease(diskquota_locks.active_table_lock);
}

/*
 * Remove relfilenode from the active table map if exists.
 */
static void
remove_from_active_table_map(const RelFileNodeBackend *relFileNode)
{
	DiskQuotaActiveTableFileEntry item = {0};

	item.dbid          = relFileNode->node.dbNode;
	item.relfilenode   = relFileNode->node.relNode;
	item.tablespaceoid = relFileNode->node.spcNode;

	LWLockAcquire(diskquota_locks.active_table_lock, LW_EXCLUSIVE);
	hash_search(active_tables_map, &item, HASH_REMOVE, NULL);
	LWLockRelease(diskquota_locks.active_table_lock);
}

/*
 * Interface of activetable module
 * This function is called by quotamodel module.
 * Disk quota worker process need to collect
 * active table disk usage from all the segments.
 * And aggregate the table size on each segment
 * to get the real table size at cluster level.
 */
void
append_active_tables(StringInfo sql, bool is_init)
{
	if (is_init)
	{
		/*
		 * Load table size info from diskquota.table_size table.
		 * This is called when system startup, disk quota rejectmap
		 * and other shared memory will be warmed up by table_size table.
		 */
		appendStringInfoString(
		        sql, "select tableid, array_agg(size order by segid) size from diskquota.table_size group by 1");
	}
	else
	{
		/*
		 * Get active table size from all the segments based on
		 * active table oid list.
		 * Function diskquota_fetch_table_stat is called to calculate
		 * the table size on the fly.
		 */
		CdbPgResults cdb_pgresults = {NULL, 0};
		uint32       count         = 0;

		appendStringInfo(sql, "with s as (select (diskquota.diskquota_fetch_table_stat(1, '{");

		/*
		 * Get active table list from all the segments.
		 * Since when loading data, there is case where only subset for
		 * segment doing the real loading. As a result, the same table
		 * maybe active on some segments while not active on others. We
		 * haven't store the table size for each segment on master(to save
		 * memory), so when re-calculate the table size, we need to sum the
		 * table size on all of the segments.
		 * First get all oid of tables which are active table on any segment,
		 * any errors will be catch in upper level.
		 */
		CdbDispatchCommand("select * from diskquota.diskquota_fetch_table_stat(0, '{}'::oid[])", DF_NONE,
		                   &cdb_pgresults);

		for (int i = 0; i < cdb_pgresults.numResults; i++)
		{
			PGresult *pgresult = cdb_pgresults.pg_results[i];

			if (PQresultStatus(pgresult) != PGRES_TUPLES_OK)
			{
				cdbdisp_clearCdbPgResults(&cdb_pgresults);
				ereport(ERROR,
				        (errmsg("[diskquota] fetching active tables, encounter unexpected result from segment: %d",
				                PQresultStatus(pgresult))));
			}

			/* push the active table oid into local_active_table_oid_map */
			for (int j = 0; j < PQntuples(pgresult); j++)
			{
				if (count++ > 0) appendStringInfoString(sql, ",");
				appendStringInfoString(sql, PQgetvalue(pgresult, j, 0));
			}
		}

		cdbdisp_clearCdbPgResults(&cdb_pgresults);

		appendStringInfo(
		        sql,
		        "}'::oid[])).* from gp_dist_random('gp_id') "
		        ") select "
		        "	\"TABLE_OID\" tableid, "
		        "	array[sum(\"TABLE_SIZE\")::bigint] || array_agg(\"TABLE_SIZE\" order by \"GP_SEGMENT_ID\") size "
		        "from s group by 1");
	}
}

/*
 * Function to get the table size from each segments
 * There are 4 modes:
 *
 * - FETCH_ACTIVE_OID: gather active table oid from all the segments, since
 * table may only be modified on a subset of the segments, we need to firstly
 * gather the active table oid list from all the segments.
 *
 * - FETCH_ACTIVE_SIZE: calculate the active table size based on the active
 * table oid list.
 *
 * - ADD_DB_TO_MONITOR: add MyDatabaseId to the monitored db cache so that
 * active tables in the current database will be recorded. This is used each
 * time a worker starts.
 *
 * - REMOVE_DB_FROM_BEING_MONITORED: remove MyDatabaseId from the monitored
 * db cache so that active tables in the current database will be recorded.
 * This is used when DROP EXTENSION.
 */
Datum
diskquota_fetch_table_stat(PG_FUNCTION_ARGS)
{
	FuncCallContext *funcctx;
	int32            mode = PG_GETARG_INT32(0);
	AttInMetadata   *attinmeta;
	bool             isFirstCall = true;
	Oid              dbid;

	HTAB                      *localCacheTable = NULL;
	DiskQuotaSetOFCache       *cache           = NULL;
	DiskQuotaActiveTableEntry *results_entry   = NULL;

#ifdef FAULT_INJECTOR
	if (SIMPLE_FAULT_INJECTOR("ereport_warning_from_segment") == FaultInjectorTypeSkip)
	{
		ereport(WARNING, (errmsg("[Fault Injector] This is a warning reported from segment")));
	}
#endif

	/* Init the container list in the first call and get the results back */
	if (SRF_IS_FIRSTCALL())
	{
		MemoryContext oldcontext;
		TupleDesc     tupdesc;
		int           ret_code = SPI_connect();
		if (ret_code != SPI_OK_CONNECT)
		{
			ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
			                errmsg("unable to connect to execute internal query. return code: %d.", ret_code)));
		}
		SPI_finish();

		/* create a function context for cross-call persistence */
		funcctx = SRF_FIRSTCALL_INIT();

		/* switch to memory context appropriate for multiple function calls */
		oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);

		if (Gp_role == GP_ROLE_DISPATCH || Gp_role == GP_ROLE_UTILITY)
		{
			ereport(ERROR, (errmsg("This function must not be called on master or by user")));
		}

		switch (mode)
		{
			case FETCH_ACTIVE_OID:
				localCacheTable = get_active_tables_oid();
				break;
			case FETCH_ACTIVE_SIZE:
				localCacheTable = get_active_tables_stats(PG_GETARG_ARRAYTYPE_P(1));
				break;
			/*TODO: add another UDF to update the monitored_db_cache */
			case ADD_DB_TO_MONITOR:
				dbid = get_dbid(PG_GETARG_ARRAYTYPE_P(1));
				update_monitor_db(dbid, ADD_DB_TO_MONITOR);
				PG_RETURN_NULL();
			case REMOVE_DB_FROM_BEING_MONITORED:
				dbid = get_dbid(PG_GETARG_ARRAYTYPE_P(1));
				update_monitor_db(dbid, REMOVE_DB_FROM_BEING_MONITORED);
				PG_RETURN_NULL();
			case PAUSE_DB_TO_MONITOR:
				dbid = get_dbid(PG_GETARG_ARRAYTYPE_P(1));
				update_monitor_db(dbid, PAUSE_DB_TO_MONITOR);
				PG_RETURN_NULL();
			case RESUME_DB_TO_MONITOR:
				dbid = get_dbid(PG_GETARG_ARRAYTYPE_P(1));
				update_monitor_db(dbid, RESUME_DB_TO_MONITOR);
				PG_RETURN_NULL();
			default:
				ereport(ERROR, (errmsg("Unused mode number %d, transaction will be aborted", mode)));
				break;
		}

		/*
		 * total number of active tables to be returned, each tuple contains
		 * one active table stat
		 */
		funcctx->max_calls = localCacheTable ? (uint32)hash_get_num_entries(localCacheTable) : 0;

		/*
		 * prepare attribute metadata for next calls that generate the tuple
		 */
		tupdesc = DiskquotaCreateTemplateTupleDesc(3);
		TupleDescInitEntry(tupdesc, (AttrNumber)1, "TABLE_OID", OIDOID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber)2, "TABLE_SIZE", INT8OID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber)3, "GP_SEGMENT_ID", INT2OID, -1, 0);

		attinmeta          = TupleDescGetAttInMetadata(tupdesc);
		funcctx->attinmeta = attinmeta;

		/* Prepare SetOf results HATB */
		cache         = (DiskQuotaSetOFCache *)palloc(sizeof(DiskQuotaSetOFCache));
		cache->result = localCacheTable;
		hash_seq_init(&(cache->pos), localCacheTable);

		MemoryContextSwitchTo(oldcontext);
	}
	else
	{
		isFirstCall = false;
	}

	funcctx = SRF_PERCALL_SETUP();

	if (isFirstCall)
	{
		funcctx->user_fctx = (void *)cache;
	}
	else
	{
		cache = (DiskQuotaSetOFCache *)funcctx->user_fctx;
	}

	/* return the results back to SPI caller */
	while ((results_entry = (DiskQuotaActiveTableEntry *)hash_seq_search(&(cache->pos))) != NULL)
	{
		Datum     result;
		Datum     values[3];
		bool      nulls[3];
		HeapTuple tuple;

		memset(values, 0, sizeof(values));
		memset(nulls, false, sizeof(nulls));

		values[0] = ObjectIdGetDatum(results_entry->reloid);
		values[1] = Int64GetDatum(results_entry->tablesize);
		values[2] = Int16GetDatum(results_entry->segid);

		tuple = heap_form_tuple(funcctx->attinmeta->tupdesc, values, nulls);

		result = HeapTupleGetDatum(tuple);

		SRF_RETURN_NEXT(funcctx, result);
	}

	/* finished, do the clear staff */
	hash_destroy(cache->result);
	pfree(cache);
	SRF_RETURN_DONE(funcctx);
}

static Oid
get_dbid(ArrayType *array)
{
	Assert(ARR_ELEMTYPE(array) == OIDOID);
	char *ptr;
	bool  typbyval;
	int16 typlen;
	char  typalign;
	Oid   dbid;

	get_typlenbyvalalign(ARR_ELEMTYPE(array), &typlen, &typbyval, &typalign);
	ptr  = ARR_DATA_PTR(array);
	dbid = DatumGetObjectId(fetch_att(ptr, typbyval, typlen));
	return dbid;
}

/*
 * Call pg_table_size to calcualte the
 * active table size on each segments.
 */
static HTAB *
get_active_tables_stats(ArrayType *array)
{
	int                        ndim = ARR_NDIM(array);
	int                       *dims = ARR_DIMS(array);
	int                        nitems;
	int16                      typlen;
	bool                       typbyval;
	char                       typalign;
	char                      *ptr;
	bits8                     *bitmap;
	int                        bitmask;
	int                        i;
	Oid                        relOid;
	int                        segId;
	HTAB                      *local_table = NULL;
	HASHCTL                    ctl;
	TableEntryKey              key;
	DiskQuotaActiveTableEntry *entry;
	bool                       found;

	Assert(ARR_ELEMTYPE(array) == OIDOID);

	nitems = ArrayGetNItems(ndim, dims);

	get_typlenbyvalalign(ARR_ELEMTYPE(array), &typlen, &typbyval, &typalign);

	ptr     = ARR_DATA_PTR(array);
	bitmap  = ARR_NULLBITMAP(array);
	bitmask = 1;

	memset(&ctl, 0, sizeof(ctl));
	ctl.keysize   = sizeof(TableEntryKey);
	ctl.entrysize = sizeof(DiskQuotaActiveTableEntry);
	ctl.hcxt      = CurrentMemoryContext;
	local_table   = diskquota_hash_create("local table map", 1024, &ctl, HASH_ELEM | HASH_CONTEXT, DISKQUOTA_TAG_HASH);

	for (i = 0; i < nitems; i++)
	{
		/*
		 * handle array containing NULL case for general inupt, but the active
		 * table oid array would not contain NULL in fact
		 */
		if (bitmap && (*bitmap & bitmask) == 0)
		{
			continue;
		}
		else
		{
			relOid     = DatumGetObjectId(fetch_att(ptr, typbyval, typlen));
			segId      = GpIdentity.segindex;
			key.reloid = relOid;
			key.segid  = segId;

			entry = (DiskQuotaActiveTableEntry *)hash_search(local_table, &key, HASH_ENTER, &found);
			if (!found)
			{
				entry->reloid    = relOid;
				entry->segid     = segId;
				entry->tablesize = calculate_table_size(relOid);
			}

			ptr = att_addlength_pointer(ptr, typlen, ptr);
			ptr = (char *)att_align_nominal(ptr, typalign);
		}

		/* advance bitmap pointer if any */
		if (bitmap)
		{
			bitmask <<= 1;
			if (bitmask == 0x100)
			{
				bitmap++;
				bitmask = 1;
			}
		}
	}

	return local_table;
}

/*
 * SetLocktagRelationOid
 *		Set up a locktag for a relation, given only relation OID
 */
static inline void
SetLocktagRelationOid(LOCKTAG *tag, Oid relid)
{
	Oid dbid;

	if (IsSharedRelation(relid))
		dbid = InvalidOid;
	else
		dbid = MyDatabaseId;

	SET_LOCKTAG_RELATION(*tag, dbid, relid);
}

static bool
is_relation_being_altered(Oid relid)
{
	LOCKTAG locktag;
	SetLocktagRelationOid(&locktag, relid);
#if GP_VERSION_NUM < 70000
	VirtualTransactionId *vxid_list = GetLockConflicts(&locktag, AccessShareLock);
#else
	VirtualTransactionId *vxid_list = GetLockConflicts(&locktag, AccessShareLock, NULL);
#endif                                                            /* GP_VERSION_NUM */
	bool being_altered = VirtualTransactionIdIsValid(*vxid_list); /* if vxid_list is empty */
	pfree(vxid_list);
	return being_altered;
}

/*
 * Check whether the cached relfilenode is stale compared to the given one
 * due to delayed cache invalidation messages.
 *
 * NOTE: It will return false if the relation is currently uncommitted.
 */
static bool
is_cached_relfilenode_stale(Oid relOid, RelFileNode rnode)
{
	/*
	 * Since we don't take any lock on relation, need to check for cache
	 * invalidation messages manually.
	 */
	AcceptInvalidationMessages();
	HeapTuple tp = SearchSysCacheCopy1(RELOID, ObjectIdGetDatum(relOid));

	/*
	 * Tuple is not valid if
	 * - The relation has not been committed yet, or
	 * - The relation has been deleted
	 */
	if (!HeapTupleIsValid(tp)) return false;
	Form_pg_class reltup = (Form_pg_class)GETSTRUCT(tp);

	/*
	 * If cache invalidation messages are not delievered in time, the
	 * relfilenode in the tuple of the relation is stale. In that case,
	 * the relfilenode in the relation tuple is not equal to the one in
	 * the active table map.
	 */
	Oid  cached_relfilenode = reltup->relfilenode;
	bool is_stale           = cached_relfilenode != rnode.relNode;
	heap_freetuple(tp);
	return is_stale;
}

/*
 * Get local active table with table oid and table size info.
 * This function first copies active table map from shared memory
 * to local active table map with refilenode info. Then traverses
 * the local map and find corresponding table oid and table file
 * size. Finally stores them into local active table map and return.
 */
static HTAB *
get_active_tables_oid(void)
{
	HASHCTL                        ctl;
	HTAB                          *local_active_table_file_map  = NULL;
	HTAB                          *local_active_table_stats_map = NULL;
	HTAB                          *local_altered_reloid_cache   = NULL;
	HASH_SEQ_STATUS                iter;
	DiskQuotaActiveTableFileEntry *active_table_file_entry;
	DiskQuotaActiveTableEntry     *active_table_entry;
	Oid                           *altered_reloid_entry;

	Oid relOid;

	refresh_monitored_dbid_cache();

	memset(&ctl, 0, sizeof(ctl));
	ctl.keysize                 = sizeof(DiskQuotaActiveTableFileEntry);
	ctl.entrysize               = sizeof(DiskQuotaActiveTableFileEntry);
	ctl.hcxt                    = CurrentMemoryContext;
	local_active_table_file_map = diskquota_hash_create("local active table map with relfilenode info", 1024, &ctl,
	                                                    HASH_ELEM | HASH_CONTEXT, DISKQUOTA_TAG_HASH);

	memset(&ctl, 0, sizeof(ctl));
	ctl.keysize                = sizeof(Oid);
	ctl.entrysize              = sizeof(Oid);
	ctl.hcxt                   = CurrentMemoryContext;
	local_altered_reloid_cache = diskquota_hash_create("local_altered_reloid_cache", 1024, &ctl,
	                                                   HASH_ELEM | HASH_CONTEXT, DISKQUOTA_OID_HASH);

	/* Move active table from shared memory to local active table map */
	LWLockAcquire(diskquota_locks.active_table_lock, LW_EXCLUSIVE);

	hash_seq_init(&iter, active_tables_map);

	/* copy active table from shared memory into local memory */
	while ((active_table_file_entry = (DiskQuotaActiveTableFileEntry *)hash_seq_search(&iter)) != NULL)
	{
		bool                           found;
		DiskQuotaActiveTableFileEntry *entry;

		if (active_table_file_entry->dbid != MyDatabaseId)
		{
			continue;
		}

		/* Add the active table entry into local hash table */
		entry = hash_search(local_active_table_file_map, active_table_file_entry, HASH_ENTER, &found);
		if (entry) *entry = *active_table_file_entry;
		hash_search(active_tables_map, active_table_file_entry, HASH_REMOVE, NULL);
	}
	// TODO: hash_seq_term(&iter);
	LWLockRelease(diskquota_locks.active_table_lock);

	memset(&ctl, 0, sizeof(ctl));
	/* only use Oid as key here, segid is not needed */
	ctl.keysize                  = sizeof(Oid);
	ctl.entrysize                = sizeof(DiskQuotaActiveTableEntry);
	ctl.hcxt                     = CurrentMemoryContext;
	local_active_table_stats_map = diskquota_hash_create("local active table map with relfilenode info", 1024, &ctl,
	                                                     HASH_ELEM | HASH_CONTEXT, DISKQUOTA_OID_HASH);

	remove_committed_relation_from_cache();

	/*
	 * scan whole local map, get the oid of each table and calculate the size
	 * of them
	 */
	hash_seq_init(&iter, local_active_table_file_map);

	while ((active_table_file_entry = (DiskQuotaActiveTableFileEntry *)hash_seq_search(&iter)) != NULL)
	{
		bool        found;
		RelFileNode rnode;
		Oid         prelid;

		/* The session of db1 should not see the table inside db2. */
		if (active_table_file_entry->dbid != MyDatabaseId) continue;

		rnode.dbNode  = active_table_file_entry->dbid;
		rnode.relNode = active_table_file_entry->relfilenode;
		rnode.spcNode = active_table_file_entry->tablespaceoid;
		relOid        = get_relid_by_relfilenode(rnode);

		/* If relfilenode is not prepared for some relation, just skip it. */
		if (!OidIsValid(relOid)) continue;

		/* skip system catalog tables */
		if (relOid < FirstNormalObjectId)
		{
			hash_search(local_active_table_file_map, active_table_file_entry, HASH_REMOVE, NULL);
		}
		else
		{
			prelid             = get_primary_table_oid(relOid, true);
			active_table_entry = hash_search(local_active_table_stats_map, &prelid, HASH_ENTER, &found);
			if (active_table_entry && !found)
			{
				active_table_entry->reloid = prelid;
				/* we don't care segid and tablesize here */
				active_table_entry->tablesize = 0;
				active_table_entry->segid     = -1;
			}
			/*
			 * Do NOT remove relation from the active table map if it is being
			 * altered or its cached relfilenode is stale so that we can check it
			 * again in the next epoch.
			 */
			if (!is_relation_being_altered(relOid) && !is_cached_relfilenode_stale(relOid, rnode))
			{
				hash_search(local_active_table_file_map, active_table_file_entry, HASH_REMOVE, NULL);
			}
		}
	}

	// TODO: hash_seq_term(&iter);

	/* Adding the remaining relfilenodes back to the map in the shared memory */
	LWLockAcquire(diskquota_locks.active_table_lock, LW_EXCLUSIVE);
	hash_seq_init(&iter, local_active_table_file_map);
	while ((active_table_file_entry = (DiskQuotaActiveTableFileEntry *)hash_seq_search(&iter)) != NULL)
	{
		HASHACTION action = check_hash_fullness(active_tables_map, diskquota_max_active_tables,
		                                        active_tables_map_warning, &active_tables_map_last_overflow_report);
		hash_search(active_tables_map, active_table_file_entry, action, NULL);
	}
	/* TODO: hash_seq_term(&iter); */
	LWLockRelease(diskquota_locks.active_table_lock);

	LWLockAcquire(diskquota_locks.altered_reloid_cache_lock, LW_SHARED);
	hash_seq_init(&iter, altered_reloid_cache);
	while ((altered_reloid_entry = (Oid *)hash_seq_search(&iter)) != NULL)
	{
		bool found;
		Oid  altered_oid = *altered_reloid_entry;
		if (OidIsValid(*altered_reloid_entry))
		{
			active_table_entry = hash_search(local_active_table_stats_map, &altered_oid, HASH_ENTER, &found);
			if (!found && active_table_entry)
			{
				active_table_entry->reloid = altered_oid;
				/* We don't care segid and tablesize here. */
				active_table_entry->tablesize = 0;
				active_table_entry->segid     = -1;
			}
		}
		hash_search(local_altered_reloid_cache, &altered_oid, HASH_ENTER, NULL);
	}
	LWLockRelease(diskquota_locks.altered_reloid_cache_lock);

	hash_seq_init(&iter, local_altered_reloid_cache);
	while ((altered_reloid_entry = (Oid *)hash_seq_search(&iter)) != NULL)
	{
		if (OidIsValid(*altered_reloid_entry) && !is_relation_being_altered(*altered_reloid_entry))
		{
			hash_search(local_altered_reloid_cache, altered_reloid_entry, HASH_REMOVE, NULL);
		}
	}

	LWLockAcquire(diskquota_locks.altered_reloid_cache_lock, LW_EXCLUSIVE);
	hash_seq_init(&iter, altered_reloid_cache);
	while ((altered_reloid_entry = (Oid *)hash_seq_search(&iter)) != NULL)
	{
		bool found;
		Oid  altered_reloid = *altered_reloid_entry;
		hash_search(local_altered_reloid_cache, &altered_reloid, HASH_FIND, &found);
		if (!found)
		{
			hash_search(altered_reloid_cache, &altered_reloid, HASH_REMOVE, NULL);
		}
	}
	LWLockRelease(diskquota_locks.altered_reloid_cache_lock);

	/*
	 * If cannot convert relfilenode to relOid, put them back to shared memory
	 * and wait for the next check.
	 */
	if (hash_get_num_entries(local_active_table_file_map) > 0)
	{
		bool                           found;
		DiskQuotaActiveTableFileEntry *entry;

		hash_seq_init(&iter, local_active_table_file_map);
		LWLockAcquire(diskquota_locks.active_table_lock, LW_EXCLUSIVE);
		while ((active_table_file_entry = (DiskQuotaActiveTableFileEntry *)hash_seq_search(&iter)) != NULL)
		{
			HASHACTION action = check_hash_fullness(active_tables_map, diskquota_max_active_tables,
			                                        active_tables_map_warning, &active_tables_map_last_overflow_report);
			entry             = hash_search(active_tables_map, active_table_file_entry, action, &found);
			if (entry) *entry = *active_table_file_entry;
		}
		LWLockRelease(diskquota_locks.active_table_lock);
	}
	hash_destroy(local_active_table_file_map);
	hash_destroy(local_altered_reloid_cache);
	return local_active_table_stats_map;
}

/* -------------------------------------------------------------------------
 *
 * gp_activetable.h
 *
 * Copyright (c) 2018-2020 Pivotal Software, Inc.
 * Copyright (c) 2020-Present VMware, Inc. or its affiliates
 *
 * IDENTIFICATION
 *		diskquota/gp_activetable.h
 *
 * -------------------------------------------------------------------------
 */
#ifndef ACTIVE_TABLE_H
#define ACTIVE_TABLE_H

#include "c.h"
#include "utils/hsearch.h"

/* Cache to detect the active table list */
typedef struct DiskQuotaActiveTableFileEntry
{
	Oid dbid;
	Oid relfilenode;
	Oid tablespaceoid;
} DiskQuotaActiveTableFileEntry;

typedef struct TableEntryKey
{
	Oid reloid;
	int segid;
} TableEntryKey;

typedef struct DiskQuotaActiveTableEntry
{
	Oid  reloid;
	int  segid;
	Size tablesize;
} DiskQuotaActiveTableEntry;

typedef struct ActiveTableEntryCombined
{
	Oid reloid;
	/*
	 Variable length array: index 0 is used for the coordinator,
	 the remaining SEGCOUNT indexes are for segments.
	 */
	Size tablesize[1];
} ActiveTableEntryCombined;

extern HTAB *gp_fetch_active_tables(bool force);
extern void  init_active_table_hook(void);
extern void  init_shm_worker_active_tables(void);
extern void  init_lock_active_tables(void);

extern HTAB *monitored_dbid_cache;

#ifndef atooid
#define atooid(x) ((Oid)strtoul((x), NULL, 10))
#endif

#endif

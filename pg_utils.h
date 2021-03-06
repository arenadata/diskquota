/* -------------------------------------------------------------------------
 *
 * pg_utils.h
 *
 * This code is utils for detecting active table for databases
 *
 * Copyright (C) 2013, PostgreSQL Global Development Group
 *
 *
 * -------------------------------------------------------------------------
 */
#ifndef DISKQUOTA_PG_UTILS_H
#define DISKQUOTA_PG_UTILS_H

#include "storage/relfilenode.h"

extern int64 diskquota_get_table_size_by_oid(Oid oid);
extern int64 diskquota_get_table_size_by_relfilenode(RelFileNode *rfh);

#endif //DISKQUOTA_PG_UTILS_H

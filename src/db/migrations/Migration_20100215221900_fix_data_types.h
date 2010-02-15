#ifndef MIGRATION_20100215221900_FIX_DATA_TYPES_H_
#define MIGRATION_20100215221900_FIX_DATA_TYPES_H_

#include "src/db/migration/Migration.h"

/**
 * Fixes the data types of legacy databases.
 *
 * This change is not undone by the down migration; see 20100214140000.
 */
class Migration_20100215221900_fix_data_types: public Migration
{
	public:
		Migration_20100215221900_fix_data_types (Database &database);
		virtual ~Migration_20100215221900_fix_data_types ();

		virtual void up ();
		virtual void down ();
};

#endif


#include "Migration_20100215211913_drop_old_columns.h"

Migration_20100215211913_drop_old_columns::Migration_20100215211913_drop_old_columns (Interface &interface):
	Migration (interface)
{
}

Migration_20100215211913_drop_old_columns::~Migration_20100215211913_drop_old_columns ()
{
}

void Migration_20100215211913_drop_old_columns::up (OperationMonitorInterface monitor)
{
	dropColumnAndTemp ("flug"       , "editierbar");
	dropColumnAndTemp ("flug"       , "verein"    );
	dropColumnAndTemp ("person"     , "bwlv"      );
}

void Migration_20100215211913_drop_old_columns::down (OperationMonitorInterface monitor)
{
	// Don't change back
}

void Migration_20100215211913_drop_old_columns::dropColumnAndTemp (const QString &table, const QString &name)
{
	dropColumn (table        , name, true);
	dropColumn (table+"_temp", name, true);
}

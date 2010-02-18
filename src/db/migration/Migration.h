#ifndef MIGRATION_H_
#define MIGRATION_H_

class Database;

class QString;
class QVariant;

// FIXME add methods here to avoid recompiling all migrations on every Database change

/**
 * A new migration is created by creating the appropriate class in
 * src/db/migrations/. The migration is automatically included into the build
 * and recognized by MigrationFactory.
 *
 * A new migration class can be created automatically by using the script
 * script/generate_migration.rb (see documentation).
 *
 * Migrations are supposed to be called only through Migrator, which keeps track
 * of which migrations have already been applied (see documentation).
 */
class Migration
{
	public:
		// *** Types
		enum Direction { dirUp, dirDown };

		Migration (Database &database);
		virtual ~Migration ();

		virtual void up ()=0;
		virtual void down ()=0;

	protected:
		Database &database;
};

#endif /* MIGRATION_H_ */

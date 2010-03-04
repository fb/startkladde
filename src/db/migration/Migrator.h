#ifndef MIGRATOR_H_
#define MIGRATOR_H_

#include <QString>

#include "src/db/migration/Migration.h" // Required for Migration::Direction
#include "src/concurrent/monitor/OperationMonitor.h" // Required for OperationMonitor::Interface

namespace Db { namespace Interface { class Interface; } }
class MigrationFactory;


/**
 * A controller for managing migrations on a database.
 *
 * Migrations should not be run directly, but only through a Migrator, which
 * will keep track of the applied migrations.
 *
 * The version numbers of all applied migrations are stored in a table
 * (#migrationsTableName), similar to Rails.
 */
class Migrator
{
	public:
		// *** Constants
		static const QString migrationsTableName, migrationsColumnName;

		// *** Construction
		Migrator (Db::Interface::Interface &interface);
		virtual ~Migrator ();

		// *** Migration
		void up ();
		void down ();
		void migrate (OperationMonitorInterface monitor=OperationMonitorInterface::null);

		// *** Schema
		void loadSchema ();
		void reset ();
		void clear ();
		void drop ();
		void create ();

		// *** Migration listing
		QList<quint64> pendingMigrations ();
		quint64 nextMigration ();
		static quint64 latestVersion ();
		bool isCurrent () { return nextMigration ()==0; }

		// *** Migrations table
		quint64 currentVersion ();
		bool hasMigration (quint64 version);
		QList<quint64> appliedMigrations ();


	protected:
		// *** Migration
		void runMigration (quint64 version, Migration::Direction direction, OperationMonitorInterface monitor=OperationMonitorInterface::null);

		// *** Migrations table
		void addMigration (quint64 version);
		void removeMigration (quint64 version);
		void createMigrationsTable ();
		void assumeMigrated (QList<quint64> versions);
		void clearMigrations ();

	private:
		Db::Interface::Interface &interface;
		MigrationFactory *factory;
};

#endif

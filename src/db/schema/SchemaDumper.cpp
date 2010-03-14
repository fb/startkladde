#include "SchemaDumper.h"

#include <iostream>

#include <QFile>
#include <QDateTime>
#include <QStringList>

#include "src/db/interface/Interface.h"
#include "src/db/migration/Migrator.h" // Required for migrationsTableName/migrationsColumnName
#include "src/db/result/Result.h"

SchemaDumper::SchemaDumper (Interface &interface):
	interface (interface)
{
}

SchemaDumper::~SchemaDumper ()
{
}

QString SchemaDumper::dumpSchema ()
{
	QStringList output;

	output << "---";

	dumpTables (output);
	dumpVersions (output);

	return output.join ("\n");
}

void SchemaDumper::dumpSchemaToFile (const QString &filename)
{
	QString dump=dumpSchema ();

	QFile file (filename);
	try
	{
		file.open (QFile::WriteOnly);

		QString generatedDate=QDateTime::currentDateTime ().toUTC ().toString (Qt::ISODate);
		file.write ("# This file has been autogenerated by SchemaDumper on " + generatedDate +" UTC.\n");
		file.write ("# It should not be modified as any changes will be overwritten.\n");
		file.write ("#\n");
		file.write ("# This file should be checked into version control. See the developer\n");
		file.write ("# documentation (doc/internal/database.txt) for further information.\n");

		file.write (dump.toUtf8 ());
		file.write ("\n");
		file.close ();
	}
	catch (...)
	{
		file.close ();
		throw;
	}
}

void SchemaDumper::dumpTables (QStringList &output)
{
	output << "tables:";

	QSharedPointer<Result> result=interface.executeQueryResult ("SHOW TABLES");

	while (result->next ())
		dumpTable (output, result->value (0).toString ());
}

void SchemaDumper::dumpTable (QStringList &output, const QString &name)
{
	// Don't dump the migrations table
	if (name==Migrator::migrationsTableName) return;

	output << QString ("- name: \"%1\"").arg (name);
	dumpColumns (output, name);
}
void SchemaDumper::dumpColumns (QStringList &output, const QString &table)
{
	output << "  columns:";

	Query query=Query ("SHOW COLUMNS FROM %1").arg (table);
	QSharedPointer<Result> result=interface.executeQueryResult (query);

	QSqlRecord record=result->record ();
	int nameIndex=record.indexOf ("Field");
	int typeIndex=record.indexOf ("Type");
	int nullIndex=record.indexOf ("Null");

	while (result->next ())
	{
		QString name=result->value (nameIndex).toString ();
		QString type=result->value (typeIndex).toString ();
		QString null=result->value (nullIndex).toString ();

		// The id columns created automatically, don't dump it
		if (name!="id")
			dumpColumn (output, name, type, null);
	}
}

void SchemaDumper::dumpColumn (QStringList &output, const QString &name, const QString &type, const QString &null)
{
	output << QString ("  - name: \"%1\"").arg (name);
	output << QString ("    type: \"%1\"").arg (type);
	output << QString ("    nullok: \"%1\"").arg (null);
}

void SchemaDumper::dumpVersions (QStringList &output)
{
	// TODO handle empty/nonexistant migrations table
	output << "versions:";

	QString table=Migrator::migrationsTableName;
	QString column=Migrator::migrationsColumnName;

	Query query=Query::selectDistinctColumns (table, column);
	QSharedPointer<Result> result=interface.executeQueryResult (query);

	while (result->next ())
		output << QString ("- %1").arg (result->value (0).toString ());
}

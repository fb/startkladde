#include "ColumnSpec.h"

#include <QList>
#include <QStringList>

ColumnSpec::ColumnSpec (const QString &name, const QString &type, const QString &extra):
	name (name), type (type), extra (extra)
{
}

ColumnSpec::~ColumnSpec ()
{
}

QString ColumnSpec::createClause ()
{
	if (extra.isEmpty ())
		return QString ("%1 %2").arg (name, type);
	else
		return QString ("%1 %2 %3").arg (name, type, extra);
}

QString ColumnSpec::createClause (const QList<ColumnSpec> &list)
{
	QStringList createClauses;

	foreach (ColumnSpec columnSpec, list)
		createClauses.append (columnSpec.createClause ());

	return createClauses.join (", ");
}
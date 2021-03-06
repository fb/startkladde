#ifndef DATABASEINFO_H_
#define DATABASEINFO_H_

#include <QString>

class QSettings;

class DatabaseInfo
{
	public:
		DatabaseInfo ();
		DatabaseInfo (QSettings &settings);
		virtual ~DatabaseInfo ();

		virtual QString toString () const;
		virtual operator QString () const;
		virtual QString serverText () const;

		virtual void load (QSettings &settings);
		virtual void save (QSettings &settings);

		virtual int effectivePort () const { return defaultPort ? 3306 : port; }

		virtual bool different (const DatabaseInfo &other);

		// Update: toString, serverText, load, save, different
		QString server;
		bool defaultPort;
		int port;
		QString username;
		QString password;
		QString database;
};

#endif

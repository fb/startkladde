#include <iostream>

#include <QStringList>

#include "src/version.h"
#include "src/config/Options.h"
#include "src/db/adminFunctions.h"
#include "src/db/Database.h"
#include "src/io/io.h"

void display_help ()
{
	std::cout << "usage: skadmin [options...] action [actionopts...]" << std::endl;
	std::cout << "  actions:" << std::endl;
	std::cout << "    - check_db: check database usability." << std::endl;
	std::cout << "    - init_db: initialize the database." << std::endl;
	std::cout << "    - noop: no operation (apart from reading the configuration)." << std::endl;
	std::cout << "    - merge_person correct_id wrong_id...: merge persons" << std::endl;
	std::cout << "  options:" << std::endl;
	Options::display_options ("    ");
}

void init_db (Database &root_db)
	// TODO return value
{
	try
	{
		initialize_database (root_db);
	}
	catch (Database::ex_init_failed &e)
	{
		std::cout << e.description (true) << std::endl;
	}
	catch (SkException &e)
	{
		std::cout << e.description (true) << std::endl;
	}
}

int check_db (Database &db)
	// Returns: 3 for db not usable, 2 for connection/program/server errors
	// That means for doing a db_init: 3: might help, 2: no use
{
	try
	{
		db.connect ();
		db.check_usability ();
		db.disconnect ();
		std::cout << "Database seems OK" << std::endl;
	}
	catch (Database::ex_access_denied &e) { std::cout << e.description () << std::endl; return 3; }
	catch (Database::ex_insufficient_access &e) { std::cout << e.description () << std::endl; return 3; }
	catch (Database::ex_unusable &e) { std::cout << "Database unusable: " << e.description () << std::endl; return 3; }
	catch (SkException &e) { std::cout << e.description () << std::endl; return 2; }
	catch (...) { std::cout << "Uncaught exception!" << std::endl; return 2; }

	return 0;
}

int merge_person (Database &db, const QStringList &args)
{
	if (args.size ()<2)
	{
		std::cout << "Error: too few option arguments." << std::endl
			<< "Usage: merge_person correct_id wrong_id [wrong_id...]" << std::endl;

		return 1;
	}
	else
	{
		// Determine the correct ID
		db_id correct_id=args[0].toLongLong ();
		if (id_invalid (correct_id))
		{
			std::cout << "Error: " << args[0] << " is not a valid ID" << std::endl;
			return 2;
		}

		// Determine the wrong IDs
		QList<db_id> wrong_ids;
		for (int i=1; i<args.size (); ++i)
		{
			db_id wrong_id=args[i].toLongLong();
			if (id_invalid (wrong_id))
			{
				std::cout << "Error: " << args[i] << " is not a valid ID" << std::endl;
				return 2;
			}
			wrong_ids.push_back (wrong_id);
		}

		// Display what we're about to do
		std::cout << "Merging wrong person";
		if (wrong_ids.size ()>1) std::cout << "s";
		for (QList<db_id>::iterator i=wrong_ids.begin (); i!=wrong_ids.end (); ++i)
			std::cout << " " << *i;
		std::cout << " into " << correct_id << "." << std::endl;

		try
		{
			db.connect ();
			db.use_db ();

			for (QList<db_id>::iterator i=wrong_ids.begin (); i!=wrong_ids.end (); ++i)
			{
				db_id wrong_id=*i;
				std::cout << "Merging " << wrong_id << " into " << correct_id << "." << std::endl;
				db.merge_person (correct_id, wrong_id);
			}

			std::cout << "Success" << std::endl;
		}
		catch (Database::ex_operation_failed &e)
		{
			std::cout << "Error: " << e.description (true) << std::endl;
			return 2;
		}
		catch (SkException &e)
		{
			std::cout << "Error: " << e.description () << std::endl;
			return 2;
		}
	}

	return 0;
}

int main (int argc, char *argv[])
{

	if (argc<=1)
	{
		std::cout << "Error: Missing an action." << std::endl;
		display_help ();
		return 1;
	}
	else
	{
		if (!opts.parse_arguments (argc, argv))
			return 1;
		else if (opts.need_display ())
			opts.do_display ();
		else if (opts.display_help)
			display_help ();
		else if (opts.non_options.empty ())
		{
			std::cout << "Error: Missing an action." << std::endl;
			display_help ();
			return 1;
		}
		else
		{
			enum admin_action { aa_none, aa_init_db, aa_noop, aa_check_db, aa_merge_person };

			QString action=opts.non_options.front ();
			admin_action act=aa_none;

			if (action=="init_db") act=aa_init_db;
			else if (action=="check_db") act=aa_check_db;
			else if (action=="noop") act=aa_noop;
			else if (action=="merge_person") act=aa_merge_person;

			if (act!=aa_none) opts.read_config_files (NULL, NULL, argc, argv);

			// Determine whether we need an root connection (need_root_db)
			bool need_root_db=false;
			switch (act)
			{
				case aa_init_db:
				case aa_merge_person:
					need_root_db=true;
					break;
				case aa_check_db:
				case aa_noop:
				case aa_none:
					need_root_db=false;
					break;
			}

			// Get the root password (root_password)
			QString root_password;
			if (need_root_db)
			{
				if (opts.root_password.isEmpty ())
				{
					std::cout << "Enter the password for " << opts.root_name << "@" << opts.server_display_name << ":" << opts.port << ": ";
					root_password=read_password ();
				}
				else
				{
					root_password=opts.root_password;
				}
			}

			// Set database connection
			Database db;
			db.display_queries=opts.display_queries;
			db.set_database (opts.database);
			if (need_root_db)
				db.set_connection_data (opts.server, opts.port, opts.root_name, root_password);
			else
				db.set_connection_data (opts.server, opts.port, opts.username, opts.password);

			QStringList::iterator nonopt=opts.non_options.begin (); nonopt++;
			switch (act)
			{
				case aa_init_db:
					init_db (db);
					break;
				case aa_check_db:
					return check_db (db);
					break;
				case aa_merge_person:

					return merge_person (db, opts.non_options.mid (1));
					break;
				case aa_noop:
					std::cout << "noop" << std::endl;
					break;
				case aa_none:
					std::cout << "Error: Unknown option " << action << "." << std::endl;
					display_help ();
					break;
				// No default to allow compiler warnings
			}

			db.disconnect ();
		}
	}

	return 0;
}


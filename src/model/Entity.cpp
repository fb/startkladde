#include "Entity.h"

Entity::Entity ()
	/*
	 * Constructs an empty Entity instance.
	 */
{
	id=0;
	editierbar=true;
}

Entity::~Entity ()
	/*
	 * Cleans up a Entity instance.
	 */
{

}

QString entityLabel (EntityType t, casus c)
	/*
	 * Returns a word describing the type of Entity.
	 * TODO: this function should be overridden in the base class instead of
	 * using EntityType t.
	 * Parameters:
	 *   - t: the Entity type.
	 *   - c: the (grammatical) case of the word.
	 * Return value:
	 *   the word.
	 */
{
	switch (t)
	{
		case st_plane:
		{
			switch (c)
			{
				case cas_nominativ: return "Flugzeug"; break;
				case cas_genitiv: return "Flugzeugs"; break;
				case cas_dativ: return "Flugzeug"; break;
				case cas_akkusativ: return "Flugzeug"; break;
				default: return "[Flugzeug]"; break;
			}
		} break;
		case st_person:
		{
			switch (c)
			{
				case cas_nominativ: return "Person"; break;
				case cas_genitiv: return "Person"; break;
				case cas_dativ: return "Person"; break;
				case cas_akkusativ: return "Person"; break;
				default: return "[Person]"; break;
			}
		} break;
		case st_startart:
		{
			switch (c)
			{
				case cas_nominativ: return "Startart"; break;
				case cas_genitiv: return "Startart"; break;
				case cas_dativ: return "Startart"; break;
				case cas_akkusativ: return "Startart"; break;
				default: return "[Startart]"; break;
			}
		} break;
		default:
		{
			switch (c)
			{
				case cas_nominativ: return "Zeug"; break;
				case cas_genitiv: return "Zeugs"; break;
				case cas_dativ: return "Zeug"; break;
				case cas_akkusativ: return "Zeug"; break;
				default: return "[Zeug]"; break;
			}
		} break;
	}
}



QString Entity::get_selector_value (int column_number) const
{
	switch (column_number)
	{
		case 0: return QString::number (id);
		case 1: return bemerkungen;
		default: return QString ();
	}
}

QString Entity::get_selector_caption (int column_number)
{
	switch (column_number)
	{
		case 0: return "ID";
		case 1: return "Bemerkungen";
		default: return QString ();
	}
}


void Entity::output (std::ostream &stream, output_format_t format, bool last, QString name, QString value)
{
	switch (format)
	{
		case of_single:
		{
			stream << name << "\t" << value << std::endl;
		} break;
		case of_table_data:
		{
			QString v=value;
			replace_tabs (v);
			stream << v;
			if (!last) stream << "\t";
			if (last) stream << std::endl;
		} break;
		case of_table_header:
		{
			QString n=name;
			replace_tabs (n);
			stream << n;
			if (!last) stream << "\t";
			if (last) stream << std::endl;
		} break;
	}
}

void Entity::output (std::ostream &stream, output_format_t format, bool last, QString name, db_id value)
{
	output (stream, format, last, name, QString::number (value));
}


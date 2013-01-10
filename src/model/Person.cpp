#include "Person.h"

#include <cassert>

#include <QApplication>

#include "src/text.h"
#include "src/db/Query.h"
#include "src/db/result/Result.h"
#include "src/util/bool.h"
#include "src/util/qDate.h"
#include "src/util/qString.h"
#include "src/i18n/notr.h"


// ******************
// ** Construction **
// ******************

Person::Person ():
	Entity ()
{
	initialize ();
}

Person::Person (dbId id):
	Entity (id)
{
	initialize ();
}

void Person::initialize ()
{
	checkMedical=false;
}


// ****************
// ** Comparison **
// ****************

bool Person::operator< (const Person &o) const
{
	if (lastName<o.lastName) return true;
	if (lastName>o.lastName) return false;
	if (firstName<o.firstName) return true;
	if (firstName>o.firstName) return false;
	return false;
}


// ****************
// ** Formatting **
// ****************

QString Person::toString () const
{
	return qnotr ("id=%1, lastName=%2, firstName=%3, club=%4, clubId=%5, medicalValidity=%6, checkMedical=%7")
		.arg (id)
		.arg (lastName)
		.arg (firstName)
		.arg (club)
		.arg (clubId)
		.arg (medicalValidity.toString ())
		.arg (checkMedical)
		;
}

QString Person::fullName () const
{
	QString l=lastName ; if (l.isEmpty ()) l=notr ("?");
	QString f=firstName; if (f.isEmpty ()) f=notr ("?");

	// TODO use .arg
	return f+notr (" ")+l;
}

QString Person::formalName () const
{
	QString l=lastName ; if (l.isEmpty ()) l=notr ("?");
	QString f=firstName; if (f.isEmpty ()) f=notr ("?");

	// TODO use .arg
	return l+notr (", ")+f;
}

QString Person::formalNameWithClub () const
{
	if (isNone (club)) return formalName ();
	// TODO use .arg
	return formalName ()+notr (" (")+club+notr (")");
}

QString Person::getDisplayName () const
{
	return fullName ();
}


// *****************
// ** ObjectModel **
// *****************

Person::DefaultObjectModel::DefaultObjectModel ():
	displayMedicalData (true)
{
}

int Person::DefaultObjectModel::columnCount () const
{
	return 8;
}

QVariant Person::DefaultObjectModel::displayHeaderData (int column) const
{
	switch (column)
	{
		case 0: return qApp->translate ("Person::DefaultObjectModel", "Last name");
		case 1: return qApp->translate ("Person::DefaultObjectModel", "First name");
		case 2: return qApp->translate ("Person::DefaultObjectModel", "Club");
		case 3: return qApp->translate ("Person::DefaultObjectModel", "Medical until");
		case 4: return qApp->translate ("Person::DefaultObjectModel", "Check medical");
		case 5: return qApp->translate ("Person::DefaultObjectModel", "Comments");
		case 6: return qApp->translate ("Person::DefaultObjectModel", "Club ID");
		case 7: return qApp->translate ("Person::DefaultObjectModel", "ID");
	}

	assert (false);
	return QVariant ();
}

QVariant Person::DefaultObjectModel::displayData (const Person &object, int column) const
{
	switch (column)
	{
		case 0: return object.lastName;
		case 1: return object.firstName;
		case 2: return object.club;
		case 3: if      (!displayMedicalData)                return qApp->translate ("Person::DefaultObjectModel", "not displayed");
		        else if (!object.medicalValidity.isValid ()) return qApp->translate ("Person::DefaultObjectModel", "unknown");
		        else                                         return object.medicalValidity.toString (defaultNumericDateFormat ());
		case 4: return object.checkMedical?
				qApp->translate ("Person", "Yes"):
				qApp->translate ("Person", "No");
		case 5: return object.comments;
		case 6: return object.clubId;
		case 7: return object.id;
	}

	assert (false);
	return QVariant ();
}

void Person::DefaultObjectModel::setDisplayMedicalData (bool displayMedicalData)
{
	// TODO this should have a way of notifying a containing model about the
	// change (there is another place where this functionality is desired).
	this->displayMedicalData=displayMedicalData;

}

int Person::DefaultObjectModel::medicalColumn () const
{
	return 3;
}

// *******************
// ** SQL interface **
// *******************

QString Person::dbTableName ()
{
	return notr ("people");
}

QString Person::selectColumnList ()
{
	return notr ("id,last_name,first_name,club,club_id,comments,medical_validity,check_medical_validity");
}

Person Person::createFromResult (const Result &result)
{
	Person p (result.value (0).toLongLong ());

	p.lastName            =result.value (1).toString ();
	p.firstName           =result.value (2).toString ();
	p.club                =result.value (3).toString ();
	p.clubId              =result.value (4).toString ();
	p.comments            =result.value (5).toString ();
	p.medicalValidity     =result.value (6).toDate ();
	p.checkMedical        =result.value (7).toBool ();

	return p;
}

QString Person::insertColumnList ()
{
	return notr ("last_name,first_name,club,club_id,comments,medical_validity,check_medical_validity");
}

QString Person::insertPlaceholderList ()
{
	return notr ("?,?,?,?,?,?,?");
}

void Person::bindValues (Query &q) const
{
	q.bind (lastName);
	q.bind (firstName);
	q.bind (club);
	q.bind (clubId);
	q.bind (comments);
	q.bind (medicalValidity);
	q.bind (checkMedical);
}

QList<Person> Person::createListFromResult (Result &result)
{
	QList<Person> list;

	while (result.next ())
		list.append (createFromResult (result));

	return list;
}

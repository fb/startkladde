#include "FlightWindow.h"

#include <cassert>
#include <iostream>

#include <QCompleter>
#include <QShowEvent>
#include <QPushButton>
#include <QDesktopWidget>

#include "src/util/color.h"
#include "src/text.h"
#include "src/gui/dialogs.h"
#include "src/gui/windows/ObjectSelectWindow.h"
#include "src/model/Flight.h"
#include "src/model/Plane.h"
#include "src/model/Person.h"
#include "src/db/cache/Cache.h"
#include "src/gui/windows/objectEditor/ObjectEditorWindow.h"
#include "src/config/Settings.h" // Required for location
#include "src/util/qDate.h"
#include "src/util/qString.h"
#include "src/concurrent/monitor/OperationCanceledException.h"
#include "src/db/DbManager.h"
#include "src/logging/messages.h"

/*
 * On enabling/diabling widgets:
 *   - The labels are hidden using SkLabel::setConcealed rather than
 *     QLabel::setVisible in order not to modify the layout.
 *   - For each widget, one or more (for pilots) labels are associated via a
 *     QMultiMap.
 *   - We use the widget as indication of which widget to show/hide rather than
 *     the label (which would make sense for hiding people's last and first
 *     name simultaneously) because otherwise we would have to map to both
 *     QWidgets and SkLabels (needed explicitly because of setConcealed).
 *   - The labels are smaller than the widgets which means they have to be
 *     artificially made as big as the widgets so the layout doesn't change
 *     when the widgets are hidden and the labels concealed. This is done in
 *     showEvent. We cannot set the layout's row heights because we cannot find
 *     out which row a given widget is in.
 *   - For the time inputs, both the checkBox and the timeEdit are associated
 *     with the label. This is because the label has to be the minimum size of
 *     both widgets. The timeEdit will be shown/hidden on checkBox value
 *     changes using the visible property, so this won't affect the label.
 *   - If there should be a problem with
 *       (a) widgets from multiple lines being associated with the same label
 *           (name fields), or
 *       (b) the time inputs being shown/hidden with the label and checkbox
 *           visible,
 *     the widgetLabelMap can be split into a widgetLabelMap and a
 *     labelWidgetMap.
 */

/*
 * More random notes:
 *   - there used to be a flight buffer which was updated on every value change
 *     in order to perform continuous error checking. However, this is a state
 *     duplication which significantly complicates the code while providing only
 *     a moderate performance improvement. Thus, it was removed. For error
 *     checking, the values are always read from the input widgets.
 */

//using namespace std;

// ***************
// ** Constants **
// ***************

static const QColor errorColor (255, 127, 127);

// ************************
// ** Construction/Setup **
// ************************

/**
 * Creates the flight editor window.
 *
 * setup must be called after cr
 *
 * @param parent the parent widget of the editor window. Passed to the base
 *               class constructor.
 * @param mode the editor mode. This determines among other things, how flight
 *             is treated.
 * @param manager the database manager to use for reading and writing data
 * @param flight the flight to edit or to display in the editing fields
 *               initially, or NULL for none. The flight is copied by the
 *               constructor and not accessed any more later.
 * @param flags the window flags, passed to the base class constructor
 */
FlightWindow::FlightWindow (QWidget *parent, FlightWindow::Mode mode, DbManager &manager, Qt::WindowFlags flags)
	:QDialog (parent, flags),
	manager (manager),
	cache (manager.getCache ()),
	mode (mode),
	labelHeightsSet (false),
	originalFlightId (invalidId),
	selectedPlane (invalidId),
	selectedTowplane (invalidId),
	selectedPilot (invalidId),
	selectedCopilot (invalidId),
	selectedTowpilot (invalidId)
{
	// *** Setup the window
	ui.setupUi (this);

	ui.buttonBox->button (QDialogButtonBox::Cancel)->setText ("Abbre&chen");

	nowButton=new QPushButton ("Jetzt");
	ui.buttonBox->addButton (nowButton, QDialogButtonBox::AcceptRole);
	QObject::connect (nowButton, SIGNAL (clicked ()), this, SLOT (nowButton_clicked ()));

	QPushButton *okButton=ui.buttonBox->button (QDialogButtonBox::Ok);
	QObject::connect (okButton, SIGNAL (clicked ()), this, SLOT (okButton_clicked ()));

	// *** Database
	QObject::connect (&manager, SIGNAL (stateChanged (DbManager::State)), this, SLOT (databaseStateChanged (DbManager::State)));
	QObject::connect (&manager.getCache (), SIGNAL (changed (DbEvent)), this, SLOT (cacheChanged (DbEvent)));

	// *** Settings
	connect (&Settings::instance (), SIGNAL (changed ()), this, SLOT (settingsChanged ()));
	settingsChanged ();

	// *** Setup the data
	fillData ();


	// *** Setup the label map
	/*
	 * Notes:
	 *   - the *pilotLabels are assigned to the corresponding last name widget.
	 *     This is because the are in the same line and the information is also
	 *     used for setting label heights. The last and first name widgets are
	 *     always shown and hidden together anyway.
	 */
	widgetLabelMap.insert (ui.registrationInput, ui.registrationLabel);
	widgetLabelMap.insert (ui.planeTypeLabel   , ui.planeTypeLabel);
	widgetLabelMap.insert (ui.flightTypeInput  , ui.flightTypeLabel);
	//
	widgetLabelMap.insert (ui.pilotLastNameInput   , ui.pilotLastNameLabel);
	widgetLabelMap.insert (ui.pilotFirstNameInput  , ui.pilotFirstNameLabel);
	widgetLabelMap.insert (ui.pilotLastNameInput   , ui.pilotLabel);
	widgetLabelMap.insert (ui.copilotLastNameInput , ui.copilotLastNameLabel);
	widgetLabelMap.insert (ui.copilotFirstNameInput, ui.copilotFirstNameLabel);
	widgetLabelMap.insert (ui.copilotLastNameInput , ui.copilotLabel);
	//
	widgetLabelMap.insert (ui.flightModeInput  , ui.flightModeLabel);
	widgetLabelMap.insert (ui.launchMethodInput, ui.launchMethodLabel);
	//
	widgetLabelMap.insert (ui.towplaneRegistrationInput, ui.towplaneRegistrationLabel);
	widgetLabelMap.insert (ui.towplaneTypeWidget       , ui.towplaneTypeLabel);
	widgetLabelMap.insert (ui.towpilotLastNameInput    , ui.towpilotLabel);
	widgetLabelMap.insert (ui.towpilotLastNameInput    , ui.towpilotLastNameLabel);
	widgetLabelMap.insert (ui.towpilotFirstNameInput   , ui.towpilotFirstNameLabel);
	widgetLabelMap.insert (ui.towflightModeInput       , ui.towflightModeLabel);
	//
	widgetLabelMap.insert (ui.departureTimeCheckbox,        ui.departureTimeLabel);
	widgetLabelMap.insert (ui.departureTimeInput,           ui.departureTimeLabel);
	widgetLabelMap.insert (ui.landingTimeCheckbox,          ui.landingTimeLabel);
	widgetLabelMap.insert (ui.landingTimeInput,             ui.landingTimeLabel);
	widgetLabelMap.insert (ui.towflightLandingTimeCheckbox, ui.towflightLandingTimeLabel);
	widgetLabelMap.insert (ui.towflightLandingTimeInput,    ui.towflightLandingTimeLabel);
	//
	widgetLabelMap.insert (ui.departureLocationInput       , ui.departureLocationLabel);
	widgetLabelMap.insert (ui.landingLocationInput         , ui.landingLocationLabel);
	widgetLabelMap.insert (ui.towflightLandingLocationInput, ui.towflightLandingLocationLabel);
	widgetLabelMap.insert (ui.numLandingsInput             , ui.numLandingsLabel);
	//
	widgetLabelMap.insert (ui.commentInput       , ui.commentLabel);
	widgetLabelMap.insert (ui.accountingNoteInput, ui.accountingNoteLabel);
	widgetLabelMap.insert (ui.dateInput          , ui.dateLabel);
	widgetLabelMap.insert (ui.errorList          , ui.errorLabel);

	// *** Setup the GUI
	switch (mode)
	{
		case modeCreate:
			setWindowTitle ("Flug anlegen");
			break;
		case modeEdit:
			setWindowTitle ("Flug editieren");
			break;
	}

	// Setup the label error colors
	foreach (SkLabel * const &label, widgetLabelMap.values ())
		label->setErrorColor (errorColor);

	// Setup the required field label colors
	const QColor requiredFieldColor=interpol (0.75, palette ().background ().color (), Qt::white);
	ui.registrationLabel         ->setDefaultBackgroundColor (requiredFieldColor);
	ui.pilotLabel                ->setDefaultBackgroundColor (requiredFieldColor);
	ui.copilotLabel              ->setDefaultBackgroundColor (requiredFieldColor);
	ui.towpilotLabel             ->setDefaultBackgroundColor (requiredFieldColor);
	ui.flightTypeLabel           ->setDefaultBackgroundColor (requiredFieldColor);
	ui.flightModeLabel           ->setDefaultBackgroundColor (requiredFieldColor);
	ui.launchMethodLabel         ->setDefaultBackgroundColor (requiredFieldColor);
	ui.towplaneRegistrationLabel ->setDefaultBackgroundColor (requiredFieldColor);

	// Hide (not just conceal, see #settingsChanged) the error fields if the
	// mode is create, so we don't get a scroll bar.
	if (mode==modeCreate)
	{
		ui.errorList->setVisible (false);
		ui.errorLabel->setVisible (false);
	}

	// Setup initial values
	if (mode==modeCreate)
		ui.departureLocationInput->setEditText (Settings::instance ().location);

	updateSetup ();
	updateErrors (false);

	ui.registrationInput->setFocus ();
}

FlightWindow::~FlightWindow ()
{
}

void FlightWindow::databaseStateChanged (DbManager::State state)
{
	if (state==DbManager::stateDisconnected)
		close ();
}

// ***********
// ** Setup **
// ***********

void FlightWindow::settingsChanged ()
{
	// Hide the towpilot fields (so they don't take up space, not just
	// concealed - that is decided in #isTowpilotActive) if recordTowpilot is
	// false. If recordTowpilot is true, but the selected launch method is not
	// an airtow, the fields will still be concealed.
	bool tp=Settings::instance ().recordTowpilot;

	ui.towpilotLastNameInput->setVisible (tp);
	ui.towpilotFirstNameInput->setVisible (tp);

	ui.towpilotLabel->setVisible (tp);
	ui.towpilotLastNameLabel->setVisible (tp);
	ui.towpilotFirstNameLabel->setVisible (tp);
}

void FlightWindow::fillData ()
{
	// *** Plane registrations
	const QStringList registrations=cache.getPlaneRegistrations ();

	ui.registrationInput->addItem (Plane::defaultRegistrationPrefix ());
	ui.registrationInput->addItems (registrations);
	ui.registrationInput->setEditText (Plane::defaultRegistrationPrefix ());
	ui.registrationInput->setDefaultPrefix (Plane::defaultRegistrationPrefix ());

	ui.towplaneRegistrationInput->addItem (Plane::defaultRegistrationPrefix ());
	ui.towplaneRegistrationInput->addItems (registrations);
	ui.towplaneRegistrationInput->setEditText (Plane::defaultRegistrationPrefix ());
	ui.towplaneRegistrationInput->setDefaultPrefix (Plane::defaultRegistrationPrefix ());


	// *** Flight types
	const QList<Flight::Type> flightTypes=Flight::listTypes (false);
	for (int i=0; i<flightTypes.size(); ++i)
		ui.flightTypeInput->addItem (Flight::typeText (flightTypes.at (i), true), flightTypes.at (i));


	// *** Person names
	const QStringList lastNames =cache.getPersonLastNames ();
	const QStringList firstNames=cache.getPersonFirstNames();

	ui.   pilotFirstNameInput ->addItem ("");
	ui. copilotFirstNameInput ->addItem ("");
	ui.towpilotFirstNameInput ->addItem ("");

	ui.   pilotLastNameInput ->addItem ("");
	ui. copilotLastNameInput ->addItem ("");
	ui.towpilotLastNameInput ->addItem ("");

	ui.   pilotFirstNameInput ->addItems (firstNames);
	ui. copilotFirstNameInput ->addItems (firstNames);
	ui.towpilotFirstNameInput ->addItems (firstNames);

	ui.   pilotLastNameInput ->addItems (lastNames);
	ui. copilotLastNameInput ->addItems (lastNames);
	ui.towpilotLastNameInput ->addItems (lastNames);

	ui.   pilotFirstNameInput ->setEditText ("");
	ui. copilotFirstNameInput ->setEditText ("");
	ui.towpilotFirstNameInput ->setEditText ("");

	ui.   pilotLastNameInput ->setEditText ("");
	ui. copilotLastNameInput ->setEditText ("");
	ui.towpilotLastNameInput ->setEditText ("");


	// *** Flight flightModes
	const QList<Flight::Mode> flightModes=Flight::listModes ();
	for (int i=0; i<flightModes.size (); ++i)
		ui.flightModeInput->addItem (Flight::modeText (flightModes.at (i)), flightModes.at (i));


	// *** Towflight flightModes
	const QList<Flight::Mode> towflightModes=Flight::listTowModes ();
	for (int i=0; i<towflightModes.size(); ++i)
		ui.towflightModeInput->addItem (Flight::modeText (towflightModes.at (i)), towflightModes.at (i));


	// *** Launch methods
	QList<LaunchMethod> launchMethods=cache.getLaunchMethods ().getList ();
	ui.launchMethodInput->addItem ("-", invalidId);
	for (int i=0; i<launchMethods.size (); ++i)
		ui.launchMethodInput->addItem (launchMethods.at (i).nameWithShortcut (), launchMethods.at (i).getId ());

	// If there is exactly one launch method, select it
	if (launchMethods.size ()==1)
		ui.launchMethodInput->setCurrentIndex (1);

	// *** Locations
	const QStringList locations=cache.getLocations ();
	ui.       departureLocationInput -> addItem ("");
	ui.         landingLocationInput -> addItem ("");
	ui.towflightLandingLocationInput -> addItem ("");

	ui.       departureLocationInput -> addItems (locations);
	ui.         landingLocationInput -> addItems (locations);
	ui.towflightLandingLocationInput -> addItems (locations);

	// Make sure our location is in the list
	const QString &location=Settings::instance ().location;
	ui.       departureLocationInput ->setEditText (location);
	ui.         landingLocationInput ->setEditText (location);
	ui.towflightLandingLocationInput ->setEditText (location);

	ui.         landingLocationInput ->setEditText ("");
	ui.towflightLandingLocationInput ->setEditText ("");


	// *** Accounting notes
	ui.accountingNoteInput->addItem ("");
	ui.accountingNoteInput->addItems (cache.getAccountingNotes ());
	ui.accountingNoteInput->setEditText ("");
}

void FlightWindow::showEvent (QShowEvent *event)
{
	// We used to manually center the window on its parent widget here.
	// However, both Gnome and Windows XP seem to do that automatically anyway.
	// If this is enabled, the window may be partly outside of the screen on
	// Windows. Make sure to move it back inside so it is completely visible.
//	QWidget *parentWidget=dynamic_cast<QWidget *> (parent ());
//	if (parentWidget)
//	{
//		// Move to the center of the parent widget
//		// TODO: causes flicker - the window is shown in the top left corner
//		// before being moved.
//		// Also: using rect ().center () is probably better.
//		move (
//			parentWidget->x ()+(parentWidget->width  ()-width  ())/2,
//			parentWidget->y ()+(parentWidget->height ()-height ())/2);
//
//		// Attention: make sure it is on the screen completely!
//		// qApp->desktop ()->availableGeometry () may be useful.
//	}

	// On Windows XP (and other environments?) with low screen resolutions,
	// (800x600), it is possible that the window is too large and the buttons
	// are hidden behind the task bar. Resize the window so it fits the
	// available geometry at the bottom.
	// Note that according to the documentation, availableGeometry should
	// exclude the task bar on Windows, so the window should be entirely
	// visible. However, this does not seem to work and the buttons may still
	// be partly occluded by the task bar (but at least they are partially
	// visible).
	int availableHeight=qApp->desktop ()->availableGeometry ().height ();
	if (height ()>availableHeight-y ())
		resize (width (), availableHeight-y ());

	// We dont't set the label heights on spantaneous show events (generated
	// by the window system, e. g. after the window has been minimized).
	if (!event->spontaneous ())
	{
		if (!labelHeightsSet)
		{
			// Set the heights of the labels to the heights of the widgets so the
			// layout won't changed when the widgets are hidden (the labels are not
			// hidden but SkLabel::concealed).

			// For the time editor, the height returned by sizeHint seems to be 1
			// pixel too low (the layout moves by 1 pixel when a timeEdit is
			// hidden). Therefore, we make the labels (layout rows) one pixel
			// higher than the size hint. To compensate the change in layout, we
			// decrease the verticalSpacing by one, unless it is already 0.
			QGridLayout *layout=dynamic_cast<QGridLayout *> (ui.inputFieldPane->layout ());
			int verticalSpacing=layout->verticalSpacing ();
			if (verticalSpacing>0)
				layout->setVerticalSpacing (verticalSpacing-1);

			QMapIterator<QWidget *, SkLabel *> i (widgetLabelMap);
			while (i.hasNext ())
			{
				i.next ();

				QWidget *widget=i.key ();
				SkLabel *label=i.value ();

				// There may be multiple widgets associated with one label, for
				// example for the time fields.
				if (widget && label)
					label->setMinimumHeight (1+qMax (widget->sizeHint ().height (), label->sizeHint ().height ()));
			}

			labelHeightsSet=true;
		}
	}

	QDialog::showEvent (event);
}


// ****************
// ** Invocation **
// ****************

/*
 * Invocation notes:
 *   For modeless windows, the invocation methods will return immediately.
 *   This has several implications:
 *     - The window has to be created on the heap. To make sure it is deleted,
 *       its DeleteOnClose attribute is set.
 *     - If the flight is accepted by the user, it is written to the database
 *       by the accepting slot.
 */

FlightWindow *FlightWindow::createFlight (QWidget *parent, DbManager &manager, QDate date, dbId preselectedLaunchMethod)
{
	FlightWindow *w=new FlightWindow (parent, modeCreate, manager, NULL);
	w->setAttribute (Qt::WA_DeleteOnClose, true);

	w->ui.dateInput->setDate (date);
	w->updateSetup ();
	if (idValid (preselectedLaunchMethod)) w->ui.launchMethodInput->setCurrentItemByItemData (preselectedLaunchMethod);

	w->show ();

	return w;
}

FlightWindow *FlightWindow::repeatFlight (QWidget *parent, DbManager &manager, const Flight &original, QDate date, dbId preselectedLaunchMethod)
{
	FlightWindow *w=new FlightWindow (parent, modeCreate, manager);
	w->setAttribute (Qt::WA_DeleteOnClose, true);
	w->flightToFields (original, true, preselectedLaunchMethod);

	w->ui.dateInput->setDate (date);
	w->updateSetup ();

	w->ui.launchMethodInput->setFocus ();

	w->show ();

	return w;
}

FlightWindow *FlightWindow::editFlight (QWidget *parent, DbManager &manager, Flight &flight)
{
	FlightWindow *w=new FlightWindow (parent, modeEdit, manager);
	w->setAttribute (Qt::WA_DeleteOnClose, true);
	w->flightToFields (flight, false);

	w->updateSetup ();
	w->updateErrors (true);

	w->show ();

	return w;
}



// **********************
// ** Input field data **
// **********************

/**
 * Write name parts (either last names or first names) to a list box - either
 * only those which match another name part (for example, only the first names
 * matching a given last name) or all values for a name part, if the other name
 * part is empty.
 *
 * The name parts are read from the Cache.
 *
 * Additionally, if the name part is unique, it is written to the target list
 * box's current text if that was empty before. This can be prevented using the
 * preserveTarget parameter.
 *
 * @param fullListMethod the method of Cache used to retrieve all values
 *                       of the name part
 * @param partialListMethod the method of Cache used to retrieve all
 *                          values of the name part which match a given other
 *                          name part
 * @param target the combo box to write the name parts to
 * @param otherName the given other name part
 * @param preserveTarget whether to preserve the current text of the target
 *                       combo box, even if it is empty
 * @return the number of matching names (not: people)
 */
int FlightWindow::fillNames (QStringList (Cache::*fullListMethod)(), QStringList (Cache::*partialListMethod)(const QString &), QComboBox *target, const QString &otherName, bool preserveTarget)
{
	// Store the old value of the target field
	QString oldValue=target->currentText ();

	// Get the list of name parts from the data storage, either the full list
	// or only the ones that match the other name part given.
	QStringList nameList;
	if (otherName.simplified ().isEmpty ())
		nameList=(cache.*fullListMethod)();
	else
		nameList=(cache.*partialListMethod)(otherName);

	// Write the name list to the target
	target->clear ();
	target->addItem ("");
	target->addItems (nameList);

	// If there is exactly one name and the target field was empty before and
	// doesn't have to be preserved, and the source name is not empty, write
	// the unique name to the target field.
	if (oldValue.isEmpty () && !preserveTarget && nameList.size ()==1 && !otherName.simplified ().isEmpty ())
		target->setEditText (nameList.at (0));
	else
		target->setEditText (oldValue);

	return nameList.size ();
}

dbId FlightWindow::fillLastNames  (bool active, QComboBox *target, const QString &firstName, bool preserveTarget)
{
	if (!active) return invalidId;

	fillNames (
		&Cache::getPersonLastNames,
		&Cache::getPersonLastNames,
		target, firstName, preserveTarget);

	// Even if there were multiple matching other name parts, the current
	// combination may still be unique. If it is, return the person's ID.
	return cache.getUniquePersonIdByName (firstName, target->currentText ());
}

dbId FlightWindow::fillFirstNames  (bool active, QComboBox *target, const QString &lastName, bool preserveTarget)
{
	if (!active) return invalidId;

	fillNames (
		&Cache::getPersonFirstNames,
		&Cache::getPersonFirstNames,
		target, lastName, preserveTarget);

	// Even if there were multiple matching other name parts, the current
	// combination may still be unique. If it is, return the person's ID.
	return cache.getUniquePersonIdByName (target->currentText (), lastName);
}

// ******************
// ** Error checks **
// ******************

void FlightWindow::updateErrors (bool setFocus)
{
	if (!isErrorListActive ()) return;

	Flight flight=determineFlightBasic ();

	Plane *plane              =cache.getNewObject<Plane       > (flight.planeId        );
	Plane *towplane           =cache.getNewObject<Plane       > (flight.towplaneId     );
	LaunchMethod *launchMethod=cache.getNewObject<LaunchMethod> (flight.launchMethodId );

	FlightError error;
	int errorIndex=0;
	int numErrors=0;
	QWidget *firstErrorWidget=NULL;

	// Reset the error state of all labels
	foreach (SkLabel * const & label, widgetLabelMap.values ())
		label->setError (false);

	ui.errorList->clear ();
	while ((error=flight.errorCheck (&errorIndex, true, launchMethod && launchMethod->isAirtow (), plane, towplane, launchMethod))!=ff_ok)
	{
		// In the cases of unknown or non-unique people, we don't want to query
		// the user. So the determineFlightBasic method uses the buffered IDs
		// (selectedX, see header), which may be invalid if the corresponding
		// entity is unknown or non-unique. This may lead to erroneous "not
		// specified" errors being reported. Check the input fields if
		// something is specified and skip the error if necessary.

		bool skipError=false;

		bool    planeSpecified=!(isNone (getCurrentRegistration         ()) || getCurrentRegistration         ()==Plane::defaultRegistrationPrefix ());
		bool towplaneSpecified=!(isNone (getCurrentTowplaneRegistration ()) || getCurrentTowplaneRegistration ()==Plane::defaultRegistrationPrefix ());
		bool    pilotSpecified=!isNone (getCurrentPilotLastName    (), getCurrentPilotFirstName    ());
		bool  copilotSpecified=!isNone (getCurrentCopilotLastName  (), getCurrentCopilotFirstName  ());
		//bool towpilotSpecified=!isNone (getCurrentTowpilotLastName (), getCurrentTowpilotFirstName ());

		// Potential non-error: something has been specified but is unknown or
		// non-unique, so the ID is invalid.
		if (error==ff_kein_flugzeug           &&    planeSpecified) skipError=true;
		if (error==ff_kein_pilot              &&    pilotSpecified) skipError=true;
		if (error==ff_schulung_ohne_begleiter &&  copilotSpecified) skipError=true;
		if (error==ff_kein_schleppflugzeug    && towplaneSpecified) skipError=true;

		if (!skipError)
		{
			++numErrors;

			ui.errorList->addItem (flight.errorDescription (error));

			QWidget *errorWidget=getErrorWidget (error);
			if (errorWidget)
			{
				if (!firstErrorWidget) firstErrorWidget=errorWidget;

				// There may be multiple labels for the widget. This uses the most
				// recently inserted one.
				SkLabel *errorWidgetLabel=widgetLabelMap.value (errorWidget);
				if (errorWidgetLabel)
					errorWidgetLabel->setError (true);
			}
		}
	}

	delete plane;
	delete towplane;
	delete launchMethod;

	if (setFocus && firstErrorWidget)
		firstErrorWidget->setFocus ();

	if (numErrors==0)
	{
		ui.errorList->addItem ("Keine");
		ui.errorList->setPalette (palette ());
	}
	else
	{
		QPalette pal=ui.errorList->palette ();
		pal.setColor (QPalette::Base, errorColor);
		ui.errorList->setPalette (pal);
	}
}

// Fehlerbehandlung
QWidget *FlightWindow::getErrorWidget (FlightError error)
{
	switch (error)
	{
		case ff_keine_id:                             return NULL;
		case ff_ok:                                   return NULL;
		case ff_kein_flugzeug:                        return ui.registrationInput;
		case ff_kein_pilot:                           return ui.pilotLastNameInput;
		case ff_pilot_gleich_begleiter:               return ui.copilotLastNameInput;
		case ff_pilot_nur_nachname:                   return ui.pilotFirstNameInput;
		case ff_pilot_nur_vorname:                    return ui.pilotLastNameInput;
		case ff_pilot_nicht_identifiziert:            return ui.pilotLastNameInput;
		case ff_begleiter_nur_nachname:               return ui.copilotFirstNameInput;
		case ff_begleiter_nur_vorname:                return ui.copilotLastNameInput;
		case ff_begleiter_nicht_identifiziert:        return ui.copilotLastNameInput;
		case ff_schulung_ohne_begleiter:              return ui.copilotLastNameInput;
		case ff_begleiter_nicht_erlaubt:              return ui.copilotLastNameInput;
		case ff_nur_gelandet:                         return ui.landingTimeInput;
		case ff_landung_vor_start:                    return ui.landingTimeInput;
		case ff_keine_startart:                       return ui.launchMethodInput;
		case ff_kein_modus:                           return ui.flightModeInput;
		case ff_kein_sfz_modus:                       return ui.towflightModeInput;
		case ff_kein_flugtyp:                         return ui.flightTypeInput;
		case ff_landungen_negativ:                    return ui.numLandingsInput;
		case ff_doppelsitzige_schulung_in_einsitzer:  return ui.copilotLastNameInput;
		case ff_kein_startort:                        return ui.departureLocationInput;
		case ff_kein_zielort:                         return ui.landingLocationInput;
		case ff_kein_zielort_sfz:                     return ui.towflightLandingLocationInput;
		case ff_segelflugzeug_landungen:              return ui.numLandingsInput;
		case ff_begleiter_in_einsitzer:               return ui.copilotLastNameInput;
		case ff_gastflug_in_einsitzer:                return ui.flightTypeInput;
		case ff_segelflugzeug_selbststart:            return ui.launchMethodInput;
		case ff_schlepp_nur_gelandet:                 return ui.towflightLandingTimeInput;
		case ff_schlepp_landung_vor_start:            return ui.towflightLandingTimeInput;
		case ff_landungen_null:                       return ui.numLandingsInput;
		case ff_landungen_ohne_start:                 return ui.numLandingsInput;
		case ff_segelflugzeug_landungen_ohne_landung: return ui.numLandingsInput;
		case ff_startort_gleich_zielort:              return ui.landingLocationInput;
		case ff_kein_schleppflugzeug:                 return ui.towplaneRegistrationInput;
		case ff_towplane_is_glider:                   return ui.towplaneRegistrationInput;
		case ff_pilot_gleich_towpilot:                return ui.pilotLastNameInput;
		case ff_towpilot_nur_nachname:                return ui.towpilotFirstNameInput;
		case ff_towpilot_nur_vorname:                 return ui.towpilotLastNameInput;
		case ff_towpilot_nicht_identifiziert:         return ui.towpilotLastNameInput;
		// No default to allow compiler warnings
	}

	return NULL;
}




// ****************************
// ** Flight reading/writing **
// ****************************

void FlightWindow::personToFields (dbId id, SkComboBox *lastNameInput, SkComboBox *firstNameInput, QString incompleteLastName, QString incompleteFirstName)
{
	// Note that filling the name parts is done here rather than from
	// updateSetup because that function is called on every field change.
	bool ok=false;

	if (idValid (id))
	{
		try
		{
			Person person=cache.getObject<Person> (id);
			 lastNameInput->setEditText (person.lastName);
			firstNameInput->setEditText (person.firstName );
			ok=true;
		}
		catch (Cache::NotFoundException &ex) {}
	}

	if (!ok)
	{
		 lastNameInput->setEditText (incompleteLastName );
		firstNameInput->setEditText (incompleteFirstName);
	}

	fillLastNames  (true, lastNameInput , firstNameInput->currentText (), true);
	fillFirstNames (true, firstNameInput, lastNameInput->currentText  (), true);
}

void FlightWindow::planeToFields (dbId id, SkComboBox *registrationInput, SkLabel *typeLabel)
{
	// Note that filling the plane type is done here rather than from
	// updateSetup because that function is called on every field change.
	if (idValid (id))
	{
		try
		{
			Plane plane=cache.getObject<Plane> (id);
			registrationInput->setEditText (plane.registration);
			typeLabel->setText (plane.type);
		}
		catch (Cache::NotFoundException &ex) {}
	}
}

/**
 * Sets up the edit fields to contain the data of a given flight
 *
 * @param flight
 * @param repeat whether to setup the fields for repeating (true) or editing
 *        (false) a flight; this affects only some of the fields
 * @param preselectedLaunchMethod the preselected launch method; only relevant
 *        if repeat is true
 */
void FlightWindow::flightToFields (const Flight &flight, bool repeat, dbId preselectedLaunchMethod)
{
	originalFlight=flight;

	// Note that for repeating, some fields are not set or set differently.

	originalFlightId = flight.getId ();
	selectedPlane    = flight.planeId;
	selectedTowplane = flight.towplaneId;
	selectedPilot    = flight.pilotId;
	selectedCopilot  = flight.copilotId;
	selectedTowpilot = flight.towpilotId;

	planeToFields (flight.planeId, ui.registrationInput, ui.planeTypeWidget);
	ui.flightTypeInput->setCurrentItemByItemData (flight.type);

	// space

	personToFields (flight.pilotId  , ui.pilotLastNameInput  , ui.pilotFirstNameInput  , flight.pilotLastName  , flight.pilotFirstName  );
	personToFields (flight.copilotId, ui.copilotLastNameInput, ui.copilotFirstNameInput, flight.copilotLastName, flight.copilotFirstName);

	// space

	ui.flightModeInput->setCurrentItemByItemData (flight.mode);

	// Launch method: on repeating a flight, the launch method is not copied because
	// it may be different from before (different winch). An exception is self
	// launch because it is unlikely that a plane which did a self launch will
	// use another launch method later.
	bool copyLaunchMethod=!repeat;

	try
	{
		// If the launch method is a self launch, copy it
		if (idValid (flight.launchMethodId))
			if (cache.getObject<LaunchMethod> (flight.launchMethodId).type==LaunchMethod::typeSelf)
				copyLaunchMethod=true;
	}
	catch (Cache::NotFoundException &ex)
	{
		log_error ("Launch method not found in FlightWindow::flightToFields");
	}

	// If the launch method is to be copied, use the launch method from the
	// flight. Otherwise, if a launch method is preselected, use it.
	if (copyLaunchMethod)
		ui.launchMethodInput->setCurrentItemByItemData (flight.launchMethodId);
	else if (idValid (preselectedLaunchMethod))
		ui.launchMethodInput->setCurrentItemByItemData (preselectedLaunchMethod);

	launchMethodChanged (ui.launchMethodInput->currentIndex ());

	// The towplane is set even if it's not an airtow in case the user selects
	// an unknown airtow launchMethod later.
	planeToFields (flight.towplaneId, ui.towplaneRegistrationInput, ui.towplaneTypeWidget);
	personToFields (flight.towpilotId, ui.towpilotLastNameInput, ui.towpilotFirstNameInput, flight.towpilotLastName, flight.towpilotFirstName);
	ui.towflightModeInput->setCurrentItemByItemData (flight.towflightMode);

	// space

	if (!repeat)
	{
		ui.       departureTimeCheckbox->setChecked (getTimeFieldCheckboxValue (flight.departed   ));
		ui.         landingTimeCheckbox->setChecked (getTimeFieldCheckboxValue (flight.landed    ));
		ui.towflightLandingTimeCheckbox->setChecked (getTimeFieldCheckboxValue (flight.towflightLanded));

		ui.       departureTimeInput->setTime (flight.departureTime       .toUTC ().time ()); // Even if not active
		ui.         landingTimeInput->setTime (flight.landingTime         .toUTC ().time ()); // Even if not active
		ui.towflightLandingTimeInput->setTime (flight.towflightLandingTime.toUTC ().time ()); // Even if not active
	}

	// space

	ui.departureLocationInput->setEditText (flight.departureLocation);
	ui.landingLocationInput->setEditText (flight.landingLocation);
	ui.towflightLandingLocationInput->setEditText (flight.towflightLandingLocation);
	if (!repeat) ui.numLandingsInput->setValue (flight.numLandings);

	// space

	if (!repeat) ui.commentInput->setText (flight.comments);
	ui.accountingNoteInput->setEditText (flight.accountingNotes);
	ui.dateInput->setDate (flight.getEffectiveDate (Qt::UTC, QDate::currentDate ()));

#undef PLANE
#undef PERSON
}

Flight FlightWindow::determineFlightBasic () throw ()
{
	Flight flight;

	// Some of the data is taken from the stored data
	flight.setId (originalFlightId);
	flight.planeId     = isRegistrationActive         ()?selectedPlane   :invalidId;
	flight.towplaneId  = isTowplaneRegistrationActive ()?selectedTowplane:invalidId;
	flight.pilotId     = isPilotActive                ()?selectedPilot   :invalidId;
	flight.copilotId   = isCopilotActive              ()?selectedCopilot :invalidId;
	flight.towpilotId  = isTowpilotActive             ()?selectedTowpilot:invalidId;


	// Some of the data can just be copied to the flight.
	// Registration: may have to query user
	if (isFlightTypeActive                   ()) flight.type            =getCurrentFlightType ();
	//
	// Pilot: may have to query user
	// Copilot: may have to query user
	//
	if (isFlightModeActive                   ()) flight.mode              =getCurrentFlightMode ();
	if (isLaunchMethodActive                 ()) flight.launchMethodId    =getCurrentLaunchMethodId ();
	//
	// Towplane registration: may have to query user
	// Towpilot: may have to query user
	if (isTowflightModeActive                ()) flight.towflightMode          =getCurrentTowflightMode ();
	//
	if (isDepartureActive                    ()) flight.departed         =isDepartureTimeActive ();
	if (isLandingActive                      ()) flight.landed           =isLandingTimeActive ();
	if (isTowflightLandingActive             ()) flight.towflightLanded  =isTowflightLandingTimeActive ();
	// Departure time: set with date
	// Landing time: set with date
	// Towflight landing time: set with date
	//
	if (isDepartureLocationActive        ()) flight.departureLocation        =getCurrentDepartureLocation ().simplified ();
	if (isLandingLocationActive          ()) flight.landingLocation          =getCurrentLandingLocation ().simplified ();
	if (isTowflightLandingLocationActive ()) flight.towflightLandingLocation =getCurrentTowflightLandingLocation ().simplified ();
	if (isNumLandingsActive              ()) flight.numLandings              =getCurrentNumLandings ();
	//
	if (isCommentActive                      ()) flight.comments        =getCurrentComment ().simplified ();
	if (isAccountingNodeActive               ()) flight.accountingNotes =getCurrentAccountingNote ().simplified ();
	// getCurrentDate


	// Setting the times requires combining the date and time fields
	QDate date= (isDateActive()) ? (getCurrentDate ()) : QDate::currentDate ();
	// TODO secs=0
#define SET_TIME(active, target, value) do { if (active) target=QDateTime (date, value, Qt::UTC); else target=QDateTime (); } while (0)
	SET_TIME (isDepartureTimeActive        (), flight.departureTime,        getCurrentDepartureTime        ());
	SET_TIME (isLandingTimeActive          (), flight.landingTime,          getCurrentLandingTime          ());
	SET_TIME (isTowflightLandingTimeActive (), flight.towflightLandingTime, getCurrentTowflightLandingTime ());
#undef SET_TIME


	return flight;
}

void FlightWindow::errorCheck (const QString &problem, QWidget *widget)
	throw (FlightWindow::AbortedException)
{
	if (!confirmProblem (this, "Fehler", problem))
	{
		if (widget) widget->setFocus ();
		throw AbortedException ();
	}
}

void FlightWindow::checkFlightPhase1 (const Flight &flight, bool departNow)
	throw (FlightWindow::AbortedException)
{
	// Phase 1: plane determined, towplane and people not determined

	// Note that we use the values from the passed flight, not from the editor
	// fields.

	if ((departNow || flight.departed) && flight.departsHere () && idInvalid (flight.launchMethodId))
		errorCheck ("Es wurde keine Startartart angegeben.",
			ui.launchMethodInput);

	if ((flight.departed || !flight.departsHere ()) && isNone (flight.departureLocation))
		errorCheck ("Es wurde kein Startort angegeben.",
				ui.departureLocationInput);

	if ((flight.landed || !flight.landsHere ()) && isNone (flight.landingLocation))
		errorCheck ("Es wurde kein Zielort angegeben.",
			ui.landingLocationInput);

	if ((flight.departsHere ()!=flight.landsHere ()) && (flight.departureLocation.simplified ()==flight.landingLocation.simplified ()))
		errorCheck ("Der Startort ist gleich dem Zielort.",
			flight.departsHere ()?ui.landingLocationInput:ui.departureLocationInput);

	if (flight.landed && flight.departed && flight.departureTime>flight.landingTime)
		errorCheck ("Die Landezeit des Flugs liegt vor der Startzeit.",
			ui.landingTimeInput);

	if (flight.departsHere () && flight.landsHere () && flight.landed && !flight.departed)
		errorCheck ("Es wurde eine Landezeit, aber keine Startzeit angegeben.",
				ui.landingTimeInput);

	if (flight.landsHere () && flight.landed && flight.numLandings==0)
		errorCheck ("Es wurde eine Landezeit angegeben, aber die Anzahl der Landungen ist 0.",
			ui.numLandingsInput);

	if (flight.towflightLanded && !flight.towflightLandsHere () && isNone (flight.towflightLandingLocation))
		errorCheck (utf8 ("Es wurde kein Zielort für das Schleppflugzeug angegeben."),
			ui.towflightLandingLocationInput);

	if (flight.departed && flight.towflightLanded && flight.departureTime>flight.towflightLandingTime)
		errorCheck ("Die Landezeit des Schleppflugs liegt vor der Startzeit.",
			ui.towflightLandingTimeInput);

	if (flight.towflightLanded && !flight.departed)
		errorCheck ("Es wurde eine Landezeit des Schleppflugs, aber keine Startzeit angegeben.",
			ui.towflightLandingTimeInput);
}

void FlightWindow::checkFlightPhase2 (const Flight &flight, bool departNow, const Plane *plane, const Plane *towplane, const LaunchMethod *launchMethod)
	throw (FlightWindow::AbortedException)
{
	// Phase 2: plane and towplane determined, people not determined

	if (idValid (flight.planeId) && flight.planeId==flight.towplaneId)
		errorCheck ("Flugzeug und Schleppflugzeug sind identisch.",
			ui.towplaneRegistrationInput);

	if (plane && launchMethod &&
		flight.numLandings>1 && plane->category==Plane::categoryGlider && !(launchMethod && launchMethod->type==LaunchMethod::typeAirtow))
		errorCheck (QString ("Laut Datenbank ist das Flugzeug %1 (%2) ein Segelflugzeug.\nEs wurde jedoch mehr als eine Landung angegeben.")
			.arg (plane->registration).arg (plane->type),
			ui.numLandingsInput);

	if (plane &&
		flight.numLandings>0 && !flight.landed && plane->category==Plane::categoryGlider && !(launchMethod && launchMethod->type==LaunchMethod::typeAirtow))
		errorCheck (QString ("Laut Datenbank ist das Flugzeug %1 (%2) ein Segelflugzeug.\nEs wurden jedoch eine Landung, aber keine Landezeit angegeben.")
			.arg (plane->registration).arg (plane->type),
			ui.numLandingsInput);

	if (plane &&
		plane->category==Plane::categoryGlider && launchMethod && launchMethod->type==LaunchMethod::typeSelf)
		errorCheck (QString ("Laut Datenbank ist das Flugzeug %1 ein Segelflugzeug.\nEs wurden jedoch \"Eigenstart\" als Startart angegeben.")
			.arg (plane->registrationWithType ()),
			ui.launchMethodInput);

	if (plane && launchMethod &&
		(plane->category==Plane::categoryAirplane || plane->category==Plane::categoryUltralight) && launchMethod->type!=LaunchMethod::typeSelf)
		errorCheck (QString ("Laut Datenbank ist das Flugzeug %1 ein Motorflugzeug.\nEs wurden jedoch eine andere Startart als \"Eigenstart\" angegeben.")
			.arg (plane->registrationWithType ()),
			ui.departureTimeInput);

	if (plane && launchMethod &&
		plane->numSeats==1 && (flight.type==Flight::typeGuestPrivate || flight.type==Flight::typeGuestExternal) && launchMethod->type!=LaunchMethod::typeSelf)
		errorCheck (QString ("Laut Datenbank ist das Flugzeug %1 (%2) einsitzig.\nEs wurden jedoch der Flugtyp \"Gastflug\" angegeben.")
			.arg (plane->registration).arg (plane->type),
			ui.registrationInput);

	if (plane && launchMethod &&
		plane->numSeats==1 && flight.type==Flight::typeTraining2 && launchMethod->type!=LaunchMethod::typeSelf)
		errorCheck (QString ("Laut Datenbank ist das Flugzeug %1 (%2) einsitzig.\nEs wurden jedoch der Flugtyp \"Doppelsitzige Schulung\" angegeben.")
			.arg (plane->registration).arg (plane->type),
			ui.registrationInput);

	if (towplane &&
		towplane->category==Plane::categoryGlider)
		errorCheck (QString ("Laut Datenbank ist das Schleppflugzeug %1 (%2) ein Segelflugzeug.")
			.arg (plane->registration).arg (plane->type),
			ui.towplaneRegistrationInput);

	if (plane && departNow && idValid (cache.planeFlying (plane->getId ())))
		errorCheck (QString ("Laut Datenbank fliegt das Flugzeug %1 noch.")
			.arg (plane->registration),
			ui.registrationInput);

	if (towplane && departNow && idValid (cache.planeFlying (towplane->getId ())))
		errorCheck (QString ("Laut Datenbank fliegt das Schleppflugzeug %1 noch.")
			.arg (towplane->registration),
			ui.registrationInput);
}

/**
 * Performs medical the validity check for a given person
 *
 * The person may be NULL, in which case no checks are performed.
 *
 * @param person the person to check or NULL
 * @return
 */
void FlightWindow::checkMedical (const Person *person, const QString &ofThePersonText)
{
	// Check medical | Date  |
	// flag          | given | Action
	// --------------+-------+---------------------------------
	// false         | *     | No check
	// true          | false | Warning: no date given
	// true          | true  | Check (warning: medical expired)

	// Person not specified
	if (!person)
		return;

	// Medical check disabled for this person
	if (!person->checkMedical)
		return;

	if (person->medicalValidity.isValid ())
	{
		// Regular medical check
		if (person->medicalValidity < ui.dateInput->date ())
			errorCheck (utf8 (
				"Laut Datenbank ist das Medical "
				"%1 am %2 abgelaufen."
				).arg (ofThePersonText, toString (person->medicalValidity)), this);
	}
	else
	{
		// No medical date specified (but check enabled)
		errorCheck (utf8 (
			"Es ist kein Ablaufdatum für das Medical %1 "
			"eingetragen, die Medicalprüfung ist aber aktiviert."
			).arg (ofThePersonText), this);
	}
}

void FlightWindow::checkFlightPhase3 (const Flight &flight, bool departNow, const Plane *plane, const Person *pilot, const Person *copilot, const Person *towpilot)
	throw (FlightWindow::AbortedException)
{
	// Phase 3: plane, towplane and people determined

	// Pilot und Begleiter identisch
	if (idValid (flight.pilotId) && flight.pilotId==flight.copilotId)
		errorCheck ("Pilot und Begleiter sind identisch.",
			ui.pilotLastNameInput);

	if (idValid (flight.pilotId) && flight.pilotId==flight.towpilotId)
		errorCheck ("Pilot und Schlepppilot sind identisch.",
			ui.towpilotLastNameInput);

	if (idValid (flight.copilotId) && flight.copilotId==flight.towpilotId)
		errorCheck ("Begleiter und Schlepppilot sind identisch.",
			ui.towpilotLastNameInput);

	if (flight.type==Flight::typeTraining2 && !flight.copilotSpecified ())
		errorCheck ("Doppelsitzige Schulung ohne Fluglehrer.",
			ui.copilotLastNameInput);

	if (plane && plane->numSeats==1 && flight.copilotSpecified ())
		errorCheck (QString ("Laut Datenbank ist das Flugzeug %1 (%2) einsitzig.\nEs wurden jedoch ein Begleiter angegeben.")
		.arg (plane->registration).arg (plane->type),
		ui.registrationInput);

	// TODO use Flight::pilotDescription
	if (pilot && departNow && idValid (cache.personFlying (pilot->getId ())))
		errorCheck (QString ("Laut Datenbank fliegt der Pilot %1 noch.")
			.arg (pilot->fullName ()),
			ui.pilotLastNameInput);

	// TODO use Flight::copilotDescription
	if (copilot && departNow && idValid (cache.personFlying (copilot->getId ())))
		errorCheck (QString ("Laut Datenbank fliegt der Begleiter %1 noch.")
			.arg (copilot->fullName ()),
			ui.copilotLastNameInput);

	// TODO use Flight::towpilotDescription
	if (towpilot && departNow && idValid (cache.personFlying (towpilot->getId ())))
		errorCheck (QString ("Laut Datenbank fliegt der Schlepppilot %2 noch.")
			.arg (towpilot->fullName ()),
			ui.towpilotLastNameInput);

	if (Settings::instance ().checkMedicals)
	{
		// TODO use Flight::pilotDescription
		checkMedical (pilot, flight.isTraining ()?utf8 ("des Flugschülers"):utf8 ("des Piloten"));

		// TODO use Flight::copilotDescription and create
		// Flight::typeCopilotMedicalRequired
		if (flight.type==Flight::typeTraining2)
			checkMedical (copilot, utf8 ("des Fluglehrers"));
	}
}

void FlightWindow::determineFlightPlanes (Flight &flight)
	throw (FlightWindow::AbortedException)
{
	// Determine the plane
	if (isRegistrationActive ())
		flight.planeId=determineAndEnterPlane (getCurrentRegistration (), "Flugzeug", ui.registrationInput, ui.planeTypeWidget);

	// For an unknown airtow, determine the towplane
	if (isTowplaneRegistrationActive ())
		flight.towplaneId=determineAndEnterPlane (getCurrentTowplaneRegistration (), "Schleppflugzeug", ui.towplaneRegistrationInput, ui.towplaneTypeWidget);

	// For a known airtow, determine the towplane (lets the user add it to the
	// database)
	if (currentIsAirtow () && idValid (getCurrentLaunchMethodId ()))
	{
		LaunchMethod launchMethod=getCurrentLaunchMethod ();
		if (launchMethod.towplaneKnown ())
			determineAndEnterPlane (launchMethod.towplaneRegistration, "Schleppflugzeug", NULL, ui.towplaneTypeWidget);
	}
}

void FlightWindow::determineFlightPeople (Flight &flight, const LaunchMethod *launchMethod)
	throw (FlightWindow::AbortedException)
{
	bool pilotRequired=true;
	if (!flight.departsHere ()) pilotRequired=false;
	if (launchMethod && launchMethod->type!=LaunchMethod::typeSelf) pilotRequired=false;

	// Determine the pilot
	selectedPilot=
	flight.pilotId=
		determineAndEnterPerson (
			isPilotActive(),
			getCurrentPilotLastName (),
			getCurrentPilotFirstName (),
			Flight::typePilotDescription (getCurrentFlightType ()),
			pilotRequired,
			flight.pilotLastName, flight.pilotFirstName,
			selectedPilot,
			ui.pilotLastNameInput,
			ui.pilotFirstNameInput);

	// Determine the copilot
	selectedCopilot=
	flight.copilotId=
		determineAndEnterPerson (
			isCopilotActive (),
			getCurrentCopilotLastName (),
			getCurrentCopilotFirstName (),
			Flight::typeCopilotDescription (getCurrentFlightType ()),
			false, // Copilot is never required; flight instructor is checked later
			flight.copilotLastName, flight.copilotFirstName,
			selectedCopilot,
			ui.copilotLastNameInput,
			ui.copilotFirstNameInput);

	// Determine the towpilot
	selectedTowpilot=
	flight.towpilotId=
		determineAndEnterPerson (
			isTowpilotActive(),
			getCurrentTowpilotLastName(),
			getCurrentTowpilotFirstName(),
			"Schlepppilot",
			true, // required
			flight.towpilotLastName, flight.towpilotFirstName,
			selectedTowpilot,
			ui.towpilotLastNameInput,
			ui.towpilotFirstNameInput);
}

/**
 * Reads a flight from the input fields, querying the user for additional input
 * (unknown planes, multiple people) if necessary. Additional data is read from
 * the cache as required.
 *
 * Also does some error checks and asks the user if he wants to accept anyway.
 *
 * @return the flight, if there are no errors in the flight or all errors have
 *         been confirmed by the user
 * @throw AbortedException if the user aborts on data input or warning
 */
Flight FlightWindow::determineFlight (bool departNow)
	throw (FlightWindow::AbortedException)
{
	/*
	 * Notes:
	 *   - for some of the data, we have to query the user. This may result
	 *     in an AbortedException
	 *   - people are determined last because if the person's name is
	 *     non-unique, the user has to be queried and we don't want to do that
	 *     more often than necessary
	 */

	Plane *plane=NULL, *towplane=NULL;
	Person *pilot=NULL, *copilot=NULL, *towpilot=NULL;
	LaunchMethod *launchMethod=NULL;

	try
	{
		// Phase 1: basic data
		Flight flight=determineFlightBasic ();

		launchMethod=cache.getNewObject<LaunchMethod> (flight.launchMethodId);

		checkFlightPhase1 (flight, departNow);

		// Phase 2: planes
		determineFlightPlanes (flight);

		plane   =cache.getNewObject<Plane> (flight.planeId);
		towplane=cache.getNewObject<Plane> (flight.towplaneId);

		checkFlightPhase2 (flight, departNow, plane, towplane, launchMethod);

		// Phase 3: people
		determineFlightPeople (flight, launchMethod);

		pilot   =cache.getNewObject<Person> (flight.pilotId    );
		copilot =cache.getNewObject<Person> (flight.copilotId);
		towpilot=cache.getNewObject<Person> (flight.towpilotId );

		checkFlightPhase3 (flight, departNow, plane, pilot, copilot, towpilot);

		return flight;
	}
	catch (...)
	{
		delete plane;
		delete towplane;

		delete pilot;
		delete copilot;
		delete towpilot;

		delete launchMethod;

		throw;
	}
}


/**
 * Tries to determine the plane for a given registration by retrieving
 * additional data from the dataSource and querying the user if necessary.
 *
 * This method takes the following steps, in order, and uses the first result:
 *  - If no registration is given, the user is asked if this is on purpose
 *  - If there is a plane with the specified registration, it is used
 *  - If there is a plane with the specified registration and the default
 *    registration prefix prepended, it is used
 *  - The user is asked if he wants to addObject the plane to the database
 * If none of these succeeds, an AbortedException is thrown
 *
 * @param registration the registration given by the user
 * @param description the description of the plane for displaying to the user.
 *                    This can be used to distinguish between differen planes.
 * @return the ID of the resulting plane, or invalid if "no plane" was
 *         confirmed by the user
 * @throw AbortedException if the user aborted the selection
 */
dbId FlightWindow::determinePlane (QString registration, QString description, QWidget *widget)
	throw (FlightWindow::AbortedException)
{
	dbId id=invalidId;

	// Check if no registration is given. Return true if the user confirms or
	// false else.
	if (isNone (registration) || registration.simplified ().toLower()==Plane::defaultRegistrationPrefix ().simplified ().toLower ())
	{
		// No registration given. Query the user to accept this.
		if (!confirmProblem (this,
			QString ("Kein %1 angegeben").arg (description),
			QString ("Es wurde kein %1 angegeben.").arg (description)))
			throw AbortedException ();

		// User accepted
		return invalidId;
	}

	// Try to get the ID for the plane with the given registration. Return if
	// found.
	id=cache.getPlaneIdByRegistration (registration);
	if (!idInvalid (id))
		return id;

	// Try to get the ID for the plane with the given registration with the
	// registration prefix prepended. Return if found and the user confirms it.
	id=cache.getPlaneIdByRegistration (Plane::defaultRegistrationPrefix ()+registration);
	if (idValid (id))
	{
		QString title=QString ("%1 nicht bekannt").arg (description);
		QString question=QString (
			"Das %1 %2 ist nicht bekannt.\n"
			"Es gibt allerdings ein Flugzeug mit dem Kennzeichen %3.\n"
			"Soll dieses Flugzeug verwendet werden?")
			.arg (description, registration, Plane::defaultRegistrationPrefix ()+registration);

		if (yesNoQuestion (this, title, question))
			return id;
	}

	QString title=QString ("%1 nicht bekannt").arg (description);
	QString question=QString (
		"Das %1 %2 ist nicht bekannt.\n"
		"Soll es in die Datenbank aufgenommen werden?")
		.arg (description, registration.toUpper ());

	if (yesNoQuestion (this, title, question))
	{
		Plane nameObject;
		nameObject.registration=registration.toUpper ();

		dbId result=ObjectEditorWindow<Plane>::createObject (this, manager, nameObject);
		if (idValid (result))
			return result;
		else
		{
			if (widget) widget->setFocus ();
			throw AbortedException ();
		}
	}
	else
		throw AbortedException ();

	throw AbortedException ();
}

dbId FlightWindow::determineAndEnterPlane (QString registration, QString description, SkComboBox *registrationInput, SkLabel *typeLabel)
	throw (FlightWindow::AbortedException)
{
	dbId result=determinePlane (registration, description, registrationInput);

	if (idValid (result))
	{
		try
		{
			Plane resultPlane=cache.getObject<Plane> (result);
			if (registrationInput) registrationInput->setEditText (resultPlane.registration);

			if (typeLabel) typeLabel->setText (resultPlane.type);
		}
		catch (Cache::NotFoundException) {}
	}

	return result;
}

dbId FlightWindow::createNewPerson (QString lastName, QString firstName)
	throw (AbortedException)
{
	Person nameObject;
	nameObject.lastName=lastName;
	nameObject.firstName=firstName;

	dbId result=ObjectEditorWindow<Person>::createObject (this, manager, nameObject);
	if (idValid (result))
		return result;
	else
		throw AbortedException ();
}

/**
 * Tries to determine the person for a given last name and first name by
 * retrieving additional data from the dataSource and querying the user if
 * necessary.
 *
 * This method takes the following steps:
 *  - If no name is given, the user is asked if this is on purpose (except if
 *    required is false, in which case invalid is returned)
 *  - If a name is given, but it does not exist, ask the user to addObject the person
 *    to the database
 *  - If a name is given and there is a unique person of this name in the
 *    database, the person is returned
 *  - In any other case, the user is shown a list of potential persons and an
 *    "Unknown" and a "Create new" button.
 * If none of these succeeds, an AbortedException is thrown
 *
 * @param active whether the person is active at all; an invalid ID is returned
 *               if the person is not used
 * @param lastName the last name of the person given by the user
 * @param firstName the first name of the person given by the user
 * @param description the description of the person for displaying to the user.
 *                    This can be used to distinguish between differen people.
 * @param required whether the person is required for a regular flight. If a
 *                 person (for example, the pilot) is required, the user has
 *                 to confirm if no name was given.
 * @return the ID of the resulting person, or invalid if "unknown" was
 *         confirmed by the user
 * @throw AbortedException if the user aborted the selection
 */
dbId FlightWindow::determinePerson (bool active, QString lastName, QString firstName, QString description, bool required, QString &incompleteLastName, QString &incompleteFirstName, dbId originalId, QWidget *widget)
	throw (FlightWindow::AbortedException)
{
	if (!active) return invalidId;

	/*
	 *  This is what can happen here:
	 *
	 *  # | Name given | Req'd | Candidates | Action
	 *  --+------------+-------+------------+----------------------------------
	 *  0 | is '+1'    | X     | 0          | Confirm: go on or AbortedException
	 *  1 | Complete   | X     | 0          | Confirm: Add or AbortedException
	 *  2 | Complete   | X     | 1          | Return name
	 *  3 | Complete   | X     | >=1        | Selection list ("Multiple candidates")
	 *  4 | Part       | X     | 0          | Selection list ("Only partial name given")
	 *  5 | Part       | X     | 1          | Selection list ("Only partial name given")
	 *  6 | Part       | X     | >=1        | Selection list ("Only partial name given")
	 *  7 | None       | Yes   | N/A        | Confirm: None or AbortedException
	 *  8 | None       | No    | N/A        | Invalid ID
	 *
	 *  Result from the selection list:
	 *    - Ok: return that person
	 *    - Create new: create a new person
	 *      - Ok: addObject person to database and return it
	 *      - Cancelled: AbortedException
	 *    - Unknown: Store incomplete name part(s)
	 *    - Canceled/no selection: AbortedException
	 *
	 * Note that for non-unique persons, it is OK to store both incomplete name
	 * parts, but unknown persons are required to be added to the database.
	 * This is a pragmatic approach and there may be better solutions.
	 */

	bool lastNameGiven=!isNone (lastName);
	bool firstNameGiven=!isNone (firstName);

	// Case 0: name is "+1"
	if (lastName.simplified ()=="+1" || firstName.simplified ()=="+1")
	{
		QString title=utf8 ("+1 als Name angegeben");
		QString problem=utf8 ("Es wurde \"+1\" als Name angegeben. Für Gastflüge sollte"
			" stattdessen der Flugtyp \"Gastflug\" verwendet werden.");
		if (!confirmProblem (this, title, problem))
			throw AbortedException ();
	}

	// Case 7 and 8: no name was given
	if (!lastNameGiven && !firstNameGiven)
	{
		// Case 8: no name required
		if (!required)
			return invalidId;

		// Case 7: confirm that the name is not known
		QString title=QString ("Kein %1 angegeben").arg (description);
		QString problem=QString ("Es wurde kein %1 angegeben.").arg (description);

		if (!confirmProblem (this, title, problem))
		{
			if (widget) widget->setFocus ();
			throw AbortedException ();
		}
		else
			return invalidId;
	}

	// Get a list of candidates, using all name information available.
	QList<dbId> candidates;
	if (lastNameGiven && firstNameGiven)
		candidates=cache.getPersonIdsByName (lastName, firstName);
	else if (lastNameGiven)
		candidates=cache.getPersonIdsByLastName (lastName);
	else if (firstNameGiven)
		candidates=cache.getPersonIdsByFirstName (firstName);

	// Case 1: complete name given, but no person found
	if (lastNameGiven && firstNameGiven && candidates.empty ())
	{
		// No person of that name was found in the database.
		QString title=QString ("%1 nicht bekannt").arg (description);
		QString question=QString (
			"Die Person %1 %2 (%3) ist nicht bekannt.\n"
			"Soll sie in die Datenbank aufgenommen werden?")
			.arg (capitalize (firstName), capitalize (lastName), description);

		if (yesNoQuestion (this, title, question))
			return createNewPerson (capitalize (lastName), capitalize (firstName));
		else
			throw AbortedException ();
	}

	// Case 2: complete name given and uniqe person found
	if (lastNameGiven && firstNameGiven && candidates.size ()==1)
		return candidates.at (0);

	// Case 3-6: show selection list with candidates, "Unknown" and "Create"
	// options and cancel button

	QString title ("Personenauswahl");
	QString text;
	if (lastNameGiven && firstNameGiven)
		// Case 3: multiple candidates
		text=utf8 ("Es kommen mehrere Personen in Frage. Bitte auswählen (%1):").arg (description);
	else if (!firstNameGiven)
		// Case 4-6: no first name given
		text=utf8 ("Es wurde nur ein Nachname angegeben. Bitte auswählen (%1):").arg (description);
	else if (!lastNameGiven)
		// Case 4-6: no last name given
		text=utf8 ("Es wurde nur ein Vorname angegeben. Bitte auswählen (%1):").arg (description);
	// Note that (!lastNameGiven && !firstNameGiven) has already been handled
	// (case 1)

	// Get all matching people (candidates) from the database, ignoring missing
	// ones
	QList<Person> people=cache.getObjects<Person> (candidates, true);

	// Determine the preselected person
	dbId preselectionId=0;
	if (idValid (originalId))
		preselectionId=originalId;

	// Do the selection
	// There were multiple persons. Let the user select one.
//	EntitySelectWindow<Person> selector (this, "selector");
//	selection_result res=selector.do_selection (title, text, people, preselectionId);

	dbId selectedPerson=invalidId;
	ObjectSelectWindowBase::Result selectionResult=
		ObjectSelectWindow<Person>::select (&selectedPerson, title, text, people, preselectionId, this);

	switch (selectionResult)
	{
		case ObjectSelectWindowBase::resultOk:
			return selectedPerson;
		case ObjectSelectWindowBase::resultUnknown:
			// Unknown person
			incompleteLastName=lastName;
			incompleteFirstName=firstName;
			return 0;
		case ObjectSelectWindowBase::resultNew:
			// Create new
			return createNewPerson (capitalize (lastName), capitalize (firstName));
		case ObjectSelectWindowBase::resultCancelled: case ObjectSelectWindowBase::resultNoneSelected:
			throw AbortedException ();
	}

	assert (!"Unhandled case in FlightWindow::determinePerson");
	return 0;
}

dbId FlightWindow::determineAndEnterPerson (bool active, QString lastName, QString firstName, QString description, bool required, QString &incompleteLastName, QString &incompleteFirstName, dbId originalId, SkComboBox *lastNameWidget, SkComboBox *firstNameWidget) throw (AbortedException)
{
	dbId result=determinePerson (active, lastName, firstName, description, required, incompleteLastName, incompleteFirstName, originalId, lastNameWidget);

	if (idValid (result))
	{
		try
		{
			Person resultPerson=cache.getObject<Person> (result);
			if (lastNameWidget) lastNameWidget->setEditText (resultPerson.lastName);
			if (firstNameWidget) firstNameWidget->setEditText (resultPerson.firstName);
		}
		catch (Cache::NotFoundException) {}
	}

	return result;
}


// **************
// ** Database **
// **************

void FlightWindow::cacheChanged (DbEvent event)
{
	if (mode==modeEdit && event.hasTable<Flight> () && event.getId ()==originalFlightId)
	{
		// The flight we are currently editing...
		if (event.getType ()==DbEvent::typeChange)
		{
			// ...has changed.

			// The only things that can change outside of this editor (since we
			// only allow one editor at a time) are the departure and landing
			// values. See if they changed, and update the editor (only the
			// changed fields)

			Flight newFlight=event.getValue<Flight> ();

			// The flight departed
			if (newFlight.departed!=originalFlight.departed)
			{
				ui.departureTimeCheckbox->setChecked (getTimeFieldCheckboxValue (newFlight.departed));
				ui.departureTimeInput->setTime (newFlight.departureTime.toUTC ().time ()); // Even if not active

				originalFlight.departed=newFlight.departed;
				originalFlight.departureTime=newFlight.departureTime;
			}

			// The flight landed
			if (newFlight.landed!=originalFlight.landed)
			{
				ui.landingTimeCheckbox->setChecked (getTimeFieldCheckboxValue (newFlight.landed));
				ui.landingTimeInput->setTime (newFlight.landingTime.toUTC ().time ()); // Even if not active

				originalFlight.landed=newFlight.landed;
				originalFlight.landingTime=newFlight.landingTime;
			}

			// The towflight landed
			if (newFlight.towflightLanded!=originalFlight.towflightLanded)
			{
				ui.towflightLandingTimeCheckbox->setChecked (getTimeFieldCheckboxValue (newFlight.towflightLanded));
				ui.towflightLandingTimeInput->setTime (newFlight.towflightLandingTime.toUTC ().time ()); // Even if not active

				originalFlight.towflightLanded=newFlight.landed;
				originalFlight.towflightLandingTime=newFlight.towflightLandingTime;
			}

			// The flight performed a landing (final landing or touch-n-go)
			if (newFlight.numLandings!=originalFlight.numLandings)
			{
				ui.numLandingsInput->setValue (newFlight.numLandings);
				originalFlight.numLandings=newFlight.numLandings;
			}

			// Landing locations may be set automatically on landing
			if (newFlight.landingLocation!=originalFlight.landingLocation)
			{
				ui.landingLocationInput->setEditText (newFlight.landingLocation);
				originalFlight.landingLocation=newFlight.landingLocation;

				// For the text fields, the changes are only processed
				// automatically on user input
				on_landingLocationInput_editingFinished (newFlight.landingLocation);
			}

			if (newFlight.towflightLandingLocation!=originalFlight.towflightLandingLocation)
			{
				ui.towflightLandingLocationInput->setEditText (newFlight.towflightLandingLocation);
				originalFlight.towflightLandingLocation=newFlight.towflightLandingLocation;

				// For the text fields, the changes are only processed
				// automatically on user input
				on_towflightLandingLocationInput_editingFinished (newFlight.towflightLandingLocation);
			}
		}
		else if (event.getType ()==DbEvent::typeDelete)
		{
			// ...was deleted.
			reject ();
		}
	}

}

bool FlightWindow::writeToDatabase (Flight &flight)
{
	bool success=false;

	switch (mode)
	{
		case modeCreate:
		{
			try
			{
				success=idValid (manager.createObject (flight, this));
			}
			catch (OperationCanceledException)
			{
				// TODO the cache may now be inconsistent
			}
		} break;
		case modeEdit:
		{
			try
			{
				// May return 0 if nothing changed
				manager.updateObject (flight, this);
				success=true;
			}
			catch (OperationCanceledException)
			{
				// TODO the cache may now be inconsistent
			}
		} break;
	}

	if (!success)
	{
		QMessageBox::critical (
			this,
			"Fehler beim Speichern",
			QString ("Fehler beim Speichern")
			);
	}

	return success;
}


// *****************************
// ** Input field active-ness **
// *****************************

bool FlightWindow::currentIsAirtow ()
{
	if (!isLaunchMethodActive ()) return false;
	if (idInvalid (getCurrentLaunchMethodId ())) return false;

	try
	{
		return getCurrentLaunchMethod ().isAirtow ();
	}
	catch (Cache::NotFoundException &ex)
	{
		return false;
	}
}

bool FlightWindow::isTowplaneRegistrationActive ()
{
	if (!currentIsAirtow ()) return false;
	if (!idValid (getCurrentLaunchMethodId ())) return false;

	try
	{
		return !getCurrentLaunchMethod ().towplaneKnown ();
	}
	catch (Cache::NotFoundException &ex)
	{
		return false;
	}
}

// ***************************
// ** Input value frontends **
// ***************************

LaunchMethod FlightWindow::getCurrentLaunchMethod ()
{
	dbId id=getCurrentLaunchMethodId ();
	return cache.getObject<LaunchMethod> (id);
}


// *******************************
// ** Input field state helpers **
// *******************************


bool FlightWindow::isNowActionPossible ()
{
	// As can be seen from the table, the "Now" action is only possible under
	// the following conditions:
	//   - create mode
	//   - none of the times specified
	//   - the date is today

	if (mode!=modeCreate) return false;

	if (isDepartureTimeActive ()) return false;
	if (isLandingTimeActive ()) return false;
	if (isTowflightLandingTimeActive ()) return false;

	if (getCurrentDate ()!=QDate::currentDate ()) return false;

	// If none of the tests, failed, the "Now" action is possible.
	return true;
}

void FlightWindow::enableWidget (QWidget *widget, bool enabled)
{
	widget->setVisible (enabled);

	QList<SkLabel *> values=widgetLabelMap.values (widget);
	for (int i=0; i<values.size(); ++i)
		// Note the use of setConcealed instead of setVisible/setHidden
		values.at (i)->setConcealed (!enabled);
}

void FlightWindow::disableWidget (QWidget *widget, bool disabled)
{
	enableWidget (widget, !disabled);
}

void FlightWindow::enableWidgets (QWidget *widget0, QWidget *widget1, bool enabled)
{
	enableWidget (widget0, enabled);
	enableWidget (widget1, enabled);
}

void FlightWindow::disableWidgets (QWidget *widget0, QWidget *widget2, bool disabled)
{
	disableWidget (widget0, disabled);
	disableWidget (widget2, disabled);
}

// ******************************
// ** Input field state update **
// ******************************

// TODO rename to departureTime
const QString textDepartureTimeAutomatic        =utf8 ("Automati&sch");
const QString textDepartureTimeDeparted         =utf8 ("Ge&startet"  );
const QString textLandingTimeAutomatic          =utf8 ("&Automatisch");
const QString textLandingTimeLanded             =utf8 ("Gel&andet"   );
const QString textTowflightLandingTimeAutomatic =utf8 ("Au&tomatisch");
const QString textTowflightLandingTimeLanded    =utf8 ("Gelande&t"   );
const QString textTowflightLandingTimeEnded     =utf8 ("Beende&t"    );
const QString textTowflightLandingTime          =utf8 ("Landezeit Schl&eppflugzeug:");
const QString textTowflightEnd                  =utf8 ("Schl&eppende:");
const QString textButtonOk                      =utf8 ("&OK");
const QString textButtonDepartNow               =utf8 ( "&Jetzt starten");
const QString textButtonDepartLater             =utf8 ("S&päter starten");
const QString textButtonLandNow                 =utf8 ( "&Jetzt landen");
const QString textButtonLandLater               =utf8 ("S&päter landen");


/*
 * Notes on field state updates:
 *   - Changes in some field values (e. g. flight type) lead to changes in some
 *     fields' visbility or labels (e. g. copilot's name).
 *   - Field visibility should not depend on data read from the database because
 *     that may be unreliable. Also, we only use the data from the input fields
 *     (getCurrent... methods), never from the flight buffer.
 *   - The matching name parts and plane types are not part of the state.
 *   - The field states are only updated if the value change was caused by user
 *     interaction, not by a programmatic change. This is done to avoid
 *     infinite loops of fields affecting each other (e. g. name pairs) and
 *     changes on intermediate value changes.
 *   - We need the functionality to setup all field states, for example after
 *     loading a flight.
 *   - To avoid code duplication, the state updating functionality is not in the
 *     handlers for the change event. Also, some changes may cause indirect
 *     changes. So all field states are updated on each change of a field value
 *     that can cause any state change.
 *   - This could be improved, for example by using flags for which states need
 *     to be updated, but some multiple states can depend on the same field and
 *     some states depend on multiple fields, so this can get quite complex. It
 *     is probably not worth the work.
 *   - State updates are grouped by widget, not by cause, because some widgets
 *     are affected by multiple causes (e. g. tow flight landing time: by
 *     flightMode.departsHere and launchMethod.isAirtow)
 */

void FlightWindow::updateSetupVisibility ()
{
	//registrationInput - always visible
	//ui.planeTypeWidget - always visible
	//flightTypeInput - always visible
	//
	//pilotLastNameInput, pilotFirstNameInput - always visible
	enableWidgets (ui.copilotLastNameInput, ui.copilotFirstNameInput   , isCopilotActive                      ());
	//
	//flightModeInput - always visible
	enableWidget  (ui.launchMethodInput                                , isLaunchMethodActive                 ());
	//
	enableWidget  (ui.towplaneRegistrationInput                        , isTowplaneRegistrationActive         ());
	enableWidget  (ui.towplaneTypeWidget                               , isTowplaneTypeActive                 ());
	enableWidgets (ui.towpilotLastNameInput, ui.towpilotFirstNameInput , isTowpilotActive                     ());
	enableWidget  (ui.towflightModeInput                               , isTowflightModeActive                ());
	//
	enableWidget (ui.departureTimeCheckbox                             , isDepartureActive                    ());
	ui.departureTimeWidget->setVisible                                   (isDepartureTimeActive                ());
	enableWidget (ui.landingTimeCheckbox                               , isLandingActive                      ());
	ui.landingTimeWidget->setVisible                                     (isLandingTimeActive                  ());
	enableWidget (ui.towflightLandingTimeCheckbox                      , isTowflightLandingActive             ());
	ui.towflightLandingTimeWidget->setVisible                            (isTowflightLandingTimeActive         ());
	//
	//departureLocationInput - always visible
	//landingLocationInput - always visible
	enableWidget (ui.towflightLandingLocationInput,                      isTowflightLandingLocationActive     ());
	//numLandingsInput - always visible
	//
	//commentInput - always visible
	//accountingNoteInput - always visible
	//dateInput - always visible
	enableWidget (ui.errorList,                                          isErrorListActive                    ());
}

void FlightWindow::updateSetupLabels ()
{
	switch (mode)
	{
		case modeCreate:
			// In create mode, we have "Automatic" checkboxes
			ui.          departureTimeCheckbox->setText (textDepartureTimeAutomatic);
			ui.         landingTimeCheckbox->setText (textLandingTimeAutomatic);
			ui.towflightLandingTimeCheckbox->setText (textTowflightLandingTimeAutomatic);
			break;
		case modeEdit:
			// In edit mode, we have "Departed"/"Landed" checkboxes
			ui.          departureTimeCheckbox->setText (textDepartureTimeDeparted);
			ui.         landingTimeCheckbox->setText (textLandingTimeLanded);
			ui.towflightLandingTimeCheckbox->setText (currentTowLandsHere ()?textTowflightLandingTimeLanded:textTowflightLandingTimeEnded);
			break;
	}

	ui.towflightLandingTimeLabel->setText (currentTowLandsHere ()?textTowflightLandingTime:textTowflightEnd);

	ui.pilotLabel  ->setText (Flight::typePilotDescription   (getCurrentFlightType ())+":");
	ui.copilotLabel->setText (Flight::typeCopilotDescription (getCurrentFlightType ())+":");
}

void FlightWindow::updateSetupButtons ()
{
	QPushButton *okButton=ui.buttonBox->button (QDialogButtonBox::Ok);

	bool nowPossible=isNowActionPossible ();
	if (nowPossible && currentDepartsHere ())
	{
		// Depart now/later
		nowButton->setVisible(true);
		nowButton->setText (textButtonDepartNow);
		okButton->setText (textButtonDepartLater);
	}
	else if (nowPossible && currentLandsHere ())
	{
		// Land now/later
		nowButton->setVisible(true);
		nowButton->setText (textButtonLandNow);
		okButton->setText (textButtonLandLater);
	}
	else
	{
		nowButton->setVisible (false);
		okButton->setText (textButtonOk);
	}
}

void FlightWindow::updateSetup ()
{
	updateSetupVisibility ();
	updateSetupLabels ();
	updateSetupButtons ();
}



// *******************************
// ** Input field value helpers **
// *******************************



// *************************************
// ** Input field value change events **
// *************************************

/*
 * Notes about these functions:
 *   - Change other fields as appropriate and call the xChanged method.
 *   - Don't call updateSetup or updateErrors - this is done by the caller.
 */

void FlightWindow::registrationChanged (const QString &text)
{
	// Find out the plane ID
	dbId id=cache.getPlaneIdByRegistration (text);
	selectedPlane=id;

	if (idValid (id))
	{
		// Get the plane
		try
		{
			Plane plane=cache.getObject<Plane> (id);

			// Set the plane type widget
			ui.planeTypeWidget->setText (plane.type);

			// For planes that only do self launches, set the launch method to "self
			// launch" if it is not currently set to anything else.
			if (plane.selfLaunchOnly () && idInvalid (getCurrentLaunchMethodId ()))
				ui.launchMethodInput->setCurrentItemByItemData (cache.getLaunchMethodByType (LaunchMethod::typeSelf));
		}
		catch (Cache::NotFoundException &ex)
		{
			ui.planeTypeWidget->setText ("?");
		}
	}
	else
	{
		ui.planeTypeWidget->setText ("-");
	}
}


void FlightWindow::flightModeChanged (int index)
{
	Flight::Mode flightMode=(Flight::Mode)ui.flightModeInput->itemData (index).toInt ();

	if (mode==modeCreate)
	{
		const QString departureLocation=ui.departureLocationInput->currentText ();
		const QString   landingLocation=ui.  landingLocationInput->currentText ();

		if (Flight::departsHere (flightMode))
		{
			// Departure location is local location
			if (locationEntryCanBeChanged (departureLocation))
				ui.departureLocationInput->setEditText (Settings::instance ().location);

			// Clear landing location (leaving or set automatically on landing)
			if (locationEntryCanBeChanged (landingLocation))
				ui.landingLocationInput->setEditText ("");
		}
		else
		{
			// Clear departure location (not departed here)
			if (locationEntryCanBeChanged (departureLocation))
				ui.departureLocationInput->setEditText ("");

			// Landing location will be set automatically on landing
		}
	}
}

void FlightWindow::launchMethodChanged (int index)
{
	dbId launchMethodId=(dbId)ui.launchMethodInput->itemData (index).toLongLong ();;

	if (idValid (launchMethodId))
	{
		LaunchMethod launchMethod=cache.getObject<LaunchMethod> (launchMethodId);

		if (launchMethod.isAirtow ())
		{
			QString towplaneRegistration=launchMethod.towplaneKnown () ? launchMethod.towplaneRegistration : getCurrentTowplaneRegistration ();
			dbId towplaneId=cache.getPlaneIdByRegistration (towplaneRegistration);
			if (idValid (towplaneId))
			{
				try
				{
					Plane towplane=cache.getObject<Plane> (towplaneId);
					ui.towplaneTypeWidget->setText (towplane.type);
				}
				catch (Cache::NotFoundException &ex)
				{
					ui.towplaneTypeWidget->setText ("?");
				}
			}

		}
	}
}

// Space

void FlightWindow::towplaneRegistrationChanged (const QString &text)
{
	// Find out the plane ID
	dbId id=cache.getPlaneIdByRegistration (text);
	selectedTowplane=id;

	if (idValid (id))
	{
		try
		{
			// Get the plane and set the type widget
			Plane towplane=cache.getObject<Plane> (id);

			// Set the plane type widget
			ui.towplaneTypeWidget->setText (towplane.type);
		}
		catch (Cache::NotFoundException &ex)
		{
			ui.towplaneTypeWidget->setText ("?");
		}
	}
	else
	{
		ui.planeTypeWidget->setText ("-");
	}
}

void FlightWindow::landingTimeCheckboxChanged (bool checked)
{
	bool landed=getTimeFieldActive (checked);

	if (landed)
	{
		// Landed => set landing location to local location
		if (locationEntryCanBeChanged (ui.landingLocationInput->currentText ()))
			ui.landingLocationInput->setEditText (Settings::instance ().location);

		// Set 1 landing if it was 0.
		if (getCurrentNumLandings ()==0)
			ui.numLandingsInput->setValue (1);
	}
	else
	{
		// Not landed => unset landing location input
		if (locationEntryCanBeChanged (ui.landingLocationInput->currentText ()))
			ui.landingLocationInput->setEditText ("");
	}
}


void FlightWindow::towflightLandingTimeCheckboxChanged (bool checked)
{
	bool towflightLanded=getTimeFieldActive (checked);

	if (currentTowLandsHere() && towflightLanded)
	{
		// Landed => set landing location to local location
		if (locationEntryCanBeChanged (ui.towflightLandingLocationInput->currentText ()))
			ui.towflightLandingLocationInput->setEditText (Settings::instance ().location);
	}
	else
	{
		// Not landed => unset landing location input
		if (locationEntryCanBeChanged (ui.towflightLandingLocationInput->currentText ()))
			ui.towflightLandingLocationInput->setEditText ("");
	}
}




// *******************
// ** Button events **
// *******************

void FlightWindow::okButton_clicked()
{
	// The "depart later"/"land later"/"ok" button was pressed. Check and store
	// the flight without departing it.

	try
	{
		Flight flight=determineFlight (false);
		if (writeToDatabase (flight))
			accept (); // Close the dialog
	}
	catch (AbortedException &e)
	{
		// User aborted, do nothing
	}
}

void FlightWindow::nowButton_clicked ()
{
	try
	{
		Flight flight=determineFlight (true);

		// If we are not in create mode, the date is not today or the auto
		// fields are not checked, the button is not visible at all.
		if (currentDepartsHere ())
			flight.departNow (true);
		else
			flight.landNow (true);

		if (writeToDatabase (flight))
			accept (); // Close the dialog
	}
	catch (AbortedException &e)
	{
		// User aborted, do nothing
	}
}

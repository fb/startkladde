#include "FlightListWindow.h"

#include <QKeyEvent>
#include <QPushButton>
#include <QSortFilterProxyModel>
#include <QtAlgorithms>
#include <QFileDialog>
#include <QTextCodec>

#include "src/data/Csv.h"
#include "src/model/Flight.h"
#include "src/text.h"
#include "src/concurrent/monitor/OperationCanceledException.h"
#include "src/util/qString.h"
#include "src/util/qDate.h"
#include "src/gui/dialogs.h"
#include "src/gui/windows/CsvExportDialog.h"
#include "src/gui/windows/input/DateInputDialog.h"
#include "src/model/objectList/MutableObjectList.h"
#include "src/model/flightList/FlightModel.h"
#include "src/model/objectList/ObjectListModel.h"

// TODO add different output formats

/*
 * Improvements:
 *   - Instead of a refresh action, track flight changes, either through
 *     AutomaticEntityList or by explicitly receiving DbEvent signals. This
 *     requires applying the filter to each changed flight.
 *   - Receive DbEvent signals to update person/plane changes immediately. At
 *     the moment, such changes are only updated when the window is activated,
 *     because the list view rereads the data from the cache.
 */

FlightListWindow::FlightListWindow (DbManager &manager, QWidget *parent):
	QMainWindow (parent),
	manager (manager)
{
	ui.setupUi(this);
	ui.buttonBox->button (QDialogButtonBox::Close)->setText (utf8 ("&Schließen"));

	QObject::connect (&manager, SIGNAL (stateChanged (DbManager::State)), this, SLOT (databaseStateChanged (DbManager::State)));

	// Set up the flight list and model
	flightList=new MutableObjectList<Flight> ();
	flightModel=new FlightModel (manager.getCache ());
	flightModel->setColorEnabled (false);
	flightListModel=new ObjectListModel<Flight> (flightList, true, flightModel, true, this);

	// Set up the sorting proxy model
	proxyModel=new QSortFilterProxyModel (this);
	proxyModel->setSourceModel (flightListModel);

	ui.table->setModel (proxyModel);
	ui.table->setAutoResizeRows (true);
}

FlightListWindow::~FlightListWindow ()
{
	// flightListModel is deleted by this class, which is its Qt parent.
	// flightModel and flightList are deleted by flightListModel, which owns
	// them.
}

void FlightListWindow::show (DbManager &manager, QWidget *parent)
{
	// Get the date range
	QDate first, last;
	if (DateInputDialog::editRange (&first, &last, "Datum eingeben", "Datum eingeben:", parent))
	{
		// Create the window
		FlightListWindow *window = new FlightListWindow (manager, parent);
		window->setAttribute (Qt::WA_DeleteOnClose, true);

		// Get the flights from the database
		if (window->fetchFlights (first, last))
			window->show ();
		else
			delete window;
	}
}

bool FlightListWindow::fetchFlights (const QDate &first, const QDate &last)
{
	// TODO: move this functionality to the date input dialog
	if (first>last)
		// Range reversed
		return fetchFlights (last, first);

	// Get the flights from the database
	QList<Flight> flights;
	try
	{
		flights=manager.getFlights (first, last, this);
	}
	catch (OperationCanceledException &ex)
	{
		return false;
	}

	// Sort the flights (by effective date)
	// TODO: hide the sort indicator. The functionality is already in
	// MainWindow, should probably be in SkTableView.
	qSort (flights);

	// Store the date range
	currentFirst=first;
	currentLast=last;

	// Update the list
	flightList->replaceList (flights);
	ui.table->resizeColumnsToContents ();

	// Create and set the descriptive text: "1.1.2011 bis 31.12.2011: 123 Flüge"
	int numFlights=flights.size ();
	QString dateText=toString (currentFirst, currentLast, " bis ");
	QString numFlightsText=countText (numFlights, "Flug", utf8 ("Flüge"), utf8 ("keine Flüge"));
	ui.captionLabel->setText (QString ("%1: %2").arg (dateText).arg (numFlightsText));

	return true;
}

void FlightListWindow::on_actionClose_triggered ()
{
	close ();
}

void FlightListWindow::keyPressEvent (QKeyEvent *e)
{
	// KeyEvents are accepted by default
	switch (e->key ())
	{
		case Qt::Key_Escape: ui.actionClose->trigger(); break;
		default: e->ignore (); break;
	}

	if (!e->isAccepted ()) QMainWindow::keyPressEvent (e);
}

void FlightListWindow::databaseStateChanged (DbManager::State state)
{
	if (state==DbManager::stateDisconnected)
		close ();
}

void FlightListWindow::on_actionSelectDate_triggered ()
{
	QDate newFirst=currentFirst;
	QDate newLast =currentLast ;

	if (DateInputDialog::editRange (&newFirst, &newLast, "Datum eingeben", "Datum eingeben:", this))
		fetchFlights (newFirst, newLast);
}

void FlightListWindow::on_actionRefresh_triggered ()
{
	fetchFlights (currentFirst, currentLast);
}

/**
 * Called when the user activates the "Export" action.
 */
void FlightListWindow::on_actionExport_triggered ()
{
	// Query the user for a file name
	QString fileName=QFileDialog::getSaveFileName (this,
			"Flugdatenbank exportieren", ".",
			"CSV-Dateien (*.csv);;Alle Dateien (*)");

	// Cancel if the file name was empty (probably because the user canceled)
	if (fileName.isEmpty ())
		return;

	// Query the user for CSV options (charset, separator...)
	CsvExportDialog csvExportDialog;
	csvExportDialog.setModal (true);
	int settingsResult=csvExportDialog.exec ();

	// Cancel if the user canceled
	if (settingsResult!=QDialog::Accepted)
		return;

	// Get the settings from the dialog
	const QTextCodec *codec=csvExportDialog.getSelectedCodec ();
	const QString &separator=csvExportDialog.getSeparator ();

	// Open the file
	QFile file (fileName);
	if (file.open (QIODevice::WriteOnly | QIODevice::Text))
	{
		// Opening succeeded

		// Create a CSV table from the flight list model
		Csv csv (*flightListModel, separator);

		// Convert and write the CSV
		file.write (codec->fromUnicode (csv.toString ()));

		// Close the file
		file.close ();


		// Exporting succeeded - display a message to the user
		int numFlights=flightListModel->rowCount (QModelIndex ());
		QString message=QString ("%1 exportiert").arg (countText (numFlights, "Flug", utf8 ("Flüge")));
		QMessageBox::information (this, "Flugdatenbank exportieren", message);
	}
	else
	{
		// Opening failed - display a message to the user
		QString message=QString ("Exportieren fehlgeschlagen: %1")
			.arg (file.errorString ());
		QMessageBox::critical (this, "Exportieren fehlgeschlagen", message);
	}
}

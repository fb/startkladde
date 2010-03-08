#include "OperationMonitorInterface.h"

#include <iostream>

#include <QAtomicInt>

#include "src/util/qString.h"
#include "src/concurrent/monitor/OperationMonitor.h"
#include "src/concurrent/monitor/OperationCanceledException.h"

const OperationMonitorInterface OperationMonitorInterface::null (NULL);

/**
 * Destroys the interface and decrements the reference count
 *
 * If the reference count is 1 (only the master copy is left), the end of the
 * operation is signaled to the monitor. If the reference count is 0 (the
 * master copy has been destroyed), the shared data is deleted.
 */
OperationMonitorInterface::~OperationMonitorInterface ()
{
//	std::cout << "-interface" << std::endl;

	int newRefCount=refCount->fetchAndAddOrdered (-1)-1;

	if (newRefCount==1)
	{
		if (monitor)
			monitor->setEnded ();
	}
	else if (newRefCount==0)
	{
		// There may be a race condition here if a new instance is created at
		// this point (see also #OperationMonitorInterface and operator=).
		delete refCount;
	}
}

OperationMonitorInterface::OperationMonitorInterface (const OperationMonitorInterface &other):
	monitor (other.monitor), refCount (other.refCount)
{
//	std::cout << "+interface" << std::endl;

	// FIXME: this is very similar to QFutureInterfaceBase, but isn't there a
	// race condition in case the last instance is destroyed at this point?
	other.refCount->ref ();
}


OperationMonitorInterface &OperationMonitorInterface::operator= (const OperationMonitorInterface &other)
{
//	std::cout << "=interface" << std::endl;

	if (&other==this) return *this;

	// FIXME: this is very similar to QFutureInterfaceBase, but isn't there a
	// race condition in case the last instance is destroyed at this point?
	other.refCount->ref ();

	int newRefCount=refCount->fetchAndAddOrdered (-1)-1;

	if (newRefCount==1)
		monitor->setEnded ();
	else if (newRefCount==0)
		delete refCount;

	refCount=other.refCount;
	monitor=other.monitor;

	return *this;
}

OperationMonitorInterface::OperationMonitorInterface (OperationMonitor *monitor):
	monitor (monitor), refCount (new QAtomicInt (1))
{
//	std::cout << "+interface" << std::endl;
}

/**
 * Sets the status of the operation by calling the setStatus method of the
 * monitor
 *
 * @param text the status
 * @param checkCanceled whether to call checkCanceled
 */
void OperationMonitorInterface::status (const QString &text, bool checkCanceled)
{
	if (monitor)
	{
		if (checkCanceled) this->checkCanceled ();

		monitor->setStatus (text);
	}
}

/**
 * An overloaded version of status(const QString&, bool). status is interpreted
 * as utf8.
 */
void OperationMonitorInterface::status (const char *text, bool checkCanceled)
{
	status (utf8 (text), checkCanceled);
}

/**
 * Sets the progress and the maximum progress of the operation by calling the
 * setProgress method of the monitor
 *
 * @param progress the current progress of the operation; should not be larger
 *                 than maxProgress
 * @param maxProgress the maximum progress of the operation
 * @param status the status of the operation (see #status) (optional)
 * @param checkCanceled whether to call checkCanceled
 */
void OperationMonitorInterface::progress (int progress, int maxProgress, const QString &status, bool checkCanceled)
{
	if (monitor)
	{
		if (checkCanceled) this->checkCanceled ();
		monitor->setProgress (progress, maxProgress);
		if (!status.isNull ()) monitor->setStatus (status);
	}
}

/**
 * An overloaded version of progress(int, int, const QString&, bool). status
 * is interpreted as utf8.
 */
void OperationMonitorInterface::progress (int progress, int maxProgress, const char *status, bool checkCanceled)
{
	if (status)
		this->progress (progress, maxProgress, utf8 (status), checkCanceled);
	else
		this->progress (progress, maxProgress, QString (), checkCanceled);
}

/**
 * Signals the end of the operation by calling the setEnded method of the
 * monitor
 *
 * Can be called manually, or is called automatically when only 1 reference is
 * left.
 */
void OperationMonitorInterface::ended ()
{
	if (monitor)
		monitor->setEnded ();
}

/**
 * Determines whether the operation has been canceled
 *
 * @return true if the operation has been canceled, false if not
 */
bool OperationMonitorInterface::canceled ()
{
	if (monitor)
		return monitor->isCanceled ();
	else
		return false;
}

/**
 * Throws an OperationCanceledException if the operation has been canceled
 *
 * This is also called by #progress and #status by default
 */
void OperationMonitorInterface::checkCanceled ()
{
	if (canceled ())
		throw OperationCanceledException ();
}

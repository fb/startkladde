#ifndef ACPIWIDGET_H_
#define ACPIWIDGET_H_

/**
  * wrapper class for libacpi
  * eggert.ehmke@berlin.de
  * 10.09.2008
  */

#include <QLabel>

class QTimer;

class AcpiWidget: public QLabel
{
  Q_OBJECT

  public:
  AcpiWidget (QWidget* parent);

  static bool valid ();

  private:
  QTimer* timer;

  private slots:
  void slotTimer();
};

#endif

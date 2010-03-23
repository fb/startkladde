#ifndef WEATHERWIDGET_H_
#define WEATHERWIDGET_H_

#include <QImage>
#include <QPixmap>
#include <QImageReader>

#include "src/gui/widgets/SkLabel.h"

class WeatherWidget:public SkLabel
{
	Q_OBJECT

	public:
		WeatherWidget (QWidget *parent=NULL);
		virtual bool loadImage (const QString&);
		virtual bool loadMovie (const QString&);
		virtual void setText (const QString&);

	public slots:
		virtual void inputLine (QString line);

	signals:
		void doubleClicked ();
		void sizeChanged (const QSize&);

	protected:
		virtual void mouseDoubleClickEvent (QMouseEvent *e);
		virtual void resizeEvent (QResizeEvent *);

	protected slots:
		virtual void pluginNotFound ();
};

#endif


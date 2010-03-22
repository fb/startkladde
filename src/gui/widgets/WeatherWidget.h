#ifndef WEATHERWIDGET_H_
#define WEATHERWIDGET_H_

#include <QLabel>
#include <QImage>
#include <QPixmap>
#include <QImageReader>

class WeatherWidget:public QLabel
{
	Q_OBJECT

	public:
		WeatherWidget (QWidget *parent=NULL, const char *name=NULL);
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


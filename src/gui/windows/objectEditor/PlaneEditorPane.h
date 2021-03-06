#ifndef PLANEEDITORPANE_H_
#define PLANEEDITORPANE_H_

#include "ObjectEditorPane.h"
#include "ui_PlaneEditorPane.h"

#include "src/gui/windows/objectEditor/ObjectEditorWindowBase.h" // Required for ObjectEditorWindowBase::Mode
#include "src/model/Plane.h"

class PlaneEditorPane: public ObjectEditorPane<Plane>
{
    Q_OBJECT

	public:
		PlaneEditorPane (ObjectEditorWindowBase::Mode mode, Cache &cache, QWidget *parent=NULL);
		virtual ~PlaneEditorPane();

		virtual void objectToFields (const Plane &plane);
		virtual void fieldsToObject (Plane &object);
		virtual void setNameObject (const Plane &nameObject);

	public slots:
		virtual void on_registrationInput_editingFinished ();

	protected:
		virtual void loadData ();
		virtual void prepareText ();
		virtual void setupText ();
		virtual void changeEvent (QEvent *event);

	private:
		Ui::PlaneEditorPaneClass ui;
};


#endif // PLANEEDITORPANE_H

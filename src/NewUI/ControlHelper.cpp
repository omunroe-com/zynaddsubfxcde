/*
  ZynAddSubFX - a software synthesizer

  ControlHelper.cpp - The glue between the ZynAddSubFX backend and GUI

  Copyright (C) 2010 Harald Hvaal

  This program is free software; you can redistribute it and/or modify
  it under the terms of version 2 of the GNU General Public License
  as published by the Free Software Foundation.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License (version 2 or later) for more details.

  You should have received a copy of the GNU General Public License (version 2)
  along with this program; if not, write to the Free Software Foundation,
  Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA

*/

#include "ControlHelper.h"
#include "../Controls/Control.h"
#include <QCoreApplication>
#include <QUndoCommand>
#include <QUndoStack>
#include <QMessageBox>
#include <QWidget>
#include <QDynamicPropertyChangeEvent>
#include <QStack>
#include <QVariant>
#include <QtDebug>
#include "Menu.h"

QUndoStack * ControlHelper::m_undoStack = NULL;

ControlHelper::ControlHelper(QObject *parent)
    :QObject(parent),
      m_control(NULL),
      m_oldIntValue(0)
{
    //attach a menu to the parent object if it is a QWidget
    if (QWidget* widget = qobject_cast<QWidget*>(parent)) {
        new Menu(widget, this);
    }
}

ControlHelper::~ControlHelper()
{
    if (m_control)
        m_control->removeRedirections(this);
}

void ControlHelper::setGlobalUndoStack(class QUndoStack *stack)
{
    m_undoStack = stack;
}

void ControlHelper::handleEvent(Event *event)
{
    //will be called from the engine/backend thread, so be careful (threadwise)
    //and efficient about all processing in this function

    if(event->type() == Event::NewValueEvent) {

        //sanity check
        if (static_cast<NewValueEvent *>(event)->control != m_control) {
            qDebug() << "ControlHelper: Weird, events from a control we don't know?";
            return;
        }
        newValueEvent(static_cast<NewValueEvent *>(event));
    }
    else
    if(event->type() == Event::RemovalEvent) {
        //clear the current connected control
        disconnect();
    }
    else
    if(event->type() == Event::OptionsChangedEvent)
        emitOptions();
    else
    if(event->type() == Event::ConnEvent)
        connectedEvent(static_cast<ConnEvent *>(event));
}

typedef std::vector<std::string> StrVec;
void ControlHelper::connectedEvent(ConnEvent *ev)
{
    puts("Connection Recieved");
    m_control = ev->control;

    if (QWidget *widget = qobject_cast<QWidget*>(parent())) {
        widget->setToolTip(QString::fromStdString(m_control->getId()));
    }

    emit valueChanged(ev->dat.i);

    if(ev->data) {
        const StrVec &vec = *static_cast<const StrVec *>(ev->data);

        QStringList options;
        for(unsigned i=0; i<vec.size(); ++i)
            options << QString::fromStdString(vec[i]);
        emit optionsChanged(options);
    }

    emit connected(ev->control);
}

void ControlHelper::disconnectedEvent()
{
    emit disconnected();
}

void ControlHelper::newValueEvent(NewValueEvent *event)
{
    m_oldIntValue = event->value;
    emit valueChanged(event->value);
}

void ControlHelper::setControl(QString absoluteId)
{
    if (m_control) m_control->removeRedirections(this);
    disconnect();

    if(absoluteId.isEmpty())
        return;

    //We let the job system connect us
    Node::requestConnect(absoluteId.toStdString(),this);

#if 0
    Node::lock();
    Node *node = Node::get(absoluteId.toStdString());
    m_control = dynamic_cast<GenControl *>(node);
    if(m_control) {
        m_control->addRedirection(this);

        emitOptions();
        qDebug() << "Assigning" << this << "to" << absoluteId;
        connectedEvent();
        m_control->queueGetInt();
    } else {
        qDebug() << "Could not find a control named " << absoluteId;
    }

    Node::unlock();
#endif
}

void ControlHelper::disconnect()
{
    if(m_control) {
        m_control = NULL;
    }
    disconnectedEvent();
}

class ControlChange : public QUndoCommand, public NodeUser
{
    private:
        int m_newValue, m_oldValue, m_id;
        GenControl *m_control;

    public:

        ControlChange(GenControl *control, int newValue, int oldValue) : 
            QUndoCommand(QString::fromStdString(control->getAbsoluteId())),
            m_newValue(newValue),
            m_oldValue(oldValue),
            m_id(control->getUid()),
            m_control(control)
        {
#warning This might very well break the undo system
            //m_control->addRedirection(this, new TypeFilter(Event::RemovalEvent));
        }

        void handleEvent(Event *event)
        {
            if(event->type() == Event::RemovalEvent) {
                //clear the current connected control
                m_control = NULL;
            }
        }

        virtual ~ControlChange()
        {
            if (m_control) m_control->removeRedirections(this);
        }

        bool mergeWith(const QUndoCommand* command)
        {
            if (command->id() != id())
                return false;
            m_newValue = static_cast<const ControlChange*>(command)->m_newValue;
            return true;
        }

        int id() const
        {
            return m_id;
        }

        void redo()
        {
            if (m_control) m_control->queueSetInt(m_newValue);
        }

        void undo()
        {
            if (m_control) m_control->queueSetInt(m_oldValue);
        }

};

void ControlHelper::setValue(int value)
{
    if (m_undoStack && m_control) {
        m_undoStack->push(new ControlChange(m_control, value, m_oldIntValue));
        m_oldIntValue = value;
    }

#if 0
    if (m_control) {
        m_control->queueSetInt(value);
    }
#endif

}

void ControlHelper::setValue(bool value)
{
    setValue(int(127 * value));
}

QString ControlHelper::getControlId()
{
    if(m_control)
        return QString::fromStdString(m_control->getAbsoluteId());
    else
        return "Undefined";
}

void ControlHelper::MIDILearn()
{
    if(m_control) {
        if (QMessageBox::information(qobject_cast<QWidget*>(parent()), QString(),
                "When you press OK, ZynAddSubFX listen for MIDI events. Once it has determined what"
                "controller should be used, it will be connected to this control.",
                QMessageBox::Ok | QMessageBox::Cancel) ==
                QMessageBox::Ok) {
            //TODO: handle result
            m_control->MIDILearn();
        }
    }
}

void ControlHelper::trigger()
{
    setValue(127);
}

void ControlHelper::updateControlId()
{
    QString fullid = findComponentPath(parent());
    if(fullid.isEmpty())
        return;
    setControl(fullid);
}

QString ControlHelper::findComponentPath(QObject *object)
{
    if (!object) return QString();

    QString fullid = object->property("absoluteControlId").toString();

    if(!fullid.isEmpty())
        //alright, we already have what we wanted
        return fullid;

    QObject *p = object;
    fullid = p->property("controlId").toString();
    if(fullid.isEmpty())
        //the object has no controlId set, so theres no reason to recurse any further for what its
        //full path would be.
        return QString();

    while(true) {
        p = p->parent();
        if(!p)
            //we've reached the end of the hierarchy without finding any absolute id. bail out
            return QString();

        QString id    = p->property("controlId").toString();
        QString absid = p->property("absoluteControlId").toString();

        if(!id.isEmpty())
            //just append the relative id and continue recursion
            fullid.prepend(id + ".");
        else
        if(!absid.isEmpty()) {
            //this appears to be the absolute id we've been looking for.
            fullid.prepend(absid + ".");
            return fullid;
        }
    }
    return QString();
}

void ControlHelper::emitOptions()
{
    QStringList options;
    if(m_control && m_control->numOptions())
        for(int i = 0; i < m_control->numOptions(); ++i)
            options << QString::fromStdString(m_control->getOption(i));
    //qDebug() << "Emitting options " << options;
    emit optionsChanged(options);
}

void ControlHelper::defaults()
{
    if (m_control) {
        m_control->defaults();
    }
}

void ControlHelper::debugPrint()
{
    qDebug() << "parent has controlId: " << parent()->property("controlId").toString() <<
        " and absoluteControlId: " << parent()->property("absoluteControlId").toString();
    if (m_control) {
        qDebug() << "Currently connected to " << QString::fromStdString(m_control->getAbsoluteId());
        qDebug() << "Current value is " << int(m_control->getInt());
    }
    else {
        qDebug() << "Currently not connected.";
    }
}

#include "ControlHelper.moc"


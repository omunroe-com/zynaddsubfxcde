/*
  ZynAddSubFX - a software synthesizer

  Control.h - Control base class
  Copyright (C) 2009-2009 Mark McCurry
  Author: Mark McCurry

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
#ifndef CONTROL_H
#define CONTROL_H

#include <string>
#include <iostream>
#include "GenControl.h"
#include "EventClasses.h"
#include "JobClasses.h"
#include <pthread.h>
#include "../Misc/XMLwrapper.h"

//these need explicit xml-names
#define GETPAR(X,Y) (X).getpar(xml, (Y));
#define ADDPAR(X,Y) (X).addpar(xml, (Y));

//these just use the names of the controls
#define GET(X) (X).getpar(xml, (X).getId())
#define ADD(X) (X).addpar(xml, (X).getId())

/**
 * A control of interal type T
 *
 */

enum ControlOptions {
    NoXml = 1,
    NoXmlIfDefault = 2
};

template<class T>
class Control:public GenControl
{
    friend class RequestGetIntJob;
    friend class RequestSetIntJob;

    public:

        Control(Node *parent, std::string id, T defaultval);
        virtual ~Control();

        T operator()() const;
        virtual std::string getString() const {return
                                                   "hm, this should get implemented, but not yet :p";}

        void setValue(const T &val);
        T getValue() const;

        virtual void setInt(int val);
        virtual int getInt() const;

        void queueGetInt();
        void queueSetInt(int value);

        void defaults();
        //note: default value should only be changed during initialization
        //because it will not lock the control (ie. not threadsafe)
        void setDefaults( const T &val);

        //will store the current value as defaults
        void storeDefaults();

        T getMin() const { return min; }
        T getMax() const { return max; }

        void setOptions(ControlOptions opt, bool enable = true);

        virtual void saveXml(class XMLwrapper *xml);

        virtual void addpar(class XMLwrapper *xml, const std::string& name);
        virtual void getpar(class XMLwrapper *xml, const std::string& name);

    protected:
        int options;
        T min, max;

    private:
        T value;
        T defaultval;
};

#include "Control.cpp"

#endif


/*
  ZynAddSubFX - a software synthesizer

  Echo.C - Echo effect
  Copyright (C) 2002-2005 Nasca Octavian Paul
  Author: Nasca Octavian Paul

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

#include <cmath>
#include <iostream>
#include "Echo.h"
#include "../Misc/LinInjFunc.h"
#include <string> //temporary

//temporary (NOTE: this is broken, but it is being used temporarially)
//just in case this is overlooked:
//DO NOT LET THIS INTO PRODUCTION LEVEL CODE!!!!
class TmpFunc:public InjFunction<char, REALTYPE>
{
    public:

        inline REALTYPE operator()(const char &x) const {return pow(2,
                                                                    (fabs(x
                                                                          -
                                                                          64.0) / 64.0 * 9) - 1.0) / 1000.0 * (x < 64 ? -1 : 1); }
        inline char operator()(const REALTYPE &x) const {
            if(x < 0)
                return char(-1 * log(1000 * x + 1) * 64.0 / log(2.0) / 9 + 64);
            return char(log(1000 * x + 1) * 64.0 / log(2.0) / 9 + 64);
        }
};

typedef LinInjFunc<REALTYPE> Linear;
Echo::Echo(const int &insertion_,
           REALTYPE *const efxoutl_,
           REALTYPE *const efxoutr_)
    :Effect(insertion_, efxoutl_, efxoutr_, NULL, 0),
      Pvolume(50),  //Ppanning(64),
      /**Note: random panning is not working here \todo match old panning exactly*/
      panning(NULL, "panning", 0.5, new Linear(0, 1.0)),
      delay(NULL, "delay", 0.70866, new Linear(0, 1.5)),
      lrdelay(NULL, "L/R delay", 0.01670838, new TmpFunc()),
      lrcross(NULL, "L/R crossover", 0.5, new Linear(0.0, 1.0)),
      fb(NULL, "Feedback", 0.313, new Linear(0, 0.9922)),
      hidamp(NULL, "Dampening", 0.528, new Linear(1.0, 0)),
      //Pdelay(60),
      //Plrdelay(100),Plrcross(100),Pfb(40),Phidamp(60),
      //lrdelay(0),
      delaySample(1), old(0.0)
{
    delay.addRedirection(this);
    lrdelay.addRedirection(this);
    //setpreset(Ppreset);
    //cleanup();
    initdelays();
}

Echo::~Echo() {}

/*
 * Cleanup the effect
 */
void Echo::cleanup()
{
    delaySample.l().clear();
    delaySample.r().clear();
    old = Stereo<REALTYPE>(0.0);
}


/*
 * Initialize the delays
 */
void Echo::initdelays()
{
    /**\todo make this adjust insted of destroy old delays*/
    kl = 0;
    kr = 0;
    dl = (int)(1 + (delay() - lrdelay()) * SAMPLE_RATE); //check validity
    if(dl < 1)
        dl = 1;
    dr = (int)(1 + (delay() + lrdelay()) * SAMPLE_RATE); //check validity
    if(dr < 1)
        dr = 1;

    delaySample.l() = AuSample(dl);
    delaySample.r() = AuSample(dr);

    old = Stereo<REALTYPE>(0.0);
}

/*
 * Effect output
 */
void Echo::out(REALTYPE *const smpsl, REALTYPE *const smpsr)
{
    Stereo<AuSample> input(AuSample(SOUND_BUFFER_SIZE, smpsl), AuSample(
                               SOUND_BUFFER_SIZE,
                               smpsr));
    out(input);
}

void Echo::out(const Stereo<AuSample> &input)
{
//void Echo::out(const Stereo<AuSample> & input){ //ideal
    REALTYPE l, r, ldl, rdl; /**\todo move l+r->? ldl+rdl->?*/

    for(int i = 0; i < input.l().size(); i++) {
        ldl = delaySample.l()[kl];
        rdl = delaySample.r()[kr];
        l   = ldl * (1.0 - lrcross()) + rdl *lrcross();
        r   = rdl * (1.0 - lrcross()) + ldl *lrcross();
        ldl = l;
        rdl = r;

        efxoutl[i] = ldl * 2.0;
        efxoutr[i] = rdl * 2.0;


        ldl = input.l()[i] * panning() - ldl *fb();
        rdl = input.r()[i] * (1.0 - panning()) - rdl *fb();

        //LowPass Filter
        delaySample.l()[kl] = ldl = ldl * hidamp() + old.l() * (1.0 - hidamp());
        delaySample.r()[kr] = rdl = rdl * hidamp() + old.r() * (1.0 - hidamp());
        old.l() = ldl;
        old.r() = rdl;

        if(++kl >= dl)
            kl = 0;
        if(++kr >= dr)
            kr = 0;
    }
}


/*
 * Parameter control
 */
void Echo::setvolume(char Pvolume)
{
    this->Pvolume = Pvolume;

    if(insertion == 0) {
        outvolume = pow(0.01, (1.0 - Pvolume / 127.0)) * 4.0;
        volume    = 1.0;
    }
    else
        volume = outvolume = Pvolume / 127.0;
    ;
    if(Pvolume == 0)
        cleanup();
}

//void Echo::setpanning(char Ppanning)
//{
//this->Ppanning=Ppanning;
//panning = (Ppanning+0.5)/127.0;
//}

//void Echo::setdelay(char Pdelay)
//{
//    delay.setValue(Pdelay);
//this->Pdelay=Pdelay;
//delay=1+(int)(Pdelay/127.0*SAMPLE_RATE*1.5);//0 .. 1.5 sec
//    initdelays();
//}

//void Echo::setlrdelay(char Plrdelay)
//{
//    REALTYPE tmp;
//    this->Plrdelay=Plrdelay;
//    tmp=(pow(2,fabs(Plrdelay-64.0)/64.0*9)-1.0)/1000.0*SAMPLE_RATE;
//    if (Plrdelay<64.0) tmp=-tmp;
//    lrdelay=(int) tmp;
//    initdelays();
//}

//void Echo::setlrcross(char Plrcross)
//{
//this->Plrcross=Plrcross;
//lrcross=Plrcross/127.0*1.0;
//}

//void Echo::setfb(char Pfb)
//{
//this->Pfb=Pfb;
//fb=Pfb/128.0;
//}

//void Echo::sethidamp(char Phidamp)
//{
//this->Phidamp=Phidamp;
//hidamp=1.0-Phidamp/127.0;
//}

void Echo::setpreset(unsigned char npreset)
{
    /**\todo see if the preset array can be replaced with a struct or a class*/
    const int     PRESET_SIZE = 7;
    const int     NUM_PRESETS = 9;
    unsigned char presets[NUM_PRESETS][PRESET_SIZE] = {
        //Echo 1
        {67, 64, 35,  64,   30,    59,     0         },
        //Echo 2
        {67, 64, 21,  64,   30,    59,     0         },
        //Echo 3
        {67, 75, 60,  64,   30,    59,     10        },
        //Simple Echo
        {67, 60, 44,  64,   30,    0,      0         },
        //Canyon
        {67, 60, 102, 50,   30,    82,     48        },
        //Panning Echo 1
        {67, 64, 44,  17,   0,     82,     24        },
        //Panning Echo 2
        {81, 60, 46,  118,  100,   68,     18        },
        //Panning Echo 3
        {81, 60, 26,  100,  127,   67,     36        },
        //Feedback Echo
        {62, 64, 28,  64,   100,   90,     55        }
    };


    if(npreset >= NUM_PRESETS)
        npreset = NUM_PRESETS - 1;
    for(int n = 0; n < PRESET_SIZE; n++)
        changepar(n, presets[npreset][n]);
    if(insertion)
        setvolume(presets[npreset][0] / 2);         //lower the volume if this is insertion effect
    Ppreset = npreset;
}


void Echo::changepar(const int &npar, const unsigned char &value)
{
    char tmp = value; //hack until removal of unsigned
    switch(npar) {
    case 0:
        setvolume(value);
        break;
    case 1:
        panning.setValue(tmp);
        break;
    case 2:
        delay.setValue(tmp);
        break;
    case 3:
        lrdelay.setValue(tmp);
        break;
    case 4:
        lrcross.setValue(tmp);
        break;
    case 5:
        fb.setValue(tmp);
        break;
    case 6:
        hidamp.setValue(tmp);
        break;
    }
}

unsigned char Echo::getpar(const int &npar) const
{
    switch(npar) {
    case 0:
        return Pvolume;
        break;
    case 1:
        return panning.getValue();
        break;
    case 2:
        return delay.getValue();
        break;
    case 3:
        return lrdelay.getValue();
        break;
    case 4:
        return lrcross.getValue();
        break;
    case 5:
        return fb.getValue();
        break;
    case 6:
        return hidamp.getValue();
        break;
    }
    return 0; // in case of bogus parameter number
}

void Echo::handleEvent(Event *ev)
{
    if(ev->type() == Event::UpdateEvent)
        initdelays();
}


/*
  ZynAddSubFX - a software synthesizer

  Master.cpp - It sends Midi Messages to Parts, receives samples from parts,
             process them with system/insertion effects and mix them
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

#warning TODO move Sequencer out of master

#include "Master.h"

#include "../Params/LFOParams.h"
#include "../Effects/EffectMgr.h"

#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <iostream>
#include <algorithm>

#include <unistd.h>

using namespace std;

Master::Master()
{
    swaplr = 0;

    pthread_mutex_init(&mutex, NULL);
    pthread_mutex_init(&vumutex, NULL);
    fft = new FFTwrapper(OSCIL_SIZE);

    shutup     = 0;
    for(int npart = 0; npart < NUM_MIDI_PARTS; npart++) {
        vuoutpeakpart[npart] = 1e-9;
        fakepeakpart[npart]  = 0;
    }

    for(int npart = 0; npart < NUM_MIDI_PARTS; npart++)
        part[npart] = new Part(&microtonal, fft, &mutex, npart);

    //Insertion Effects init
    for(int nefx = 0; nefx < NUM_INS_EFX; nefx++)
        insefx[nefx] = new EffectMgr(1, &mutex);

    //System Effects init
    for(int nefx = 0; nefx < NUM_SYS_EFX; nefx++)
        sysefx[nefx] = new EffectMgr(0, &mutex);


    defaults();
}

void Master::defaults()
{
    volume = 1.0;
    setPvolume(80);
    setPkeyshift(64);

    for(int npart = 0; npart < NUM_MIDI_PARTS; npart++) {
        part[npart]->defaults();
        part[npart]->Prcvchn = npart % NUM_MIDI_CHANNELS;
    }

    partonoff(0, 1); //enable the first part

    for(int nefx = 0; nefx < NUM_INS_EFX; nefx++) {
        insefx[nefx]->defaults();
        Pinsparts[nefx] = -1;
    }

    //System Effects init
    for(int nefx = 0; nefx < NUM_SYS_EFX; nefx++) {
        sysefx[nefx]->defaults();
        for(int npart = 0; npart < NUM_MIDI_PARTS; npart++)
            setPsysefxvol(npart, nefx, 0);

        for(int nefxto = 0; nefxto < NUM_SYS_EFX; nefxto++)
            setPsysefxsend(nefx, nefxto, 0);
    }

    microtonal.defaults();
    ShutUp();
}

bool Master::mutexLock(lockset request)
{
    switch (request)
    {
        case MUTEX_TRYLOCK:
            return !pthread_mutex_trylock(&mutex);
        case MUTEX_LOCK:
            return !pthread_mutex_lock(&mutex);
        case MUTEX_UNLOCK:
            return !pthread_mutex_unlock(&mutex);
    }
    return false;
}

Master &Master::getInstance()
{
    static Master instance;
    return instance;
}

/*
 * Note On Messages (velocity=0 for NoteOff)
 */
void Master::noteOn(char chan, char note, char velocity)
{
    if(velocity) {
        for(int npart = 0; npart < NUM_MIDI_PARTS; npart++) {
            if(chan == part[npart]->Prcvchn) {
                fakepeakpart[npart] = velocity * 2;
                if(part[npart]->Penabled)
                    part[npart]->NoteOn(note, velocity, keyshift);
            }
        }
    }
    else
        this->noteOff(chan, note);
    HDDRecorder.triggernow();
}

/*
 * Note Off Messages
 */
void Master::noteOff(char chan, char note)
{
    for(int npart = 0; npart < NUM_MIDI_PARTS; npart++)
        if((chan == part[npart]->Prcvchn) && part[npart]->Penabled)
            part[npart]->NoteOff(note);
}

/*
 * Controllers
 */
void Master::setController(char chan, int type, int par)
{
    if((type == C_dataentryhi) || (type == C_dataentrylo)
       || (type == C_nrpnhi) || (type == C_nrpnlo)) { //Process RPN and NRPN by the Master (ignore the chan)
        ctl.setparameternumber(type, par);

        int parhi = -1, parlo = -1, valhi = -1, vallo = -1;
        if(ctl.getnrpn(&parhi, &parlo, &valhi, &vallo) == 0) //this is NRPN
            //fprintf(stderr,"rcv. NRPN: %d %d %d %d\n",parhi,parlo,valhi,vallo);
            switch(parhi) {
            case 0x04: //System Effects
                if(parlo < NUM_SYS_EFX)
                    sysefx[parlo]->seteffectpar_nolock(valhi, vallo);
                ;
                break;
            case 0x08: //Insertion Effects
                if(parlo < NUM_INS_EFX)
                    insefx[parlo]->seteffectpar_nolock(valhi, vallo);
                ;
                break;
            }
        ;
    }
    else {  //other controllers
        for(int npart = 0; npart < NUM_MIDI_PARTS; npart++) //Send the controller to all part assigned to the channel
            if((chan == part[npart]->Prcvchn) && (part[npart]->Penabled != 0))
                part[npart]->SetController(type, par);
        ;

        if(type == C_allsoundsoff) { //cleanup insertion/system FX
            for(int nefx = 0; nefx < NUM_SYS_EFX; ++nefx)
                sysefx[nefx]->cleanup();
            for(int nefx = 0; nefx < NUM_INS_EFX; ++nefx)
                insefx[nefx]->cleanup();
        }
    }
}

void Master::vuUpdate(const REALTYPE *outl, const REALTYPE *outr)
{
    //Peak computation (for vumeters)
    vu.outpeakl = 1e-12;
    vu.outpeakr = 1e-12;
    for(int i = 0; i < SOUND_BUFFER_SIZE; i++) {
        if(fabs(outl[i]) > vu.outpeakl)
            vu.outpeakl = fabs(outl[i]);
        if(fabs(outr[i]) > vu.outpeakr)
            vu.outpeakr = fabs(outr[i]);
    }
    if((vu.outpeakl > 1.0) || (vu.outpeakr > 1.0))
        vu.clipped = 1;
    if(vu.maxoutpeakl < vu.outpeakl)
        vu.maxoutpeakl = vu.outpeakl;
    if(vu.maxoutpeakr < vu.outpeakr)
        vu.maxoutpeakr = vu.outpeakr;

    //RMS Peak computation (for vumeters)
    vu.rmspeakl = 1e-12;
    vu.rmspeakr = 1e-12;
    for(int i = 0; i < SOUND_BUFFER_SIZE; i++) {
        vu.rmspeakl += outl[i] * outl[i];
        vu.rmspeakr += outr[i] * outr[i];
    }
    vu.rmspeakl = sqrt(vu.rmspeakl / SOUND_BUFFER_SIZE);
    vu.rmspeakr = sqrt(vu.rmspeakr / SOUND_BUFFER_SIZE);

    //Part Peak computation (for Part vumeters or fake part vumeters)
    for(int npart = 0; npart < NUM_MIDI_PARTS; npart++) {
        vuoutpeakpart[npart] = 1.0e-12;
        if(part[npart]->Penabled != 0) {
            REALTYPE *outl = part[npart]->partoutl,
                     *outr = part[npart]->partoutr;
            for(int i = 0; i < SOUND_BUFFER_SIZE; i++) {
                REALTYPE tmp = fabs(outl[i] + outr[i]);
                if(tmp > vuoutpeakpart[npart])
                    vuoutpeakpart[npart] = tmp;
            }
            vuoutpeakpart[npart] *= volume;
        }
        else
            if(fakepeakpart[npart] > 1)
                fakepeakpart[npart]--;
    }
}

/*
 * Enable/Disable a part
 */
void Master::partonoff(int npart, int what)
{
    if(npart >= NUM_MIDI_PARTS)
        return;
    if(what == 0) { //disable part
        fakepeakpart[npart]   = 0;
        part[npart]->Penabled = 0;
        part[npart]->cleanup();
        for(int nefx = 0; nefx < NUM_INS_EFX; nefx++) {
            if(Pinsparts[nefx] == npart)
                insefx[nefx]->cleanup();

        }
    }
    else {  //enabled
        part[npart]->Penabled = 1;
        fakepeakpart[npart]   = 0;
    }
}

/*
 * Master audio out (the final sound)
 */
void Master::AudioOut(REALTYPE *outl, REALTYPE *outr)
{
    //Swaps the Left channel with Right Channel
    if(swaplr)
        swap(outl,outr);

    //clean up the output samples (should not be needed?)
    memset(outl, 0, sizeof(REALTYPE) * SOUND_BUFFER_SIZE);
    memset(outr, 0, sizeof(REALTYPE) * SOUND_BUFFER_SIZE);

    //Compute part samples and store them part[npart]->partoutl,partoutr
    for(int npart = 0; npart < NUM_MIDI_PARTS; npart++)
        if(part[npart]->Penabled != 0)
            part[npart]->ComputePartSmps();

    //Insertion effects
    for(int nefx = 0; nefx < NUM_INS_EFX; nefx++) {
        if(Pinsparts[nefx] >= 0) {
            int efxpart = Pinsparts[nefx];
            if(part[efxpart]->Penabled)
                insefx[nefx]->out(part[efxpart]->partoutl,
                                  part[efxpart]->partoutr);
        }
    }


    //Apply the part volumes and pannings (after insertion effects)
    for(int npart = 0; npart < NUM_MIDI_PARTS; npart++) {
        if(part[npart]->Penabled == 0)
            continue;

        Stereo<REALTYPE> newvol(part[npart]->volume),
                         oldvol(part[npart]->oldvolumel,
                                part[npart]->oldvolumer);

        REALTYPE pan      = part[npart]->panning;
        if(pan < 0.5)
            newvol.l *= pan * 2.0;
        else
            newvol.r *= (1.0 - pan) * 2.0;

        //the volume or the panning has changed and needs interpolation
        if(ABOVE_AMPLITUDE_THRESHOLD(oldvol.l, newvol.l)
           || ABOVE_AMPLITUDE_THRESHOLD(oldvol.r, newvol.r)) {
            for(int i = 0; i < SOUND_BUFFER_SIZE; i++) {
                Stereo<REALTYPE> vol(INTERPOLATE_AMPLITUDE(oldvol.l, newvol.l,
                                                       i, SOUND_BUFFER_SIZE),
                                     INTERPOLATE_AMPLITUDE(oldvol.r, newvol.r,
                                                       i, SOUND_BUFFER_SIZE));
                part[npart]->partoutl[i] *= vol.l;
                part[npart]->partoutr[i] *= vol.r;
            }
            part[npart]->oldvolumel = newvol.l;
            part[npart]->oldvolumer = newvol.r;
        }
        else {
            for(int i = 0; i < SOUND_BUFFER_SIZE; i++) { //the volume did not changed
                part[npart]->partoutl[i] *= newvol.l;
                part[npart]->partoutr[i] *= newvol.r;
            }
        }
    }


    //System effects
    for(int nefx = 0; nefx < NUM_SYS_EFX; nefx++) {
        if(sysefx[nefx]->geteffect() == 0)
            continue; //the effect is disabled

        REALTYPE *tmpmixl = getTmpBuffer();
        REALTYPE *tmpmixr = getTmpBuffer();
        //Clean up the samples used by the system effects
        memset(tmpmixl, 0, sizeof(REALTYPE) * SOUND_BUFFER_SIZE);
        memset(tmpmixr, 0, sizeof(REALTYPE) * SOUND_BUFFER_SIZE);

        //Mix the channels according to the part settings about System Effect
        for(int npart = 0; npart < NUM_MIDI_PARTS; npart++) {
            //skip if the part has no output to effect
            if(Psysefxvol[nefx][npart] == 0)
                continue;

            //skip if the part is disabled
            if(part[npart]->Penabled == 0)
                continue;

            //the output volume of each part to system effect
            const REALTYPE vol = sysefxvol[nefx][npart];
            for(int i = 0; i < SOUND_BUFFER_SIZE; i++) {
                tmpmixl[i] += part[npart]->partoutl[i] * vol;
                tmpmixr[i] += part[npart]->partoutr[i] * vol;
            }
        }

        // system effect send to next ones
        for(int nefxfrom = 0; nefxfrom < nefx; nefxfrom++) {
            if(Psysefxsend[nefxfrom][nefx] != 0) {
                const REALTYPE vol = sysefxsend[nefxfrom][nefx];
                for(int i = 0; i < SOUND_BUFFER_SIZE; i++) {
                    tmpmixl[i] += sysefx[nefxfrom]->efxoutl[i] * vol;
                    tmpmixr[i] += sysefx[nefxfrom]->efxoutr[i] * vol;
                }
            }
        }

        sysefx[nefx]->out(tmpmixl, tmpmixr);

        //Add the System Effect to sound output
        const REALTYPE outvol = sysefx[nefx]->sysefxgetvolume();
        for(int i = 0; i < SOUND_BUFFER_SIZE; i++) {
            outl[i] += tmpmixl[i] * outvol;
            outr[i] += tmpmixr[i] * outvol;
        }

        returnTmpBuffer(tmpmixl);
        returnTmpBuffer(tmpmixr);
    }

    //Mix all parts
    for(int npart = 0; npart < NUM_MIDI_PARTS; npart++) {
        if(part[npart]->Penabled) { //only mix active parts
            for(int i = 0; i < SOUND_BUFFER_SIZE; i++) { //the volume did not changed
                outl[i] += part[npart]->partoutl[i];
                outr[i] += part[npart]->partoutr[i];
            }
        }
    }

    //Insertion effects for Master Out
    for(int nefx = 0; nefx < NUM_INS_EFX; nefx++)
        if(Pinsparts[nefx] == -2)
            insefx[nefx]->out(outl, outr);


    //Master Volume
    for(int i = 0; i < SOUND_BUFFER_SIZE; i++) {
        outl[i] *= volume;
        outr[i] *= volume;
    }

    if(!pthread_mutex_trylock(&vumutex)) {
        vuUpdate(outl, outr);
        pthread_mutex_unlock(&vumutex);
    }

    //Shutup if it is asked (with fade-out)
    if(shutup) {
        for(int i = 0; i < SOUND_BUFFER_SIZE; i++) {
            REALTYPE tmp =
                (SOUND_BUFFER_SIZE - i) / (REALTYPE) SOUND_BUFFER_SIZE;
            outl[i] *= tmp;
            outr[i] *= tmp;
        }
        ShutUp();
    }

    //update the LFO's time
    LFOParams::time++;

    dump.inctick();
}

Master::~Master()
{
    for(int npart = 0; npart < NUM_MIDI_PARTS; npart++)
        delete part[npart];
    for(int nefx = 0; nefx < NUM_INS_EFX; nefx++)
        delete insefx[nefx];
    for(int nefx = 0; nefx < NUM_SYS_EFX; nefx++)
        delete sysefx[nefx];

    delete fft;
    FFT_cleanup();

    pthread_mutex_destroy(&mutex);
    pthread_mutex_destroy(&vumutex);
}


/*
 * Parameter control
 */
void Master::setPvolume(char Pvolume_)
{
    Pvolume = Pvolume_;
    volume  = dB2rap((Pvolume - 96.0) / 96.0 * 40.0);
}

void Master::setPkeyshift(char Pkeyshift_)
{
    Pkeyshift = Pkeyshift_;
    keyshift  = (int)Pkeyshift - 64;
}


void Master::setPsysefxvol(int Ppart, int Pefx, char Pvol)
{
    Psysefxvol[Pefx][Ppart] = Pvol;
    sysefxvol[Pefx][Ppart]  = pow(0.1, (1.0 - Pvol / 96.0) * 2.0);
}

void Master::setPsysefxsend(int Pefxfrom, int Pefxto, char Pvol)
{
    Psysefxsend[Pefxfrom][Pefxto] = Pvol;
    sysefxsend[Pefxfrom][Pefxto]  = pow(0.1, (1.0 - Pvol / 96.0) * 2.0);
}


/*
 * Panic! (Clean up all parts and effects)
 */
void Master::ShutUp()
{
    for(int npart = 0; npart < NUM_MIDI_PARTS; npart++) {
        part[npart]->cleanup();
        fakepeakpart[npart] = 0;
    }
    for(int nefx = 0; nefx < NUM_INS_EFX; nefx++)
        insefx[nefx]->cleanup();
    for(int nefx = 0; nefx < NUM_SYS_EFX; nefx++)
        sysefx[nefx]->cleanup();
    vuresetpeaks();
    shutup = 0;
}


/*
 * Reset peaks and clear the "cliped" flag (for VU-meter)
 */
void Master::vuresetpeaks()
{
    pthread_mutex_lock(&vumutex);
    vu.outpeakl    = 1e-9;
    vu.outpeakr    = 1e-9;
    vu.maxoutpeakl = 1e-9;
    vu.maxoutpeakr = 1e-9;
    vu.clipped     = 0;
    pthread_mutex_unlock(&vumutex);
}

vuData Master::getVuData()
{
    vuData tmp;
    pthread_mutex_lock(&vumutex);
    tmp = vu;
    pthread_mutex_unlock(&vumutex);
    return tmp;
}

void Master::applyparameters(bool lockmutex)
{
    for(int npart = 0; npart < NUM_MIDI_PARTS; npart++)
        part[npart]->applyparameters(lockmutex);
}

void Master::add2XML(XMLwrapper *xml)
{
    xml->addpar("volume", Pvolume);
    xml->addpar("key_shift", Pkeyshift);
    xml->addparbool("nrpn_receive", ctl.NRPN.receive);

    xml->beginbranch("MICROTONAL");
    microtonal.add2XML(xml);
    xml->endbranch();

    for(int npart = 0; npart < NUM_MIDI_PARTS; npart++) {
        xml->beginbranch("PART", npart);
        part[npart]->add2XML(xml);
        xml->endbranch();
    }

    xml->beginbranch("SYSTEM_EFFECTS");
    for(int nefx = 0; nefx < NUM_SYS_EFX; nefx++) {
        xml->beginbranch("SYSTEM_EFFECT", nefx);
        xml->beginbranch("EFFECT");
        sysefx[nefx]->add2XML(xml);
        xml->endbranch();

        for(int pefx = 0; pefx < NUM_MIDI_PARTS; pefx++) {
            xml->beginbranch("VOLUME", pefx);
            xml->addpar("vol", Psysefxvol[nefx][pefx]);
            xml->endbranch();
        }

        for(int tonefx = nefx + 1; tonefx < NUM_SYS_EFX; tonefx++) {
            xml->beginbranch("SENDTO", tonefx);
            xml->addpar("send_vol", Psysefxsend[nefx][tonefx]);
            xml->endbranch();
        }


        xml->endbranch();
    }
    xml->endbranch();

    xml->beginbranch("INSERTION_EFFECTS");
    for(int nefx = 0; nefx < NUM_INS_EFX; nefx++) {
        xml->beginbranch("INSERTION_EFFECT", nefx);
        xml->addpar("part", Pinsparts[nefx]);

        xml->beginbranch("EFFECT");
        insefx[nefx]->add2XML(xml);
        xml->endbranch();
        xml->endbranch();
    }

    xml->endbranch();
}


int Master::getalldata(char **data)
{
    XMLwrapper *xml = new XMLwrapper();

    xml->beginbranch("MASTER");

    pthread_mutex_lock(&mutex);
    add2XML(xml);
    pthread_mutex_unlock(&mutex);

    xml->endbranch();

    *data = xml->getXMLdata();
    delete (xml);
    return strlen(*data) + 1;
}

void Master::putalldata(char *data, int /*size*/)
{
    XMLwrapper *xml = new XMLwrapper();
    if(!xml->putXMLdata(data)) {
        delete (xml);
        return;
    }

    if(xml->enterbranch("MASTER") == 0)
        return;

    pthread_mutex_lock(&mutex);
    getfromXML(xml);
    pthread_mutex_unlock(&mutex);

    xml->exitbranch();

    delete (xml);
}

int Master::saveXML(const char *filename)
{
    XMLwrapper *xml = new XMLwrapper();

    xml->beginbranch("MASTER");
    add2XML(xml);
    xml->endbranch();

    int result = xml->saveXMLfile(filename);
    delete (xml);
    return result;
}



int Master::loadXML(const char *filename)
{
    XMLwrapper *xml = new XMLwrapper();
    if(xml->loadXMLfile(filename) < 0) {
        delete (xml);
        return -1;
    }

    if(xml->enterbranch("MASTER") == 0)
        return -10;
    getfromXML(xml);
    xml->exitbranch();

    delete (xml);
    return 0;
}

void Master::getfromXML(XMLwrapper *xml)
{
    setPvolume(xml->getpar127("volume", Pvolume));
    setPkeyshift(xml->getpar127("key_shift", Pkeyshift));
    ctl.NRPN.receive = xml->getparbool("nrpn_receive", ctl.NRPN.receive);


    part[0]->Penabled = 0;
    for(int npart = 0; npart < NUM_MIDI_PARTS; npart++) {
        if(xml->enterbranch("PART", npart) == 0)
            continue;
        part[npart]->getfromXML(xml);
        xml->exitbranch();
    }

    if(xml->enterbranch("MICROTONAL")) {
        microtonal.getfromXML(xml);
        xml->exitbranch();
    }

    sysefx[0]->changeeffect(0);
    if(xml->enterbranch("SYSTEM_EFFECTS")) {
        for(int nefx = 0; nefx < NUM_SYS_EFX; nefx++) {
            if(xml->enterbranch("SYSTEM_EFFECT", nefx) == 0)
                continue;
            if(xml->enterbranch("EFFECT")) {
                sysefx[nefx]->getfromXML(xml);
                xml->exitbranch();
            }

            for(int partefx = 0; partefx < NUM_MIDI_PARTS; partefx++) {
                if(xml->enterbranch("VOLUME", partefx) == 0)
                    continue;
                setPsysefxvol(partefx, nefx,
                              xml->getpar127("vol", Psysefxvol[partefx][nefx]));
                xml->exitbranch();
            }

            for(int tonefx = nefx + 1; tonefx < NUM_SYS_EFX; tonefx++) {
                if(xml->enterbranch("SENDTO", tonefx) == 0)
                    continue;
                setPsysefxsend(nefx, tonefx,
                               xml->getpar127("send_vol",
                                              Psysefxsend[nefx][tonefx]));
                xml->exitbranch();
            }
            xml->exitbranch();
        }
        xml->exitbranch();
    }


    if(xml->enterbranch("INSERTION_EFFECTS")) {
        for(int nefx = 0; nefx < NUM_INS_EFX; nefx++) {
            if(xml->enterbranch("INSERTION_EFFECT", nefx) == 0)
                continue;
            Pinsparts[nefx] = xml->getpar("part",
                                          Pinsparts[nefx],
                                          -2,
                                          NUM_MIDI_PARTS);
            if(xml->enterbranch("EFFECT")) {
                insefx[nefx]->getfromXML(xml);
                xml->exitbranch();
            }
            xml->exitbranch();
        }

        xml->exitbranch();
    }
}


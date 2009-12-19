/*
  ZynAddSubFX - a software synthesizer

  Recorder.C - Records sound to a file
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

#include <sys/stat.h>
#include "Recorder.h"
#ifdef NEW_IO
#include "../Nio/OutMgr.h"
#include "../Nio/WavEngine.h"
#endif

Recorder::Recorder()
    :status(0), notetrigger(0), wave(NULL)
{}

Recorder::~Recorder()
{
    if(recording() == 1)
        stop();
}

int Recorder::preparefile(std::string filename_, int overwrite)
{
    if(!overwrite) {
        struct stat fileinfo;
        int statr;
        statr = stat(filename_.c_str(), &fileinfo);
        if(statr == 0)   //file exists
            return 1;
    }

    if(!(wave=new WavEngine(sysOut, filename_, SAMPLE_RATE, 2)))
        return 2;

    status = 1; //ready

    return 0;
}

void Recorder::start()
{
    notetrigger = 0;
    status      = 2; //recording
}

void Recorder::stop()
{
    if(wave)
    {
        sysOut->remove(wave);
        wave->Close();
        delete wave;
        wave = NULL; //is this even needed?
    }
    status = 0;
}

void Recorder::pause()
{
    status = 0;
//        wave->Stop();
        sysOut->remove(wave);
}

int Recorder::recording()
{
    if((status == 2) && (notetrigger != 0))
        return 1;
    else
        return 0;
}

void Recorder::triggernow()
{
    if(status == 2) {
        if(notetrigger!=1) {
            wave->openAudio();
            //wave->Start();
            sysOut->add(wave);
        }
        notetrigger = 1;
    }
}


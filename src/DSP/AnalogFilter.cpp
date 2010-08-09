/*
  ZynAddSubFX - a software synthesizer

  AnalogFilter.cpp - Several analog filters (lowpass, highpass...)
  Copyright (C) 2002-2005 Nasca Octavian Paul
  Copyright (C) 2010-2010 Mark McCurry
  Author: Nasca Octavian Paul
          Mark McCurry

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
#include <cstdio>
#include <cstring> //memcpy
#include "../Misc/Util.h"
#include "AnalogFilter.h"

AnalogFilter::AnalogFilter(unsigned char Ftype,
                           REALTYPE Ffreq,
                           REALTYPE Fq,
                           unsigned char Fstages)
{
    stages = Fstages;
    for(int i = 0; i < 3; i++) {
        coeff.c[i]    = 0.0;
        coeff.d[i]    = 0.0;
        oldCoeff.c[i] = 0.0;
        oldCoeff.d[i] = 0.0;
    }
    type = Ftype;
    freq = Ffreq;
    q    = Fq;
    gain = 1.0;
    if(stages >= MAX_FILTER_STAGES)
        stages = MAX_FILTER_STAGES;
    cleanup();
    firsttime  = false;
    abovenq    = false;
    oldabovenq = false;
    setfreq_and_q(Ffreq, Fq);
    firsttime  = true;
    coeff.d[0] = 0; //this is not used
    outgain    = 1.0;
}

AnalogFilter::~AnalogFilter()
{}

void AnalogFilter::cleanup()
{
    for(int i = 0; i < MAX_FILTER_STAGES + 1; i++) {
        history[i].x1 = 0.0;
        history[i].x2 = 0.0;
        history[i].y1 = 0.0;
        history[i].y2 = 0.0;
        oldHistory[i] = history[i];
    }
    needsinterpolation = false;
}

void AnalogFilter::computefiltercoefs()
{
    REALTYPE tmp;
    bool     zerocoefs = false; //this is used if the freq is too high

    //do not allow frequencies bigger than samplerate/2
    REALTYPE freq = this->freq;
    if(freq > (SAMPLE_RATE / 2 - 500.0)) {
        freq      = SAMPLE_RATE / 2 - 500.0;
        zerocoefs = true;
    }
    if(freq < 0.1)
        freq = 0.1;
    //do not allow bogus Q
    if(q < 0.0)
        q = 0.0;
    REALTYPE tmpq, tmpgain;
    if(stages == 0) {
        tmpq    = q;
        tmpgain = gain;
    }
    else {
        tmpq    = (q > 1.0 ? pow(q, 1.0 / (stages + 1)) : q);
        tmpgain = pow(gain, 1.0 / (stages + 1));
    }

    //Alias Terms
    REALTYPE *c = coeff.c;
    REALTYPE *d = coeff.d;

    //General Constants
    const REALTYPE omega = 2 * PI * freq / SAMPLE_RATE;
    const REALTYPE sn = sin(omega), cs = cos(omega);
    REALTYPE alpha, beta;

    //most of theese are implementations of
    //the "Cookbook formulae for audio EQ" by Robert Bristow-Johnson
    //The original location of the Cookbook is:
    //http://www.harmony-central.com/Computer/Programming/Audio-EQ-Cookbook.txt
    switch(type) {
    case 0: //LPF 1 pole
        if(!zerocoefs)
            tmp = exp(-2.0 * PI * freq / SAMPLE_RATE);
        else
            tmp = 0.0;
        c[0]  = 1.0 - tmp;
        c[1]  = 0.0;
        c[2]  = 0.0;
        d[1]  = tmp;
        d[2]  = 0.0;
        order = 1;
        break;
    case 1: //HPF 1 pole
        if(!zerocoefs)
            tmp = exp(-2.0 * PI * freq / SAMPLE_RATE);
        else
            tmp = 0.0;
        c[0]  = (1.0 + tmp) / 2.0;
        c[1]  = -(1.0 + tmp) / 2.0;
        c[2]  = 0.0;
        d[1]  = tmp;
        d[2]  = 0.0;
        order = 1;
        break;
    case 2: //LPF 2 poles
        if(!zerocoefs) {
            alpha = sn / (2 * tmpq);
            tmp   = 1 + alpha;
            c[0]  = (1.0 - cs) / 2.0 / tmp;
            c[1]  = (1.0 - cs) / tmp;
            c[2]  = (1.0 - cs) / 2.0 / tmp;
            d[1]  = -2 * cs / tmp * (-1);
            d[2]  = (1 - alpha) / tmp * (-1);
        }
        else {
            c[0] = 1.0;
            c[1] = 0.0;
            c[2] = 0.0;
            d[1] = 0.0;
            d[2] = 0.0;
        }
        order = 2;
        break;
    case 3: //HPF 2 poles
        if(!zerocoefs) {
            alpha = sn / (2 * tmpq);
            tmp   = 1 + alpha;
            c[0]  = (1.0 + cs) / 2.0 / tmp;
            c[1]  = -(1.0 + cs) / tmp;
            c[2]  = (1.0 + cs) / 2.0 / tmp;
            d[1]  = -2 * cs / tmp * (-1);
            d[2]  = (1 - alpha) / tmp * (-1);
        }
        else {
            c[0] = 0.0;
            c[1] = 0.0;
            c[2] = 0.0;
            d[1] = 0.0;
            d[2] = 0.0;
        }
        order = 2;
        break;
    case 4: //BPF 2 poles
        if(!zerocoefs) {
            alpha = sn / (2 * tmpq);
            tmp   = 1 + alpha;
            c[0]  = alpha / tmp *sqrt(tmpq + 1);
            c[1]  = 0;
            c[2]  = -alpha / tmp *sqrt(tmpq + 1);
            d[1]  = -2 * cs / tmp * (-1);
            d[2]  = (1 - alpha) / tmp * (-1);
        }
        else {
            c[0] = 0.0;
            c[1] = 0.0;
            c[2] = 0.0;
            d[1] = 0.0;
            d[2] = 0.0;
        }
        order = 2;
        break;
    case 5: //NOTCH 2 poles
        if(!zerocoefs) {
            alpha = sn / (2 * sqrt(tmpq));
            tmp   = 1 + alpha;
            c[0]  = 1 / tmp;
            c[1]  = -2 * cs / tmp;
            c[2]  = 1 / tmp;
            d[1]  = -2 * cs / tmp * (-1);
            d[2]  = (1 - alpha) / tmp * (-1);
        }
        else {
            c[0] = 1.0;
            c[1] = 0.0;
            c[2] = 0.0;
            d[1] = 0.0;
            d[2] = 0.0;
        }
        order = 2;
        break;
    case 6: //PEAK (2 poles)
        if(!zerocoefs) {
            tmpq *= 3.0;
            alpha = sn / (2 * tmpq);
            tmp   = 1 + alpha / tmpgain;
            c[0]  = (1.0 + alpha * tmpgain) / tmp;
            c[1]  = (-2.0 * cs) / tmp;
            c[2]  = (1.0 - alpha * tmpgain) / tmp;
            d[1]  = -2 * cs / tmp * (-1);
            d[2]  = (1 - alpha / tmpgain) / tmp * (-1);
        }
        else {
            c[0] = 1.0;
            c[1] = 0.0;
            c[2] = 0.0;
            d[1] = 0.0;
            d[2] = 0.0;
        }
        order = 2;
        break;
    case 7: //Low Shelf - 2 poles
        if(!zerocoefs) {
            tmpq  = sqrt(tmpq);
            alpha = sn / (2 * tmpq);
            beta  = sqrt(tmpgain) / tmpq;
            tmp   = (tmpgain + 1.0) + (tmpgain - 1.0) * cs + beta * sn;

            c[0]  = tmpgain
                    * ((tmpgain
                        + 1.0) - (tmpgain - 1.0) * cs + beta * sn) / tmp;
            c[1]  = 2.0 * tmpgain
                    * ((tmpgain - 1.0) - (tmpgain + 1.0) * cs) / tmp;
            c[2]  = tmpgain
                    * ((tmpgain
                        + 1.0) - (tmpgain - 1.0) * cs - beta * sn) / tmp;
            d[1]  = -2.0 * ((tmpgain - 1.0) + (tmpgain + 1.0) * cs) / tmp * (-1);
            d[2]  =
                ((tmpgain
                  + 1.0) + (tmpgain - 1.0) * cs - beta * sn) / tmp * (-1);
        }
        else {
            c[0] = tmpgain;
            c[1] = 0.0;
            c[2] = 0.0;
            d[1] = 0.0;
            d[2] = 0.0;
        }
        order = 2;
        break;
    case 8: //High Shelf - 2 poles
        if(!zerocoefs) {
            tmpq  = sqrt(tmpq);
            alpha = sn / (2 * tmpq);
            beta  = sqrt(tmpgain) / tmpq;
            tmp   = (tmpgain + 1.0) - (tmpgain - 1.0) * cs + beta * sn;

            c[0]  = tmpgain
                    * ((tmpgain
                        + 1.0) + (tmpgain - 1.0) * cs + beta * sn) / tmp;
            c[1]  = -2.0 * tmpgain
                    * ((tmpgain - 1.0) + (tmpgain + 1.0) * cs) / tmp;
            c[2]  = tmpgain
                    * ((tmpgain
                        + 1.0) + (tmpgain - 1.0) * cs - beta * sn) / tmp;
            d[1]  = 2.0 * ((tmpgain - 1.0) - (tmpgain + 1.0) * cs) / tmp * (-1);
            d[2]  =
                ((tmpgain
                  + 1.0) - (tmpgain - 1.0) * cs - beta * sn) / tmp * (-1);
        }
        else {
            c[0] = 1.0;
            c[1] = 0.0;
            c[2] = 0.0;
            d[1] = 0.0;
            d[2] = 0.0;
        }
        order = 2;
        break;
    default: //wrong type
        type = 0;
        computefiltercoefs();
        break;
    }
}


void AnalogFilter::setfreq(REALTYPE frequency)
{
    if(frequency < 0.1)
        frequency = 0.1;
    REALTYPE rap = freq / frequency;
    if(rap < 1.0)
        rap = 1.0 / rap;

    oldabovenq = abovenq;
    abovenq    = frequency > (SAMPLE_RATE / 2 - 500.0);

    bool nyquistthresh = (abovenq ^ oldabovenq);


    //if the frequency is changed fast, it needs interpolation
    if((rap > 3.0) || nyquistthresh) { //(now, filter and coeficients backup)
        oldCoeff   = coeff;
        for(int i = 0; i < MAX_FILTER_STAGES + 1; ++i)
            oldHistory[i] = history[i];
        if(!firsttime)
            needsinterpolation = true;
    }
    freq      = frequency;
    computefiltercoefs();
    firsttime = false;
}

void AnalogFilter::setfreq_and_q(REALTYPE frequency, REALTYPE q_)
{
    q = q_;
    setfreq(frequency);
}

void AnalogFilter::setq(REALTYPE q_)
{
    q = q_;
    computefiltercoefs();
}

void AnalogFilter::settype(int type_)
{
    type = type_;
    computefiltercoefs();
}

void AnalogFilter::setgain(REALTYPE dBgain)
{
    gain = dB2rap(dBgain);
    computefiltercoefs();
}

void AnalogFilter::setstages(int stages_)
{
    if(stages_ >= MAX_FILTER_STAGES)
        stages_ = MAX_FILTER_STAGES - 1;
    stages = stages_;
    cleanup();
    computefiltercoefs();
}

void AnalogFilter::singlefilterout(REALTYPE *smp, fstage &hist,
                                   const Coeff &coeff)
{
    if(order == 1) { //First order filter
        for(int i = 0; i < SOUND_BUFFER_SIZE; i++) {
            REALTYPE y0 =    smp[i]*coeff.c[0] + hist.x1*coeff.c[1]
                          + hist.y1*coeff.d[1];
            hist.y1 = y0;
            hist.x1 = smp[i];
            smp[i]  = y0;
        }
    }
    if(order == 2) { //Second order filter
        for(int i = 0; i < SOUND_BUFFER_SIZE; i++) {
            REALTYPE y0 =    smp[i]*coeff.c[0] + hist.x1*coeff.c[1]
                          + hist.x2*coeff.c[2] + hist.y1*coeff.d[1]
                          + hist.y2*coeff.d[2];
            hist.y2 = hist.y1;
            hist.y1 = y0;
            hist.x2 = hist.x1;
            hist.x1 = smp[i];
            smp[i]  = y0;
        }
    }
}
void AnalogFilter::filterout(REALTYPE *smp)
{
    for(int i = 0; i < stages + 1; i++)
        singlefilterout(smp, history[i], coeff);

    if(needsinterpolation) {
        //Merge Filter at old coeff with new coeff
        REALTYPE *ismp = getTmpBuffer();
        memcpy(ismp, smp, sizeof(REALTYPE) * SOUND_BUFFER_SIZE);

        for(int i = 0; i < stages + 1; i++)
            singlefilterout(ismp, oldHistory[i], oldCoeff);

        for(int i = 0; i < SOUND_BUFFER_SIZE; i++) {
            REALTYPE x = i / (REALTYPE) SOUND_BUFFER_SIZE;
            smp[i] = ismp[i] * (1.0 - x) + smp[i] * x;
        }
        returnTmpBuffer(ismp);
        needsinterpolation = false;
    }

    for(int i = 0; i < SOUND_BUFFER_SIZE; i++)
        smp[i] *= outgain;
}

REALTYPE AnalogFilter::H(REALTYPE freq)
{
    //REALTYPE fr = freq / SAMPLE_RATE * PI * 2.0;
    const REALTYPE fr = freq * 2.0 * PI;
    REALTYPE x  = coeff.c[0], y = 0.0;
    for(int n = 1; n < 3; n++) {
        x += cos(n * fr) * coeff.c[n];
        y -= sin(n * fr) * coeff.c[n];
    }
    REALTYPE h = x * x + y * y;
    x = 1.0;
    y = 0.0;
    for(int n = 1; n < 3; n++) {
        x -= cos(n * fr) * coeff.d[n];
        y += sin(n * fr) * coeff.d[n];
    }
    h /= (x * x + y * y);

    printf("%f -> %f\n",freq,pow(h, (stages + 1.0) / 2.0));
    return pow(h, (stages + 1.0) / 2.0);
}


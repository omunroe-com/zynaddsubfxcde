#include "SynthNote.h"
#include "../globals.h"
#include <cstring>

SynthNote::SynthNote(REALTYPE freq, REALTYPE vel, int port, int note, bool quiet)
    :legato(freq,vel,port,note,quiet)
{}

SynthNote::Legato::Legato(REALTYPE freq, REALTYPE vel, int port,
                          int note, bool quiet)
{
    // Initialise some legato-specific vars
    msg = LM_Norm;
    fade.length      = (int)(SAMPLE_RATE * 0.005); // 0.005 seems ok.
    if(fade.length < 1)
        fade.length = 1;                    // (if something's fishy)
    fade.step        = (1.0 / fade.length);
    decounter        = -10;
    param.freq       = freq;
    param.vel        = vel;
    param.portamento = port;
    param.midinote   = note;
    silent  = quiet;
}

int SynthNote::Legato::update(REALTYPE freq, REALTYPE velocity, int portamento_,
                               int midinote_, bool externcall)
{
    if(externcall)
        msg = LM_Norm;
    if(msg != LM_CatchUp) {
        lastfreq   = param.freq;
        param.freq = freq;
        param.vel  = velocity;
        param.portamento = portamento_;
        param.midinote   = midinote_;
        if(msg == LM_Norm) {
            if(silent) {
                fade.m = 0.0;
                msg    = LM_FadeIn;
            }
            else {
                fade.m = 1.0;
                msg    = LM_FadeOut;
                return 1;
            }
        }
        if(msg == LM_ToNorm)
            msg = LM_Norm;
    }
    return 0;
}

void SynthNote::Legato::apply(SynthNote &note, REALTYPE *outl, REALTYPE *outr)
{
    if(silent) // Silencer
        if(msg != LM_FadeIn) {
            memset(outl, 0, SOUND_BUFFER_SIZE * sizeof(REALTYPE));
            memset(outr, 0, SOUND_BUFFER_SIZE * sizeof(REALTYPE));
        }
    switch(msg) {
    case LM_CatchUp:  // Continue the catch-up...
        if(decounter == -10)
            decounter = fade.length;
        //Yea, could be done without the loop...
        for(int i = 0; i < SOUND_BUFFER_SIZE; i++) {
            decounter--;
            if(decounter < 1) {
                // Catching-up done, we can finally set
                // the note to the actual parameters.
                decounter = -10;
                msg = LM_ToNorm;
                note.legatonote(param.freq, param.vel, param.portamento,
                                param.midinote, false);
                break;
            }
        }
        break;
    case LM_FadeIn:  // Fade-in
        if(decounter == -10)
            decounter = fade.length;
        silent = false;
        for(int i = 0; i < SOUND_BUFFER_SIZE; i++) {
            decounter--;
            if(decounter < 1) {
                decounter = -10;
                msg = LM_Norm;
                break;
            }
            fade.m += fade.step;
            outl[i] *= fade.m;
            outr[i] *= fade.m;
        }
        break;
    case LM_FadeOut:  // Fade-out, then set the catch-up
        if(decounter == -10)
            decounter = fade.length;
        for(int i = 0; i < SOUND_BUFFER_SIZE; i++) {
            decounter--;
            if(decounter < 1) {
                for(int j = i; j < SOUND_BUFFER_SIZE; j++) {
                    outl[j] = 0.0;
                    outr[j] = 0.0;
                }
                decounter = -10;
                silent    = true;
                // Fading-out done, now set the catch-up :
                decounter = fade.length;
                msg = LM_CatchUp;
                //This freq should make this now silent note to catch-up/resync
                //with the heard note for the same length it stayed at the
                //previous freq during the fadeout.
                REALTYPE catchupfreq = param.freq * (param.freq / lastfreq);
                note.legatonote(catchupfreq, param.vel, param.portamento,
                                param.midinote, false);
                break;
            }
            fade.m -= fade.step;
            outl[i] *= fade.m;
            outr[i] *= fade.m;
        }
        break;
    default:
        break;
    }
}


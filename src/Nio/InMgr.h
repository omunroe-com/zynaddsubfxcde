#ifndef INMGR_H
#define INMGR_H

#include <string>
#include <semaphore.h>
#include "SafeQueue.h"

enum midi_type{
    M_NOTE = 1,
    M_CONTROLLER = 2
};    //type=1 for note, type=2 for controller

struct MidiDriverEvent
{
    MidiDriverEvent();
    int channel; //the midi channel for the event
    int type;    //type=1 for note, type=2 for controller
    int num;     //note or contoller number
    int value;   //velocity or controller value
};

//super simple class to manage the inputs
class InMgr
{
    public:
        static InMgr &getInstance();
        ~InMgr();

        void putEvent(MidiDriverEvent ev);

        /**Flush the Midi Queue*/
        void flush();

        bool setSource(std::string name);

        std::string getSource() const;

        friend class EngineMgr;
    private:
        InMgr();
        class MidiIn *getIn(std::string name);
        SafeQueue<MidiDriverEvent> queue;
        sem_t work;
        class MidiIn *current;

        /**the link to the rest of zyn*/
        class Master &master;
};

#endif


/*
  ZynAddSubFX - a software synthesizer

  minimal.cpp - ZynAddSubFX plugin for the "minimal" sequencer
  Copyright (C) 2014-2016 Johannes Lorenz
  Author: Johannes Lorenz

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

#include "../globals.h"
#include "../Misc/Util.h"
#include "../Misc/MiddleWare.h"
#include "../Misc/Master.h"
#include "../Misc/Config.h"

#include "minimal/io.h"
#include "minimal-zynaddsubfx.h"

// global variables required for linking:
// TODO: make them class members?
//SYNTH_T  *synth;
MiddleWare *middleware;
#if USE_NSM
#include "../UI/NSM.H"

NSM_Client *nsm = 0;
#endif

#include <thread>
#include <unistd.h>
#include <climits>

namespace mini {

zyn_tree_t::zyn_tree_t(const char *name) :
	nnode("", nullptr),
	audio_instrument_t(name),
	events_t_port(this)
{
}

command_base *zynaddsubfx_t::make_close_command() const
{
	return new command<>("/close-ui");
}

void zynaddsubfx_t::prepare()
{
set_sample_rate(44100); // TODO!!


    std::clog << "Starting zynaddsubfx..." << std::endl;
    std::clog << "  (this is a good sign...)" << std::endl;
    
	SYNTH_T synth;
	//synth->samplerate = sample_rate; // TODO!!!
	
	//    this->sampleRate  = srate;
	//    this->banksInited = false;
	
	config = new Config();
	config->init();
	
	sprng(::time(nullptr));
	
	synth.alias();
	middleware = new MiddleWare(std::move(synth), config);
	middleware->spawnMaster()->bank.rescanforbanks();
/*	loadThread = new std::thread([this]() {
		while(middleware) {
		middleware->tick();
		usleep(1000);
		}});*/
}

struct master_functor
{
    Master* _master;
    sample_rate_t sample_rate;
    void operator()(std::size_t , std::size_t amnt,
        mini::Stereo<float>* dest)
    {
        io::mlog << "amnt: " << amnt << io::endl;
        // first arg is not needed: master currently counts it itself
        // (TODO: allow master to start at offset)
        if(! dest)
         io::mlog << "WARNING 1..." << io::endl;
        io::mlog << "buf2: " << dest << io::endl;
        _master->GetAudioOutSamplesStereo(amnt, (int)sample_rate, dest);
    }
};

void zynaddsubfx_t::run_synth(unsigned long ,
		snd_seq_event_t *,
		unsigned long )
{
    const std::size_t buffersize = middleware->getSynth().buffersize;

    if(samples_played <= time())
    {
    Master *master = middleware->spawnMaster();

    io::mlog << "write space: " << value().write_space()
	 << " samples: " << samples_played
	 << " buffer size: " << buffersize << io::endl;

    if( audio_out::value().write_space() < buffersize )
        throw "warning: not enough write space! :-(";
    
    io::mlog << "buf1: " << &audio_out::value() << io::endl;

    master_functor mf { master, zynaddsubfx_t::sample_rate };
    value().write_func(mf, buffersize); // TODO: not sure about this value
    samples_played += buffersize;
    }
    
    
     // TODO: not sure about this value:
    set_next_time(time() + buffersize);
    
}

void zynaddsubfx_t::send_osc_cmd(const char * msg)
{
    middleware->transmitMsg(msg);
}

void zynaddsubfx_t::set_sample_rate(sample_rate_t srate) {
	zynaddsubfx_t::sample_rate = srate; }

bool zynaddsubfx_t::advance() {
	//assert(sample_rate);
	run_synth(0, nullptr, 0ul);
	return true;
}

zyn_tree_t::events_t_port_t::events_t_port_t(zyn_tree_t *parent) :
	// todo: base, ext does not make sense here?
	osc_in_port_t<notes_in>(*parent->get_ins()),
	parent_ptr(parent),
	ins(parent->get_ins())
{
	note_ons.reserve(NOTES_MAX);
	note_offs.reserve(NOTES_MAX);
	for(std::size_t idx = 0; idx < NOTES_MAX; ++idx)
	{
		note_ons.emplace_back(parent, ins, 0 /*chan*/, idx/*offs*/,
			self_port_templ<int, true>{});
		note_offs.emplace_back(parent, ins, 0 /*chan*/, idx/*offs*/,
			self_port_templ<int, true>{});
	}

	set_trigger(); // TODO: here?
}

void zyn_tree_t::events_t_port_t::on_read(sample_no_t pos)
{
	io::mlog << "zyn notes port::on_read" << io::endl;
        for(const std::pair<int, int>& rch : notes_in::data.sender->recently_changed)
        if(rch.first < 0) // note height < 0, this will probably not happen...
	 break;
	else
	{
		// for self_port_t, on_read is not virtual, so we call it manually...
		// -> TODO?? probably the above comment is deprecated
                std::pair<int, daw::music_note_properties> p2 = notes_in::data.sender->lines[rch.first][rch.second];

		io::mlog << "first, second: " << p2.first << ", " << p2.second.value() << io::endl;

		if(p2.first >= 0) // i.e. note on
		{
			m_note_on_t& note_on_cmd = note_ons[rch.first];
			// self_port_t must be completed manually:
			note_on_cmd.cmd_ptr->port_at<2>().set(p2.second.value());
			note_on_cmd.cmd_ptr->command::update();

			note_on_cmd.cmd_ptr->complete_buffer(); // TODO: call in on_read??
		}

		auto& cmd_ref = (p2.first < 0)
			? note_offs[rch.first].cmd
			: note_ons[rch.first].cmd;

		// TODO!!
		// note_offs[p.first].on_read();
		if(cmd_ref.set_changed())
		{
			cmd_ref.update_next_time(pos); // TODO: call on recv
			ins->update(cmd_ref.get_handle());
		}
	}
}

}


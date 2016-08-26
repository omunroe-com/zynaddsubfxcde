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

#ifndef MINIMAL_H
#define MINIMAL_H

#include <vector>
#include "minimal/sample.h"
#include "minimal/audio_instrument.h" // TODO: separate audio_instrument_t ?
#include "minimal/command_tools.h"
#include "minimal/mports.h"
#include "minimal/notes.h"

class MiddleWare;
struct SYNTH_T;
class Config;

namespace std
{
	class thread;
}

namespace mini
{

class zyn_tree_t;

namespace zyn {

template<class PortT>
using port = command_with_ports<PortT>;

}

//! arbitrary single pointer class
template<class Access>
class single_ptr
{
	Access* acc = nullptr;
public:
	template<class ...Args>
	Access* get(Args... args) {
		return (acc == nullptr)
			? (acc = new Access(args...))
			: acc;
	}
	Access* operator->() {
		return get<>();
	}
};

template<class Access, std::size_t Size>
class single_ptr_array
{
	std::vector<single_ptr<Access>> acc;
public:
	template<class ...Args>
	Access* get(std::size_t idx, Args... args) {
		if(acc.empty())
		 acc.resize(Size);
		return acc[idx].get(args...);
	}
};

class build_base
{
protected:
	nnode* parent = nullptr;
	const char* name = "/";
public:
	build_base() = default;

	build_base(const nnode::ctor& ctor) :
		parent(ctor.parent),
		name(ctor.name)
		{}
};

template<class NodeT>
class build : single_ptr<NodeT>, public build_base
{
public:
	using build_base::build_base;

	NodeT* operator->() {
		return single_ptr<NodeT>::get(name, parent);
	}
};

template<class NodeT, std::size_t Size>
class build_multi : single_ptr_array<NodeT, Size>, public build_base
{
public:
	using build_base::build_base;

	NodeT& operator[](std::size_t idx) {
		return *single_ptr_array<NodeT, Size>::get(idx, name, parent, idx);
	}
};

template<class PortT>
struct pram : public command_with_ports<PortT>
{
	pram(const char* ext, nnode* parent) : // : nnode(ext, parent),
		// we can not virtually call get_ins() in the ctor,
		// but we can call it from parent :P
		command_with_ports<PortT>(parent, parent->get_ins(), ext)
	{
	//	spawn<zyn::port<PortT>>(ext);
	}
	/*operator PortT&() {
		return spawn<zyn::port<PortT>>("");
	}*/

	/*PortT& operator()() {
		return spawn<zyn::port<PortT>>("");
	}*/
};

template<class T>
using build_inport = build<pram<in_port_t<T>>>;

struct kit0_t : nnode
{
	using nnode::nnode;
};

class amp_env_t : public nnode
{
public:
	using nnode::nnode;
	template<class Port>
	zyn::port<Port>& envsustain() {
		return spawn<zyn::port<Port>>("Penvsustain");
		//return new param_t("envsustain", ins);

	} // TODO: own envsustain class with operator() ?

	/*template<class Port1>
	zyn::p_envsustain<Port1> envsustain(oint<Port1> con) const {
		return p_envsustain<Port1>(con);
	}*/
};

class global_t : public nnode
{
public:
	using nnode::nnode;
	build<amp_env_t> amp_env = ctor(this, "AmpEnvelope");
};

class oscil_smp : public nnode
{
	using nnode::nnode;
};

class voice0_t : public nnode
{
public:
	using nnode::nnode;
	build<oscil_smp> oscil = ctor(this, "OscilSmp");
	build_inport<int> volume = ctor(this, "PVolume");
	build_inport<int> panning = ctor(this, "PPanning");
};

class padpars : public nnode
{
	void on_preinit() { // TODO
//		ins->add_const_command(command<bool>("... pad enabled", true));
	}
public:
	using nnode::nnode;
};

class adpars_t : public nnode
{
	void on_preinit() { // TODO
//		ins->add_const_command(command<bool>("... add enabled", true));
	}
public:
	using nnode::nnode;
	build<voice0_t> voice0 = ctor(this, "voice0");
	build<global_t> global = ctor(this, "global");

};

template<class = void, bool = false>
class use_no_port {};

template<template<class, bool> class P, class T>
struct _port_type_of { using type = P<T, true>; };

template<class T>
struct _port_type_of<use_no_port, T> { using type = T; };

template<template<class , bool> class P, class T>
using port_type_of = typename _port_type_of<P, T>::type;

class zyn_tree_t : public nnode, public audio_instrument_t
{
	// todo: only send some params on new note?
public:
	template<template<class , bool> class Port1 = use_no_port,
		template<class , bool> class Port2 = use_no_port,
		template<class , bool> class Port3 = use_no_port>
	class note_on : public command_with_ports<port_type_of<Port1, int>, port_type_of<Port2, int>, port_type_of<Port3, int>>
	{
		using base = command_with_ports<port_type_of<Port1, int>, port_type_of<Port2, int>, port_type_of<Port3, int>>;
	public:
		note_on(nnode* parent, instrument_t* zyn, port_type_of<Port1, int> chan, port_type_of<Port2, int> note, port_type_of<Port3, int>&& velocity) // TODO: rvals
			: base(parent, zyn, "noteOn", chan, note, std::forward<port_type_of<Port3, int>>(velocity)) // TODO: forward instead of move?
		{
		}
	};

	template<template<class , bool> class Port1 = use_no_port,
		template<class , bool> class Port2 = use_no_port,
		template<class , bool> class Port3 = use_no_port>
	class note_off : public command_with_ports<port_type_of<Port1, int>, port_type_of<Port2, int>, port_type_of<Port3, int>>
	{
		using base = command_with_ports<port_type_of<Port1, int>, port_type_of<Port2, int>, port_type_of<Port3, int>>;
	public:
		note_off(nnode* parent, instrument_t* zyn, port_type_of<Port1, int> chan, port_type_of<Port2, int> note, port_type_of<Port3, int>&& id)
			: base(parent, zyn, "noteOff", chan, note, std::forward<port_type_of<Port3, int>>(id))
		{
		}
	};
private:
	using c_note_on = note_on<use_no_port, use_no_port, self_port_templ>;
	using c_note_off = note_off<use_no_port, use_no_port, self_port_templ>;

        //! mostly an in_port_t<event_signal_receiver_t<T>>
	struct events_t_port_t : osc_in_port_t<notes_in>
	{
		zyn_tree_t* parent_ptr;
		command_base* cmd;
		instrument_t* ins; // TODO: remove this?
		using m_note_on_t = note_on<use_no_port, use_no_port, self_port_templ>;
		using m_note_off_t = note_off<use_no_port, use_no_port, self_port_templ>;

		std::vector<m_note_on_t> note_ons;
		std::vector<m_note_off_t> note_offs;
	public:
		events_t_port_t(class zyn_tree_t* parent);

		void on_read(sample_no_t pos);
	};

	events_t_port_t events_t_port; // TODO: inherit??


public:

	zyn_tree_t(const char* name);
	virtual ~zyn_tree_t() {}

	/*
	 *  ports
	 */

	//build<add0> add0; //= ctor(this, "voice0");


	/*zyn::adpars add0() {
		//return spawn<zyn::adpars>("part0/kit0/adpars");
		return part0().kit0().adpars();
	}

	zyn::padpars pad0() {
		return spawn<zyn::padpars>("part0/kit0/padpars");
	}*/
	// TODO: add0;

	class pars_base : public nnode
	{
		void on_preinit() { // TODO
	//		ins->add_const_command(command<bool>("... add enabled", true));
		}
	public:
		using nnode::nnode;
		build<voice0_t> voice0 = ctor(this, "voice0");
		build<global_t> global = ctor(this, "global");
	};

	class add_pars : public pars_base {};
	class pad_pars : public pars_base {};

	events_t_port_t& note_input() {
		return events_t_port;
	}


	struct fx_t : public nnode
	{
		/*template<class Port>
		zyn::port<Port>& efftype() { // TODO: panning must be int...
			return spawn<zyn::port<Port>>("efftype");
		}*/

		build_inport<int> efftype = ctor(this, "efftype");

		// TODO: for debugging features, efftype should be a node, too

		// TODO: template is useless
/*		template<class Port>
		zyn::port<Port>& eff0_part_id() { // TODO: panning must be int...
			return *new zyn::port<Port>(this, get_ins(), "/", "efftype");
			//return spawn_new<zyn::port<Port>>("efftype");
		}*/
		using nnode::nnode;
		//build<voice0> voice0 = ctor(this, "voice0");
		//build<global> global = ctor(this, "global");
	};


	template<class Port>
	zyn::port<Port>& eff0_part_id() { // TODO: panning must be int...
		return *new zyn::port<Port>(this, get_ins(), "/", "efftype");
		//return spawn_new<zyn::port<Port>>("efftype");
	}

	class kit_t : public nnode
	{
	public:
		using nnode::nnode;
		build<adpars_t> adpars = ctor(this, "adpars");
	};

	class part_t : public nnode
	{
	public:
		using nnode::nnode;
		//zyn::amp_env amp_env() const {
		//	return spawn<zyn::amp_env>("AmpEnvelope/");
		//}
	/*	template<class Port>
		zyn::port<Port>& Ppanning() { // TODO: panning must be int...
			return spawn<zyn::port<Port>>("Ppanning");
		}*/
		build_inport<int> Ppanning = ctor(this, "Ppanning");

		build<fx_t> partefx = ctor(this, "partefx0"); // TODO: id, maybe as template?
		build<kit0_t> kit0 = ctor(this, "kit0");

	//	using T = fx_t (zyn_tree_t::*)(const std::string& ext) const;
	//	const T partefx2 = &zyn_tree_t::spawn<fx_t, 0>;


		//using partefx2 = zyn_tree_t::spawn<fx_t, 0>; // TODO: instead, return struct which has operator()?
	};


	// TODO
	//template<std::size_t Id>
	//fx_t insefx0() { return spawn<fx_t, Id>("insefx"); }


	//template<std::size_t Id>
	//part_t part() { return spawn<part_t, Id>("part"); } // TODO: large tuple for these
	//part_t part0() { return part<0>(); }

	build_multi<part_t, 16> part = ctor(this, "part"); // TODO: 16? 8?
	build_multi<fx_t, 3> insefx = ctor(this, "insefx"); // TODO? how many?

	instrument_t* get_ins() { return this; }

/*	// TODO???
	using volume_ptr_t = zyn::port<int>*(*)();
	//spawn_new<zyn::port<Port>>;
	volume_ptr_t volume = &spawn_new<zyn::port<int>>;
*/
	build_inport<int> volume = ctor(this, "volume");
};

class zynaddsubfx_t : public zyn_tree_t
{
	sample_no_t samples_played = 0;

	// globals are annoying. we can not use them in this class
	// MiddleWare *middleware;
	// SYNTH_T *synth;
	
	std::string make_start_command() const { return "/TODO"; }
	command_base* make_close_command() const;
	
	sample_rate_t sample_rate;
	std::thread* loadThread;
	
	using snd_seq_event_t = int;
	void run_synth(unsigned long, snd_seq_event_t *, unsigned long);
	
	float *outl; // TODO: -> into class?
	float *outr;
	
	std::size_t smp_pos = 0;
	Config* config;

public:
	using zyn_tree_t::zyn_tree_t;
	~zynaddsubfx_t() = default;
	
	void send_osc_cmd(const char*msg);
	
	void set_sample_rate(sample_rate_t srate); // TODO: inline?
	void prepare();
	bool advance(); // TODO: inline?
	
	void instantiate_first() { std::cerr << "instantiate zyn" << std::endl; prepare(); }
};

}

#endif // MINIMAL_H

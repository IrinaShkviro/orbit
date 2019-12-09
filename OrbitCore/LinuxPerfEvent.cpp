//-----------------------------------
// Copyright Pierric Gimmig 2013-2017
//-----------------------------------

//-----------------------------------
// Author: Florian Kuebler
//-----------------------------------

#include "LinuxPerfEvent.h"
#include "LinuxPerfEventVisitor.h"

//-----------------------------------------------------------------------------
void LinuxPerfLostEvent::accept(LinuxPerfEventVisitor* a_Visitor) { a_Visitor->visit(this); }

//-----------------------------------------------------------------------------
void LinuxForkEvent::accept(LinuxPerfEventVisitor* a_Visitor) { a_Visitor->visit(this); }

//-----------------------------------------------------------------------------
void LinuxSchedSwitchEvent::accept(LinuxPerfEventVisitor* a_Visitor) { a_Visitor->visit(this); }
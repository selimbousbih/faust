/************************************************************************
 ************************************************************************
    FAUST compiler
    Copyright (C) 2019-2020 GRAME, Centre National de Creation Musicale
    ---------------------------------------------------------------------
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 ************************************************************************
 ************************************************************************/

#ifndef _FBC_EXECUTOR_H
#define _FBC_EXECUTOR_H

#include "faust/gui/CGlue.h"
#include "interpreter_bytecode.hh"

/*
 * The base class for Interpreter and mixed Interpreter/Compiler.
 */
template <class REAL>
struct FBCExecutor {
    
    virtual ~FBCExecutor() {}
    
    virtual void ExecuteBuildUserInterface(FIRUserInterfaceBlockInstruction<REAL>* block, UITemplate* glue) {};
    virtual void ExecuteBlock(FBCBlockInstruction<REAL>* block, bool compile = false) {};

    virtual void setIntValue(int offset, int value) {}
    virtual int  getIntValue(int offset) { return -1; }

    virtual void setInput(int offset, REAL* buffer) {}
    virtual void setOutput(int offset, REAL* buffer) {}

    virtual void updateInputControls() {}
    virtual void updateOutputControls() {}

    virtual void dumpMemory(FBCBlockInstruction<REAL>* block, const std::string& name, const std::string& filename) {}
    
};

/*
 * The base class for a Compiler to compile the hot 'compute' function.
 */
template <class REAL>
struct FBCExecuteFun {

    FBCExecuteFun() {}
    // The FBC block used in the'compute' function.
    FBCExecuteFun(FBCBlockInstruction<REAL>* fbc_block) {}
    virtual ~FBCExecuteFun() {}
   
    /*
     * The function to be executed each cycle.
     *
     * @param int_heap - the integer heap
     * @param real_heap - the REAL heap
     * @param inputs - the audio inputs
     * @param outputs - the audio outputs
     */
    virtual void Execute(int* int_heap, REAL* real_heap, REAL** inputs, REAL** outputs) {}
    
};

#endif

/************************** BEGIN cpp-dsp-adapter.h **************************/
/************************************************************************
 ************************************************************************
 Copyright (C) 2020 GRAME, Centre National de Creation Musicale
 
 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU Lesser General Public License as published by
 the Free Software Foundation; either version 2.1 of the License, or
 (at your option) any later version.
 
 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU Lesser General Public License for more details.
 
 You should have received a copy of the GNU Lesser General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 
 ************************************************************************
 ************************************************************************/

#ifndef CPP_mydsp_adapter_H
#define CPP_mydsp_adapter_H

#if defined(SOUNDFILE)
#include "faust/gui/SoundUI.h"
#endif

class dsp;

// Factory API
dsp* createmydsp();

#endif
/**************************  END  cpp-dsp-adapter.h **************************/

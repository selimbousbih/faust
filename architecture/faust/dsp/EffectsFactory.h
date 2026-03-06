/************************** BEGIN EffectsFactory.h ***********************
FAUST Architecture File
Copyright (C) 2003-2026 GRAME, Centre National de Creation Musicale
---------------------------------------------------------------------
This program is free software; you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as published by
the Free Software Foundation; either version 2.1 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.

EXCEPTION : As a special exception, you may create a larger work
that contains this FAUST architecture section and distribute
that work under terms of your choice, so long as this FAUST
architecture section is not modified.
************************************************************************/

#ifndef __EffectsFactory__
#define __EffectsFactory__

#include <algorithm>

#include "faust/dsp/dsp.h"

// Subclass EffectsFactory, override createEffect(), then install it with setFactory().
// The installed factory is not owned here and must outlive its use by the DSP engine.
class EffectsFactory {
    static EffectsFactory*& currentFactory()
    {
        static EffectsFactory* factory = nullptr;
        return factory;
    }

    static EffectsFactory& defaultFactory()
    {
        static EffectsFactory factory;
        return factory;
    }

   protected:
    dsp* createPassthroughDSP() const { return new PassthroughDSP(); }

   public:
    enum {
        EFFECT_ID_RECORDER = -2,
        EFFECT_ID_STEREO_DEPTH = -1,
    };

    virtual ~EffectsFactory() {}

    virtual dsp* createEffect(int) { return createPassthroughDSP(); }

    static void setFactory(EffectsFactory* factory) { currentFactory() = factory; }

    static EffectsFactory* getFactory()
    {
        return (currentFactory()) ? currentFactory() : &defaultFactory();
    }

    static dsp* create(int id) { return getFactory()->createEffect(id); }

   private:
    class PassthroughDSP : public dsp {
        int fSampleRate;

       public:
        PassthroughDSP() : fSampleRate(44100) {}

        int getNumInputs() { return 2; }
        int getNumOutputs() { return 2; }
        void buildUserInterface(UI*) {}
        int getSampleRate() { return fSampleRate; }
        void init(int sample_rate) { instanceInit(sample_rate); }
        void instanceInit(int sample_rate) { instanceConstants(sample_rate); }
        void instanceConstants(int sample_rate) { fSampleRate = sample_rate; }
        void instanceResetUserInterface() {}
        void instanceClear() {}
        PassthroughDSP* clone() { return new PassthroughDSP(*this); }
        void metadata(Meta*) {}

        void compute(int count, FAUSTFLOAT** inputs, FAUSTFLOAT** outputs)
        {
            for (int chan = 0; chan < getNumOutputs(); chan++) {
                if (inputs[chan] != outputs[chan]) {
                    std::copy(inputs[chan], inputs[chan] + count, outputs[chan]);
                }
            }
        }
    };
};

#endif  // __EffectsFactory__
/************************** END EffectsFactory.h **************************/

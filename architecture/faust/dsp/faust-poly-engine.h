/************************** BEGIN faust-poly-engine.h *******************
FAUST Architecture File
Copyright (C) 2003-2022 GRAME, Centre National de Creation Musicale
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

#ifndef __faust_poly_engine__
#define __faust_poly_engine__

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "faust/dsp/dsp.h"
#include "faust/audio/audio.h"
#include "faust/gui/meta.h"
#include "faust/gui/JSONUI.h"
#include "faust/gui/APIUI.h"
#include "faust/gui/MidiUI.h"
#include "faust/dsp/poly-dsp.h"
#include "faust/dsp/faust-engine.h"
#include "faust/dsp/dsp-combiner.h"

#include "faust/dsp/EffectsFactory.h"

#if SOUNDFILE
#include "faust/gui/SoundUI.h"
#endif

//**************************************************************
// Mono or polyphonic audio DSP engine
//**************************************************************

/**
 * @class dsp_summer
 * @brief Runs two independent DSP chains in parallel and sums their outputs.
 *
 * Both chains must produce the same number of output channels.
 * Inputs are routed only to dsp1 (dsp2 is a generator with 0 inputs).
 */
class dsp_summer : public dsp {

    dsp* fDSP1;
    dsp* fDSP2;
    int  fBufferSize;
    FAUSTFLOAT** fDSP1Outputs;
    FAUSTFLOAT** fDSP2Outputs;

public:

    dsp_summer(dsp* dsp1, dsp* dsp2, int buffer_size = 4096)
        : fDSP1(dsp1), fDSP2(dsp2), fBufferSize(buffer_size)
    {
        int nout = fDSP1->getNumOutputs();
        fDSP1Outputs = new FAUSTFLOAT*[nout];
        fDSP2Outputs = new FAUSTFLOAT*[nout];
        for (int c = 0; c < nout; c++) {
            fDSP1Outputs[c] = new FAUSTFLOAT[fBufferSize];
            fDSP2Outputs[c] = new FAUSTFLOAT[fBufferSize];
        }
    }

    virtual ~dsp_summer()
    {
        int nout = fDSP1->getNumOutputs();
        for (int c = 0; c < nout; c++) {
            delete[] fDSP1Outputs[c];
            delete[] fDSP2Outputs[c];
        }
        delete[] fDSP1Outputs;
        delete[] fDSP2Outputs;
        delete fDSP1;
        delete fDSP2;
    }

    virtual int getNumInputs()  { return fDSP1->getNumInputs(); }
    virtual int getNumOutputs() { return fDSP1->getNumOutputs(); }
    virtual int getSampleRate() { return fDSP1->getSampleRate(); }

    virtual void init(int sample_rate)
    {
        fDSP1->init(sample_rate);
        fDSP2->init(sample_rate);
    }
    virtual void instanceInit(int sample_rate)
    {
        fDSP1->instanceInit(sample_rate);
        fDSP2->instanceInit(sample_rate);
    }
    virtual void instanceConstants(int sample_rate)
    {
        fDSP1->instanceConstants(sample_rate);
        fDSP2->instanceConstants(sample_rate);
    }
    virtual void instanceResetUserInterface()
    {
        fDSP1->instanceResetUserInterface();
        fDSP2->instanceResetUserInterface();
    }
    virtual void instanceClear()
    {
        fDSP1->instanceClear();
        fDSP2->instanceClear();
    }
    virtual dsp* clone() { return nullptr; } // not needed here

    virtual void metadata(Meta* m)
    {
        fDSP1->metadata(m);
        fDSP2->metadata(m);
    }

    virtual void buildUserInterface(UI* ui_interface)
    {
        fDSP1->buildUserInterface(ui_interface);
        fDSP2->buildUserInterface(ui_interface);
    }

    virtual void compute(int count, FAUSTFLOAT** inputs, FAUSTFLOAT** outputs)
    {
        // Re-allocate temp buffers if count exceeds current buffer size
        if (count > fBufferSize) {
            int nout = fDSP1->getNumOutputs();
            for (int c = 0; c < nout; c++) {
                delete[] fDSP1Outputs[c];
                delete[] fDSP2Outputs[c];
                fDSP1Outputs[c] = new FAUSTFLOAT[count];
                fDSP2Outputs[c] = new FAUSTFLOAT[count];
            }
            fBufferSize = count;
        }
        fDSP1->compute(count, inputs, fDSP1Outputs);
        fDSP2->compute(count, nullptr, fDSP2Outputs);
        int nout = fDSP1->getNumOutputs();
        for (int c = 0; c < nout; c++) {
            for (int f = 0; f < count; f++) {
                outputs[c][f] = fDSP1Outputs[c][f] + fDSP2Outputs[c][f];
            }
        }
    }
    virtual void compute(double /*date_usec*/, int count, FAUSTFLOAT** inputs, FAUSTFLOAT** outputs)
    {
        compute(count, inputs, outputs);
    }
};

class FaustPolyEngine {
        
    protected:

        mydsp_poly* fPolyDSP;               // the polyphonic Faust object (main preset)
        dsp* fFinalDSP;                     // the "final" dsp object submitted to the audio driver
    
        APIUI fAPIUI;                       // UI descriptor for main preset

        // Sequencer preset — 1-voice instrument + its own effects chain
        mydsp_poly* fPolyDSPSequencer;      // sequencer preset poly object (1 voice)
        APIUI fAPIUISequencer;             // UI descriptor for sequencer preset
        bool fHasSequencerPreset;           // true when a sequencer preset is active

        std::string fJSONUI;
        std::string fJSONMeta;
        bool fRunning;
        audio* fDriver;
    
        midi_handler fMidiHandler;
        MidiUI fMidiUI;
    
        // Build the main preset chain up to (but not including) fFinalDSP submission.
        // Returns the chain dsp* (poly + fx). Also sets fPolyDSP and updates JSON/MIDI/API.
        // Does NOT call driver->init() — caller is responsible.
        dsp* buildMainPresetChain(std::vector<int> dsps, int polyCount)
        {
            bool midi_sync = false;
            bool midi = false;
            int nvoices = 0;

            dsp *mono_dsp = EffectsFactory::create(dsps[0]);
            for (int i = 1; i < polyCount; i++) {
                mono_dsp = new dsp_sequencer(mono_dsp, EffectsFactory::create(dsps[i]));
            }

            MidiMeta::analyse(mono_dsp, midi, midi_sync, nvoices);

            // Getting the UI JSON
            JSONUI jsonui1(mono_dsp->getNumInputs(), mono_dsp->getNumOutputs());
            mono_dsp->buildUserInterface(&jsonui1);
            fJSONUI = jsonui1.JSON();

            // Getting the metadata JSON
            JSONUI jsonui1M(mono_dsp->getNumInputs(), mono_dsp->getNumOutputs());
            mono_dsp->metadata(&jsonui1M);
            fJSONMeta = jsonui1M.JSON();

            fPolyDSP = new mydsp_poly(mono_dsp, nvoices, true);

            dsp *fxChain = EffectsFactory::create(polyCount);
            for (int i = polyCount; i < (int)dsps.size(); i++) {
                fxChain = new dsp_sequencer(fxChain, EffectsFactory::create(dsps[i]));
            }

            return new dsp_sequencer(fPolyDSP, fxChain);
        }

        // Build the sequencer preset chain (always 1 voice). Returns the chain dsp*.
        // Also sets fPolyDSPSequencer.
        dsp* buildSequencerPresetChain(std::vector<int> dsps, int polyCount)
        {
            dsp *mono_dsp = EffectsFactory::create(dsps[0]);
            for (int i = 1; i < polyCount; i++) {
                mono_dsp = new dsp_sequencer(mono_dsp, EffectsFactory::create(dsps[i]));
            }

            // Fixed 8 voices for the sequencer preset
            fPolyDSPSequencer = new mydsp_poly(mono_dsp, 8, true);

            dsp *fxChain = EffectsFactory::create(polyCount);
            for (int i = polyCount; i < (int)dsps.size(); i++) {
                fxChain = new dsp_sequencer(fxChain, EffectsFactory::create(dsps[i]));
            }

            return new dsp_sequencer(fPolyDSPSequencer, fxChain);
        }

        void finaliseDSP(dsp* chain, audio* driver, midi_handler* handler)
        {
            dsp* outputChain = chain;
            if (dsp* recorder = EffectsFactory::create(EffectsFactory::EFFECT_ID_RECORDER)) {
                outputChain = new dsp_sequencer(outputChain, recorder);
            }
            if (dsp* stereoDepth = EffectsFactory::create(EffectsFactory::EFFECT_ID_STEREO_DEPTH)) {
                outputChain = new dsp_sequencer(outputChain, stereoDepth);
            }

            fFinalDSP = outputChain;

            // Update JSONs with the full graph
            JSONUI jsonui2(0, fFinalDSP->getNumOutputs());
            fFinalDSP->buildUserInterface(&jsonui2);
            fJSONUI = jsonui2.JSON();

            JSONUI jsonui2M(0, fFinalDSP->getNumOutputs());
            fFinalDSP->metadata(&jsonui2M);
            fJSONMeta = jsonui2M.JSON();

            fFinalDSP->buildUserInterface(&fMidiUI);
            fFinalDSP->buildUserInterface(&fAPIUI);

#if SOUNDFILE
            buildUserInterface(new SoundUI(SoundUI::getBinaryPath()));
#endif

            // Retrieving DSP object name
            struct MyMeta : public Meta
            {
                std::string fName;
                void declare(const char* key, const char* value)
                {
                    if (strcmp(key, "name") == 0) fName = value;
                }
                MyMeta() : fName("Dummy") {}
            };

            MyMeta meta;
            fFinalDSP->metadata(&meta);
            if (handler) handler->setName(meta.fName);

            if (!driver->init(meta.fName.c_str(), fFinalDSP)) {
                delete fFinalDSP;
                fFinalDSP  = nullptr;
                fPolyDSP   = nullptr;
                fPolyDSPSequencer = nullptr;
                throw std::bad_alloc();
            } else {
                fDriver = driver;
            }
        }

        // Single-preset init (backward-compatible).
        void init(std::vector<int> dsps, audio* driver, midi_handler* handler, int polyCount)
        {
            fRunning = false;
            fHasSequencerPreset = false;
            fPolyDSPSequencer   = nullptr;

            dsp* chain = buildMainPresetChain(dsps, polyCount);
            finaliseDSP(chain, driver, handler);
        }
    
    
    public:

        /**
         * Legacy constructor used by DspFaust::init(dsp*, audio*).
         * Wraps the mono_dsp in a single-voice poly engine and initialises the
         * driver immediately, preserving the original behaviour.
         */
        FaustPolyEngine(dsp* mono_dsp, audio* driver, midi_handler* midi = nullptr)
            : fMidiUI(&fMidiHandler),
              fPolyDSP(nullptr), fFinalDSP(nullptr),
              fPolyDSPSequencer(nullptr), fHasSequencerPreset(false),
              fRunning(false), fDriver(driver)
        {
            assert(mono_dsp);
            bool midi_sync = false;
            bool midi_flag = false;
            int nvoices = 0;
            MidiMeta::analyse(mono_dsp, midi_flag, midi_sync, nvoices);

            JSONUI jsonui1(mono_dsp->getNumInputs(), mono_dsp->getNumOutputs());
            mono_dsp->buildUserInterface(&jsonui1);
            fJSONUI = jsonui1.JSON();

            JSONUI jsonui1M(mono_dsp->getNumInputs(), mono_dsp->getNumOutputs());
            mono_dsp->metadata(&jsonui1M);
            fJSONMeta = jsonui1M.JSON();

            fPolyDSP = new mydsp_poly(mono_dsp, nvoices, true);

            fFinalDSP = fPolyDSP;

            JSONUI jsonui2(mono_dsp->getNumInputs(), mono_dsp->getNumOutputs());
            fFinalDSP->buildUserInterface(&jsonui2);
            fJSONUI = jsonui2.JSON();

            JSONUI jsonui2M(mono_dsp->getNumInputs(), mono_dsp->getNumOutputs());
            fFinalDSP->metadata(&jsonui2M);
            fJSONMeta = jsonui2M.JSON();

            fFinalDSP->buildUserInterface(&fMidiUI);
            fFinalDSP->buildUserInterface(&fAPIUI);

            if (midi) {
                midi->setName("Faust");
            }

            if (!driver->init("Faust", fFinalDSP)) {
                fFinalDSP = nullptr;
                fPolyDSP  = nullptr;
                throw std::bad_alloc();
            }
        }

        /**
         * Default constructor — use refresh() to load a preset after construction.
         * Used when the engine is created before DSP content is known.
         */
        FaustPolyEngine(audio* driver = nullptr, midi_handler* midi = nullptr)
            : fMidiUI(&fMidiHandler),
              fPolyDSP(nullptr), fFinalDSP(nullptr),
              fPolyDSPSequencer(nullptr), fHasSequencerPreset(false),
              fRunning(false), fDriver(driver)
        {}
    
        virtual ~FaustPolyEngine()
        {
            delete fFinalDSP;
        }

        /*
         * start()
         * Begins the processing and return true if the connection
         * with the audio device was successful and false if not.
         */
        bool start()
        {
            if (!fRunning) {
                fRunning = fDriver->start();
            }
            return fRunning;
        }
    
        /*
         * isRunning()
         * Returns true if the DSP frames are being computed and
         * false if not.
         */
        bool isRunning() 
        {
            return fRunning;
        }

        /*
         * stop()
         * Stops the processing, closes the audio engine.
         */
        void stop()
        {
            if (fRunning) {
                fRunning = false;
                fDriver->stop();
            }
        }
    
        /*
         * keyOn(pitch, velocity)
         * Instantiates a new polyphonic voice where velocity
         * and pitch are MIDI numbers (0-127). keyOn can only
         * be used if nvoices > 0. keyOn will return 0 if the
         * object is not polyphonic and the allocated voice otherwise.
         */
        MapUI* keyOn(int pitch, int velocity)
        {
            if (fPolyDSP) {
                return fPolyDSP->keyOn(0, pitch, velocity); // MapUI* passed to Java as an integer
            } else {
                return 0;
            }
        }

        /*
         * keyOff(pitch)
         * De-instantiates a polyphonic voice where pitch is the
         * MIDI number of the note (0-127). keyOff can only be
         * used if nvoices > 0. keyOff will return 0 if the
         * object is not polyphonic and 1 otherwise.
         */
        int keyOff(int pitch, int velocity = 0)
        {
            if (fPolyDSP) {
                fPolyDSP->keyOff(0, pitch, velocity);
                return 1;
            } else {
                return 0;
            }
        }

        /*
         * newVoice()
         * Instantiate a new voice and returns the corresponding mapUI.
         */
        MapUI* newVoice()
        {
            if (fPolyDSP) {
                return fPolyDSP->newVoice();
            } else {
                return 0;
            }
        }

        /*
         * deleteVoice(MapUI* voice)
         * Delete a voice based on its MapUI*.
         */
        int deleteVoice(MapUI* voice)
        {
            if (fPolyDSP) {
                fPolyDSP->deleteVoice(voice);
                return 1;
            } else {
                return 0;
            }
        }

        /*
         * deleteVoice(uintptr_t voice)
         * Delete a voice based on its MapUI* casted as a uintptr_t.
         */
        int deleteVoice(uintptr_t voice)
        {
            return deleteVoice(reinterpret_cast<MapUI*>(voice));
        }
        
        /*
         * allNotesOff()
         * Terminates all the active voices, gently (with release when hard = false or immediately when hard = true)
         */
        void allNotesOff(bool hard = false)
        {
            if (fPolyDSP) {
                fPolyDSP->allNotesOff(hard);
            }
        }
    
        /*
         * Propagate MIDI data to the Faust object.
         */
        void propagateMidi(int count, double time, int type, int channel, int data1, int data2)
        {
            if (count == 3) fMidiHandler.handleData2(time, type, channel, data1, data2);
            else if (count == 2) fMidiHandler.handleData1(time, type, channel, data1);
            else if (count == 1) fMidiHandler.handleSync(time, type);
            // In POLY mode, update all voices
            GUI::updateAllGuis();
        }
    
        /*
         * getJSONUI()
         * Returns a string containing a JSON description of the
         * UI of the Faust object.
         */
        const char* getJSONUI()
        {
            return fJSONUI.c_str();
        }
        
        /*
         * getJSONMeta()
         * Returns a string containing a JSON description of the
         * metadata of the Faust object.
         */
        const char* getJSONMeta()
        {
            return fJSONMeta.c_str();
        }
    
        /*
         * buildUserInterface(UI* ui_interface)
         * Calls the polyphonic or monophonic buildUserInterface with the ui_interface parameter.
         */
        void buildUserInterface(UI* ui_interface)
        {
            fFinalDSP->buildUserInterface(ui_interface);
        }
    
        void compute(int count, FAUSTFLOAT** inputs, FAUSTFLOAT** outputs)
        {
            fFinalDSP->compute(count, inputs, outputs);
        }

        void setSampleRate(int sample_rate)
        {
            if (fFinalDSP && fFinalDSP->getSampleRate() != sample_rate) {
                fFinalDSP->init(sample_rate);
            }
        }

        int getNumInputs()
        {
            return fFinalDSP ? fFinalDSP->getNumInputs() : 0;
        }

        int getNumOutputs()
        {
            return fFinalDSP ? fFinalDSP->getNumOutputs() : 0;
        }

        /*
         * getParamsCount()
         * Returns the number of control parameters of the Faust object.
         */
        int getParamsCount()
        {
            return fAPIUI.getParamsCount();
        }
    
        /*
         * setParamValue(address, value)
         * Sets the value of the parameter associated with address.
         */
        void setParamValue(const char* address, float value)
        {
            fAPIUI.setParamValue(address, value);
            // In POLY mode, update all voices
            GUI::updateAllGuis();
        }
        
        /*
         * getParamValue(address)
         * Takes the address of a parameter and returns its current value.
         */
        float getParamValue(const char* address)
        {
            return fAPIUI.getParamValue(address);
        }
    
        /*
         * setParamValue(id, value)
         * Sets the value of the parameter associated with id.
         */
        void setParamValue(int id, float value)
        {
            fAPIUI.setParamValue(id, value);
            // In POLY mode, update all voices
            GUI::updateAllGuis();
        }
        
        /*
         * getParamValue(id)
         * Takes the id of a parameter and returns its current value.
         */
        float getParamValue(int id)
        {
            return fAPIUI.getParamValue(id);
        }

        /*
         * setVoiceParamValue(address, voice, value)
         * Sets the value of the parameter associated with address for
         * the voice. setVoiceParamValue can only be used if nvoices > 0.
         */
        void setVoiceParamValue(const char* address, uintptr_t voice, float value)
        {
            reinterpret_cast<MapUI*>(voice)->setParamValue(address, value);
        }

        /*
         * setVoiceParamValue(id, voice, value)
         * Sets the value of the parameter associated with the id for
         * the voice. setVoiceParamValue can only be used if nvoices > 0.
         */
        void setVoiceParamValue(int id, uintptr_t voice, float value)
        {
            reinterpret_cast<MapUI*>(voice)->setParamValue(reinterpret_cast<MapUI*>(voice)->getParamAddress(id), value);
        }
    
        /*
         * getVoiceParamValue(address, voice)
         * Gets the parameter value associated with address for the voice.
         * getVoiceParamValue can only be used if nvoices > 0.
         */
        float getVoiceParamValue(const char* address, uintptr_t voice)
        {
            return reinterpret_cast<MapUI*>(voice)->getParamValue(address);
        }

        /*
         * getVoiceParamValue(id, voice)
         * Gets the parameter value associated with the id for the voice.
         * getVoiceParamValue can only be used if nvoices > 0.
         */
        float getVoiceParamValue(int id, uintptr_t voice)
        {
            return reinterpret_cast<MapUI*>(voice)->getParamValue(reinterpret_cast<MapUI*>(voice)->getParamAddress(id));
        }
    
        /*
         * getParamLabel(id)
         * Returns the label of a parameter in function of its "id".
         */
        const char* getParamLabel(int id)
        {
            return fAPIUI.getParamLabel(id);
        }
    
        /*
         * getParamShortname(id)
         * Returns the shortname of a parameter in function of its "id".
         */
        const char* getParamShortname(int id)
        {
            return fAPIUI.getParamShortname(id);
        }

        /*
         * getParamAddress(id)
         * Returns the address of a parameter in function of its "id".
         */
        const char* getParamAddress(int id)
        {
            return fAPIUI.getParamAddress(id);
        }

        /*
         * getVoiceParamAddress(id, voice)
         * Returns the address of a parameter for a specific voice in function of its "id".
         */
        const char* getVoiceParamAddress(int id, uintptr_t voice)
        {
            return reinterpret_cast<MapUI*>(voice)->getParamAddress1(id);
        }
        
        /*
         * getParamMin(address)
         * Returns the minimum value of a parameter.
         */
        float getParamMin(const char* address)
        {
            int id = (address) ? fAPIUI.getParamIndex(address) : -1;
            return (id >= 0) ? fAPIUI.getParamMin(id) : 0.f;
        }
    
        /*
         * getParamMin(id)
         * Returns the minimum value of a parameter.
         */
        float getParamMin(int id)
        {
            return fAPIUI.getParamMin(id);
        }
    
        /*
         * getParamMax(address)
         * Returns the maximum value of a parameter.
         */
        float getParamMax(const char* address)
        {
            int id = (address) ? fAPIUI.getParamIndex(address) : -1;
            return (id >= 0) ? fAPIUI.getParamMax(id) : 0.f;
        }
    
        /*
         * getParamMax(id)
         * Returns the maximum value of a parameter.
         */
        float getParamMax(int id)
        {
            return fAPIUI.getParamMax(id);
        }
    
        /*
         * getParamInit(address)
         * Returns the default value of a parameter.
         */
        float getParamInit(const char* address)
        {
            int id = (address) ? fAPIUI.getParamIndex(address) : -1;
            return (id >= 0) ? fAPIUI.getParamInit(id) : 0.f;
        }
    
        /*
         * getParamInit(id)
         * Returns the default value of a parameter.
         */
        float getParamInit(int id)
        {
            return fAPIUI.getParamInit(id);
        }
    
        /*
         * getMetadata(address, key)
         * Returns the metadata of a parameter.
         */
        const char* getMetadata(const char* address, const char* key)
        {
            int id = (address) ? fAPIUI.getParamIndex(address) : -1;
            return (id >= 0) ? fAPIUI.getMetadata(id, key) : "";
        }
    
        /*
         * getMetadata(id, key)
         * Returns the metadata of a parameter.
         */
        const char* getMetadata(int id, const char* key)
        {
            return fAPIUI.getMetadata(id, key);
        }

        /*
         * propagateAcc(int acc, float v)
         * Propage accelerometer value to the curve conversion layer.
         */
        void propagateAcc(int acc, float v)
        {
            fAPIUI.propagateAcc(acc, v);
            // In POLY mode, update all voices
            GUI::updateAllGuis();
        }

        /*
         * setAccConverter(int p, int acc, int curve, float amin, float amid, float amax)
         * Change accelerometer curve mapping.
         */
        void setAccConverter(int p, int acc, int curve, float amin, float amid, float amax)
        {
            fAPIUI.setAccConverter(p, acc, curve, amin, amid, amax);
        }

        /*
         * propagateGyr(int gyr, float v)
         * Propage gyroscope value to the curve conversion layer.
         */
        void propagateGyr(int gyr, float v)
        {
            fAPIUI.propagateGyr(gyr, v);
            // In POLY mode, update all voices
            GUI::updateAllGuis();
        }

        /*
         * setGyrConverter(int p, int acc, int curve, float amin, float amid, float amax)
         * Change gyroscope curve mapping.
         */
        void setGyrConverter(int p, int gyr, int curve, float amin, float amid, float amax)
        {
            fAPIUI.setGyrConverter(p, gyr, curve, amin, amid, amax);
        }
    
        /*
         * getCPULoad()
         * Return DSP CPU load.
         */
        float getCPULoad() { return fDriver->getCPULoad(); }

        /*
         * getScreenColor()
         * Get the requested screen color.
         * -1 means no screen color control (no screencolor metadata found)
         * otherwise return 0x00RRGGBB a ready to use color
         */
        int getScreenColor()
        {
            return fAPIUI.getScreenColor();
        }

        // -----------------------------------------------------------------------
        // Refresh helpers
        // -----------------------------------------------------------------------

        void resetEngine()
        {
            fDriver->stop();
            fRunning = false;

            delete fFinalDSP;   // deletes the whole graph (owns all sub-DSPs)
            fFinalDSP         = nullptr;
            fPolyDSP          = nullptr;
            fPolyDSPSequencer = nullptr;

            fMidiUI.~MidiUI();
            new (&fMidiUI) MidiUI(&fMidiHandler);

            fAPIUI.~APIUI();
            new (&fAPIUI) APIUI();

            fAPIUISequencer.~APIUI();
            new (&fAPIUISequencer) APIUI();
        }

        /*
         * refresh(dsps, polyCount)
         * Hot-swap the DSP graph with a single preset (backward-compatible).
         */
        void refresh(std::vector<int> dsps, int polyCount)
        {
            resetEngine();
            init(dsps, fDriver, &fMidiHandler, polyCount);
        }

        /*
         * refresh(dsps, polyCount, seqDsps, seqPolyCount)
         * Hot-swap the DSP graph with two presets running in parallel:
         *   - Main preset : dsps[0..polyCount-1] instrument, rest = effects.
         *   - Sequencer preset: seqDsps[0..seqPolyCount-1] instrument (8 voices), rest = effects.
         * Both chains are summed into the audio output.
         */
        void refresh(std::vector<int> dsps,    int polyCount,
                     std::vector<int> seqDsps, int seqPolyCount)
        {
            if (seqDsps.size() == 0) {
                refresh(dsps, polyCount);
                return;
            }

            resetEngine();
            fRunning = false;
            fHasSequencerPreset = true;

            dsp* mainChain = buildMainPresetChain(dsps, polyCount);
            dsp* seqChain  = buildSequencerPresetChain(seqDsps, seqPolyCount);

            // Wire sequencer preset's APIUI before the chains are owned by dsp_summer
            seqChain->buildUserInterface(&fAPIUISequencer);

            dsp* combined = new dsp_summer(mainChain, seqChain);
            finaliseDSP(combined, fDriver, &fMidiHandler);
        }

        // -----------------------------------------------------------------------
        // Sequencer preset — voice management
        // -----------------------------------------------------------------------

        /*
         * newVoiceSequencer()
         * Allocate a new voice on the sequencer preset.
         */
        MapUI* newVoiceSequencer()
        {
            if (fHasSequencerPreset && fPolyDSPSequencer) {
                return fPolyDSPSequencer->newVoice();
            }
            return nullptr;
        }

        /*
         * deleteVoiceSequencer(voice)
         * Delete a voice from the sequencer preset.
         */
        int deleteVoiceSequencer(MapUI* voice)
        {
            if (fHasSequencerPreset && fPolyDSPSequencer) {
                fPolyDSPSequencer->deleteVoice(voice);
                return 1;
            }
            return 0;
        }

        int deleteVoiceSequencer(uintptr_t voice)
        {
            return deleteVoiceSequencer(reinterpret_cast<MapUI*>(voice));
        }

        /*
         * allNotesOffSequencer()
         * Stop all voices on the sequencer preset.
         */
        void allNotesOffSequencer(bool hard = false)
        {
            if (fHasSequencerPreset && fPolyDSPSequencer) {
                fPolyDSPSequencer->allNotesOff(hard);
            }
        }

        // -----------------------------------------------------------------------
        // Sequencer preset — per-voice parameter access
        // -----------------------------------------------------------------------

        /*
         * setVoiceParamValueSequencer(address, voice, value)
         * Sets the value of the parameter associated with address for
         * the given sequencer voice.
         */
        void setVoiceParamValueSequencer(const char* address, uintptr_t voice, float value)
        {
            reinterpret_cast<MapUI*>(voice)->setParamValue(address, value);
        }

        /*
         * getVoiceParamValueSequencer(address, voice)
         * Gets the parameter value associated with address for the given sequencer voice.
         */
        float getVoiceParamValueSequencer(const char* address, uintptr_t voice)
        {
            return reinterpret_cast<MapUI*>(voice)->getParamValue(address);
        }

        // -----------------------------------------------------------------------
        // Sequencer preset — parameter access (global / grouped)
        // -----------------------------------------------------------------------

        int getSequencerParamsCount()
        {
            return fAPIUISequencer.getParamsCount();
        }

        void setSequencerParamValue(const char* address, float value)
        {
            fAPIUISequencer.setParamValue(address, value);
            GUI::updateAllGuis();
        }

        float getSequencerParamValue(const char* address)
        {
            return fAPIUISequencer.getParamValue(address);
        }

        void setSequencerParamValue(int id, float value)
        {
            fAPIUISequencer.setParamValue(id, value);
            GUI::updateAllGuis();
        }

        float getSequencerParamValue(int id)
        {
            return fAPIUISequencer.getParamValue(id);
        }

        const char* getSequencerParamLabel(int id)
        {
            return fAPIUISequencer.getParamLabel(id);
        }

        const char* getSequencerParamAddress(int id)
        {
            return fAPIUISequencer.getParamAddress(id);
        }

        float getSequencerParamMin(const char* address)
        {
            int id = (address) ? fAPIUISequencer.getParamIndex(address) : -1;
            return (id >= 0) ? fAPIUISequencer.getParamMin(id) : 0.f;
        }

        float getSequencerParamMax(const char* address)
        {
            int id = (address) ? fAPIUISequencer.getParamIndex(address) : -1;
            return (id >= 0) ? fAPIUISequencer.getParamMax(id) : 0.f;
        }

        float getSequencerParamInit(const char* address)
        {
            int id = (address) ? fAPIUISequencer.getParamIndex(address) : -1;
            return (id >= 0) ? fAPIUISequencer.getParamInit(id) : 0.f;
        }

        bool hasSequencerPreset() const { return fHasSequencerPreset; }
};


// Public C API

#ifdef __cplusplus
extern "C" {
#endif
    
    void destroy(void* dsp) { delete reinterpret_cast<FaustPolyEngine*>(dsp); }

    bool start(void* dsp) { return reinterpret_cast<FaustPolyEngine*>(dsp)->start(); }
    void stop(void* dsp)  { reinterpret_cast<FaustPolyEngine*>(dsp)->stop(); }
    
    bool isRunning(void* dsp) { return reinterpret_cast<FaustPolyEngine*>(dsp)->isRunning(); }

    // --- Main preset ---
    uintptr_t keyOn(void* dsp, int pitch, int velocity)
        { return (uintptr_t)reinterpret_cast<FaustPolyEngine*>(dsp)->keyOn(pitch, velocity); }
    int keyOff(void* dsp, int pitch)
        { return reinterpret_cast<FaustPolyEngine*>(dsp)->keyOff(pitch); }
    
    void propagateMidi(void* dsp, int count, double time, int type, int channel, int data1, int data2)
    {
        reinterpret_cast<FaustPolyEngine*>(dsp)->propagateMidi(count, time, type, channel, data1, data2);
    }

    const char* getJSONUI(void* dsp)   { return reinterpret_cast<FaustPolyEngine*>(dsp)->getJSONUI(); }
    const char* getJSONMeta(void* dsp) { return reinterpret_cast<FaustPolyEngine*>(dsp)->getJSONMeta(); }

    int getParamsCount(void* dsp) { return reinterpret_cast<FaustPolyEngine*>(dsp)->getParamsCount(); }
    
    void setParamValue(void* dsp, const char* address, float value)
        { reinterpret_cast<FaustPolyEngine*>(dsp)->setParamValue(address, value); }
    float getParamValue(void* dsp, const char* address)
        { return reinterpret_cast<FaustPolyEngine*>(dsp)->getParamValue(address); }
   
    void setParamIdValue(void* dsp, int id, float value)
        { reinterpret_cast<FaustPolyEngine*>(dsp)->setParamValue(id, value); }
    float getParamIdValue(void* dsp, int id)
        { return reinterpret_cast<FaustPolyEngine*>(dsp)->getParamValue(id); }
    
    void setVoiceParamValue(void* dsp, const char* address, uintptr_t voice, float value)
        { reinterpret_cast<FaustPolyEngine*>(dsp)->setVoiceParamValue(address, voice, value); }
    float getVoiceParamValue(void* dsp, const char* address, uintptr_t voice)
        { return reinterpret_cast<FaustPolyEngine*>(dsp)->getVoiceParamValue(address, voice); }
    
    const char* getParamLabel(void* dsp, int id)     { return reinterpret_cast<FaustPolyEngine*>(dsp)->getParamLabel(id); }
    const char* getParamShortname(void* dsp, int id) { return reinterpret_cast<FaustPolyEngine*>(dsp)->getParamShortname(id); }
    const char* getParamAddress(void* dsp, int id)   { return reinterpret_cast<FaustPolyEngine*>(dsp)->getParamAddress(id); }

    void propagateAcc(void* dsp, int acc, float v) { reinterpret_cast<FaustPolyEngine*>(dsp)->propagateAcc(acc, v); }
    void setAccConverter(void* dsp, int p, int acc, int curve, float amin, float amid, float amax)
        { reinterpret_cast<FaustPolyEngine*>(dsp)->setAccConverter(p, acc, curve, amin, amid, amax); }
    void propagateGyr(void* dsp, int acc, float v) { reinterpret_cast<FaustPolyEngine*>(dsp)->propagateGyr(acc, v); }
    void setGyrConverter(void* dsp, int p, int gyr, int curve, float amin, float amid, float amax)
        { reinterpret_cast<FaustPolyEngine*>(dsp)->setGyrConverter(p, gyr, curve, amin, amid, amax); }

    float getCPULoad(void* dsp)    { return reinterpret_cast<FaustPolyEngine*>(dsp)->getCPULoad(); }
    int   getScreenColor(void* dsp) { return reinterpret_cast<FaustPolyEngine*>(dsp)->getScreenColor(); }

    // --- Sequencer preset ---

    bool hasSequencerPreset(void* dsp)
        { return reinterpret_cast<FaustPolyEngine*>(dsp)->hasSequencerPreset(); }

    uintptr_t newVoiceSequencer(void* dsp)
        { return (uintptr_t)reinterpret_cast<FaustPolyEngine*>(dsp)->newVoiceSequencer(); }
    int deleteVoiceSequencer(void* dsp, uintptr_t voice)
        { return reinterpret_cast<FaustPolyEngine*>(dsp)->deleteVoiceSequencer(voice); }
    void allNotesOffSequencer(void* dsp, bool hard)
        { reinterpret_cast<FaustPolyEngine*>(dsp)->allNotesOffSequencer(hard); }

    void setVoiceParamValueSequencer(void* dsp, const char* address, uintptr_t voice, float value)
        { reinterpret_cast<FaustPolyEngine*>(dsp)->setVoiceParamValueSequencer(address, voice, value); }
    float getVoiceParamValueSequencer(void* dsp, const char* address, uintptr_t voice)
        { return reinterpret_cast<FaustPolyEngine*>(dsp)->getVoiceParamValueSequencer(address, voice); }

    int getSequencerParamsCount(void* dsp)
        { return reinterpret_cast<FaustPolyEngine*>(dsp)->getSequencerParamsCount(); }

    void setSequencerParamValue(void* dsp, const char* address, float value)
        { reinterpret_cast<FaustPolyEngine*>(dsp)->setSequencerParamValue(address, value); }
    float getSequencerParamValue(void* dsp, const char* address)
        { return reinterpret_cast<FaustPolyEngine*>(dsp)->getSequencerParamValue(address); }

    void setSequencerParamIdValue(void* dsp, int id, float value)
        { reinterpret_cast<FaustPolyEngine*>(dsp)->setSequencerParamValue(id, value); }
    float getSequencerParamIdValue(void* dsp, int id)
        { return reinterpret_cast<FaustPolyEngine*>(dsp)->getSequencerParamValue(id); }

    const char* getSequencerParamLabel(void* dsp, int id)
        { return reinterpret_cast<FaustPolyEngine*>(dsp)->getSequencerParamLabel(id); }
    const char* getSequencerParamAddress(void* dsp, int id)
        { return reinterpret_cast<FaustPolyEngine*>(dsp)->getSequencerParamAddress(id); }
    
#ifdef __cplusplus
}
#endif

#endif // __faust_poly_engine__
/************************** END faust-poly-engine.h **************************/

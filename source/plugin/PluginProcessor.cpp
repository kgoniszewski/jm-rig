#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace jmrig
{

PluginProcessor::PluginProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::mono(),   true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "JMRigState", createParameterLayout())
{
    auto get = [this] (const char* id)
    {
        auto* p = apvts.getRawParameterValue (id);
        jassert (p != nullptr);
        return p;
    };

    raw.inputGain   = get (ParamIDs::inputGain);
    raw.outputGain  = get (ParamIDs::outputGain);
    raw.ampGain     = get (ParamIDs::ampGain);
    raw.ampBass     = get (ParamIDs::ampBass);
    raw.ampMid      = get (ParamIDs::ampMid);
    raw.ampTreble   = get (ParamIDs::ampTreble);
    raw.ampPresence = get (ParamIDs::ampPresence);
    raw.ampMaster   = get (ParamIDs::ampMaster);
    raw.ampBright   = get (ParamIDs::ampBright);
    raw.cabOn       = get (ParamIDs::cabOn);
    raw.cabBlend    = get (ParamIDs::cabBlend);
    raw.cabLowCut   = get (ParamIDs::cabLowCut);
    raw.cabHighCut  = get (ParamIDs::cabHighCut);
    raw.bypass      = get (ParamIDs::bypass);
}

//==============================================================================
void PluginProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    maxBlock = juce::jmax (1, samplesPerBlock);
    monoScratch.allocate ((size_t) maxBlock, true);

    // Set targets first so prepare() starts the smoothers at the current values.
    engine.setParameters (readParameters (false));
    engine.prepare (sampleRate, maxBlock);

    setLatencySamples (engine.getLatencySamples());
}

void PluginProcessor::releaseResources() {}

void PluginProcessor::reset()
{
    engine.reset();
}

bool PluginProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();

    const auto monoOrStereo = [] (const juce::AudioChannelSet& s)
    {
        return s == juce::AudioChannelSet::mono() || s == juce::AudioChannelSet::stereo();
    };

    return monoOrStereo (in) && monoOrStereo (out);
}

//==============================================================================
RigParameters PluginProcessor::readParameters (bool forceBypass) const noexcept
{
    const auto load = [] (const std::atomic<float>* p) { return p->load (std::memory_order_relaxed); };

    RigParameters p;
    p.inputGainDb  = load (raw.inputGain);
    p.outputGainDb = load (raw.outputGain);
    p.ampGain      = load (raw.ampGain);
    p.ampBass      = load (raw.ampBass);
    p.ampMid       = load (raw.ampMid);
    p.ampTreble    = load (raw.ampTreble);
    p.ampPresence  = load (raw.ampPresence);
    p.ampMaster    = load (raw.ampMaster);
    p.ampBright    = load (raw.ampBright) >= 0.5f;
    p.cabOn        = load (raw.cabOn) >= 0.5f;
    p.cabBlend     = load (raw.cabBlend);
    p.cabLowCutHz  = load (raw.cabLowCut);
    p.cabHighCutHz = load (raw.cabHighCut);
    p.bypass       = forceBypass || load (raw.bypass) >= 0.5f;
    return p;
}

void PluginProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    render (buffer, false);
}

void PluginProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // Keep running the engine so the dry signal stays latency-aligned and
    // un-bypassing fades in rather than clicking.
    render (buffer, true);
}

void PluginProcessor::render (juce::AudioBuffer<float>& buffer, bool forceBypass) noexcept
{
    juce::ScopedNoDenormals noDenormals;

    const auto numSamples = buffer.getNumSamples();
    const auto numIn  = getTotalNumInputChannels();
    const auto numOut = getTotalNumOutputChannels();

    if (numSamples <= 0 || numOut <= 0)
        return;

    if (maxBlock <= 0 || numIn <= 0)
    {
        buffer.clear();
        return;
    }

    engine.setParameters (readParameters (forceBypass));

    // The guitar is on input 1. A stereo input bus is not summed: on most
    // interfaces input 2 is another source, and summing would cost 6 dB.
    for (int offset = 0; offset < numSamples; offset += maxBlock)
    {
        const auto n = juce::jmin (maxBlock, numSamples - offset);

        juce::FloatVectorOperations::copy (monoScratch.get(), buffer.getReadPointer (0, offset), n);
        engine.process (monoScratch.get(), n);

        for (int ch = 0; ch < numOut; ++ch)
            juce::FloatVectorOperations::copy (buffer.getWritePointer (ch, offset), monoScratch.get(), n);
    }

    for (int ch = numOut; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);
}

juce::AudioProcessorParameter* PluginProcessor::getBypassParameter() const
{
    return apvts.getParameter (ParamIDs::bypass);
}

//==============================================================================
juce::AudioProcessorEditor* PluginProcessor::createEditor()
{
    return new PluginEditor (*this);
}

namespace
{
    const juce::Identifier irsTag ("CabIRs"), irTag ("IR"), slotProp ("slot"), nameProp ("name"), dataProp ("data");

    const char* slotKey (CabStage::Slot s) { return s == CabStage::Slot::a ? "a" : "b"; }
}

void PluginProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.removeChild (state.getChildWithName (irsTag), nullptr);
    state.appendChild (createIrState(), nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void PluginProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    auto state = juce::ValueTree::fromXml (*xml);
    const auto irs = state.getChildWithName (irsTag);
    state.removeChild (irs, nullptr);

    apvts.replaceState (state);
    restoreIrState (irs);
}

//==============================================================================
juce::String PluginProcessor::loadImpulseResponse (IrSlot slot, juce::MemoryBlock fileData, const juce::String& name)
{
    auto decoded = decoder.decode (fileData);

    if (! decoded.ok())
        return decoded.error;

    engine.loadImpulseResponse (slot, std::move (decoded.impulse), decoded.sampleRate);

    const juce::ScopedLock sl (irLock);
    stored (slot) = { name, std::move (fileData) };
    return {};
}

void PluginProcessor::clearImpulseResponse (IrSlot slot)
{
    engine.loadDefaultImpulseResponse (slot);

    const juce::ScopedLock sl (irLock);
    stored (slot) = {};
}

juce::String PluginProcessor::getImpulseResponseName (IrSlot slot) const
{
    const juce::ScopedLock sl (irLock);
    return stored (slot).name;
}

juce::ValueTree PluginProcessor::createIrState() const
{
    juce::ValueTree irs (irsTag);
    const juce::ScopedLock sl (irLock);

    for (auto slot : { IrSlot::a, IrSlot::b })
    {
        const auto& ir = stored (slot);

        if (ir.data.isEmpty())
            continue;

        irs.appendChild (juce::ValueTree (irTag, { { slotProp, slotKey (slot) },
                                                   { nameProp, ir.name },
                                                   { dataProp, juce::var (ir.data) } }),
                         nullptr);
    }

    return irs;
}

void PluginProcessor::restoreIrState (const juce::ValueTree& irs)
{
    for (auto slot : { IrSlot::a, IrSlot::b })
    {
        const auto ir = irs.getChildWithProperty (slotProp, slotKey (slot));
        const auto* block = ir.isValid() ? ir[dataProp].getBinaryData() : nullptr;

        if (block == nullptr || ! loadImpulseResponse (slot, *block, ir[nameProp].toString()).isEmpty())
            clearImpulseResponse (slot);
    }
}

} // namespace jmrig

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new jmrig::PluginProcessor();
}

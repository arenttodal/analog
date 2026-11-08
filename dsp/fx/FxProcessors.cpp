#include "Effects.h"

namespace andor::analog::fx
{

std::unique_ptr<Processor> makeProcessor (Type type)
{
    switch (type)
    {
        case Type::spread: return std::make_unique<StereoSpread>();
        case Type::compressor: return std::make_unique<Compressor>();
        case Type::eq: return std::make_unique<Equalizer>();
        case Type::filter: return std::make_unique<FxFilter>();
        case Type::phaser: return std::make_unique<Phaser>();
        case Type::flanger: return std::make_unique<Flanger>();
        case Type::shifter: return std::make_unique<FrequencyShifter>();
        case Type::convolution: return std::make_unique<Convolution>();
        case Type::utility: return std::make_unique<Utility>();
        default: return {};   // (the original four are the instrument's own: PostProcessor)
    }
}

bool implemented (Type type) noexcept
{
    return isLegacy (type) || makeProcessor (type) != nullptr;
}

bool isOn (const Settings& s, InstanceId id) noexcept
{
    const auto slot = static_cast<std::size_t> (id.slot);
    switch (id.type)
    {
        case Type::spread: return s.spread[slot].on;
        case Type::compressor: return s.comp[slot].on;
        case Type::eq: return s.eq[slot].on;
        case Type::filter: return s.filter[slot].on;
        case Type::phaser: return s.phaser[slot].on;
        case Type::flanger: return s.flanger[slot].on;
        case Type::shifter: return s.shifter[slot].on;
        case Type::convolution: return s.conv[slot].on;
        case Type::utility: return s.utility[slot].on;
        default: return true;
    }
}

} // namespace andor::analog::fx

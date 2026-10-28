#include "Wav.h"

#include <cstdint>
#include <cstdio>

namespace andor::analog::wav
{

namespace
{
    void u32 (std::FILE* f, std::uint32_t v)
    {
        const unsigned char b[4] = { static_cast<unsigned char> (v), static_cast<unsigned char> (v >> 8), static_cast<unsigned char> (v >> 16),
                                     static_cast<unsigned char> (v >> 24) };
        std::fwrite (b, 1, 4, f);
    }
    void u16 (std::FILE* f, std::uint16_t v)
    {
        const unsigned char b[2] = { static_cast<unsigned char> (v), static_cast<unsigned char> (v >> 8) };
        std::fwrite (b, 1, 2, f);
    }
}

bool writeStereo (const std::string& path, const std::vector<float>& left, const std::vector<float>& right, double sampleRate)
{
    std::FILE* f = std::fopen (path.c_str(), "wb");
    if (f == nullptr)
        return false;
    const auto frames = static_cast<std::uint32_t> (left.size() < right.size() ? left.size() : right.size());
    const std::uint32_t dataBytes = frames * 2u * 4u;
    std::fwrite ("RIFF", 1, 4, f);
    u32 (f, 36u + dataBytes);
    std::fwrite ("WAVEfmt ", 1, 8, f);
    u32 (f, 16);
    u16 (f, 3);   // IEEE float
    u16 (f, 2);
    u32 (f, static_cast<std::uint32_t> (sampleRate));
    u32 (f, static_cast<std::uint32_t> (sampleRate) * 8u);
    u16 (f, 8);
    u16 (f, 32);
    std::fwrite ("data", 1, 4, f);
    u32 (f, dataBytes);
    for (std::uint32_t i = 0; i < frames; ++i)
    {
        std::fwrite (&left[i], 4, 1, f);
        std::fwrite (&right[i], 4, 1, f);
    }
    return std::fclose (f) == 0;
}

} // namespace andor::analog::wav

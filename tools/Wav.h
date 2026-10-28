#pragma once

#include <string>
#include <vector>

namespace andor::analog::wav
{

/** Writes interleaved-free stereo float audio as a 32-bit float WAV file. */
bool writeStereo (const std::string& path, const std::vector<float>& left, const std::vector<float>& right, double sampleRate);

} // namespace andor::analog::wav

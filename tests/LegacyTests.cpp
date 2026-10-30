// The sounds that exist must not change: every factory sound and a set of edge patches
// (sync, Poly Mod, unison, GRANULAR, REIMAGINED, the drifting voices) rendered at three
// sample rates, compared with the renders stored before the oscillator models, the OTA
// filter, the chorus, ORGANIC and MORPH arrived (tests/golden/legacy-renders.txt).
//
// Exact hashes are compared on the platform that stored them (other compilers round
// differently: osp::platformTag); everywhere a coarse loudness contour (16 RMS windows,
// 0.1 % tolerance) is compared too. OSP_UPDATE_GOLDEN=1 rewrites the file (only for an
// intentional change of the legacy sound, explained in the commit).

#include "FactoryPatches.h"
#include "TestUtil.h"

#include "core/PlatformTag.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

using namespace testutil;

namespace
{
    struct Case
    {
        std::string name;
        Patch patch;
        osp::keydna::Settings keyDna;
        std::vector<Event> events;
    };

    std::vector<Event> phrase()
    {
        // A chord, a repeated note, a line over it, bend and wheel, everything released.
        return { on (0, 48, 100), on (0, 55, 90), on (0, 64, 70), on (4800, 72, 120), off (9600, 72), on (12000, 74, 50),
                 bendTo (14000, 12000), cc (16000, 1, 90), off (24000, 74), off (26000, 48), off (26000, 55), off (26000, 64) };
    }

    std::vector<Case> cases()
    {
        std::vector<Case> list;
        for (int i = 0; i < legacyFactoryCount; ++i)
        {
            const auto& f = factoryPatches()[static_cast<std::size_t> (i)];
            list.push_back ({ "factory/" + f.name, f.patch, keyDnaOf (f), phrase() });
        }
        auto edge = [&list] (const std::string& name, auto&& change) {
            Patch p;
            change (p);
            list.push_back ({ "edge/" + name, p, {}, phrase() });
        };
        edge ("init", [] (Patch&) {});
        edge ("sync-polymod", [] (Patch& p) {
            p.sync = true;
            p.polyModEnv = 0.7;
            p.polyModOsc2 = 0.5;
            p.polyModToPitch1 = p.polyModToWidth1 = p.polyModToCutoff = true;
            p.osc1Wave = Osc1Wave::sawPulse;
            p.osc2Wave = Osc2Wave::all;
        });
        edge ("unison-mono-glide", [] (Patch& p) {
            p.unison = true;
            p.glideSeconds = 0.08;
            p.resonance = 0.8;
        });
        edge ("granular", [] (Patch& p) {
            p.granular = true;
            p.grains.position = 0.4;
            p.grains.spread = 0.6;
        });
        edge ("kaleidoscope", [] (Patch& p) {
            p.reimagined = 0.7;
            p.reimaginedSettings.mode = osp::ReimaginedMode::kaleidoscope;
        });
        edge ("analog-full-noise", [] (Patch& p) {
            p.analogAmount = 1.0;
            p.noiseLevel = 0.4;
            p.filterDrive = 0.6;
            p.driveBias = 0.5;
            p.phaseMode = PhaseMode::reset;
        });
        return list;
    }

    struct Fingerprint
    {
        std::uint64_t hash = 0;
        std::array<double, 16> rms {};
    };

    Fingerprint fingerprintOf (const Case& c, double rate)
    {
        auto s = makeSynth (c.patch, rate);
        if (c.keyDna.enabled)
            s->setKeyDna (c.keyDna);
        auto events = c.events;
        for (auto& e : events)   // (the phrase is written at 48 kHz)
            e.sample = static_cast<int> (std::llround (e.sample * rate / 48000.0));
        const auto out = render (*s, 0.75, events, 173);
        Fingerprint f;
        std::uint64_t h = 1469598103934665603ull;   // FNV-1a over the samples' bits
        for (std::size_t i = 0; i < out.left.size(); ++i)
            for (const float x : { out.left[i], out.right[i] })
            {
                std::uint32_t bits;
                std::memcpy (&bits, &x, sizeof bits);
                h = (h ^ bits) * 1099511628211ull;
            }
        f.hash = h;
        for (std::size_t w = 0; w < f.rms.size(); ++w)
            f.rms[w] = out.rms (0.75 * static_cast<double> (w) / 16.0, 0.75 * static_cast<double> (w + 1) / 16.0);
        return f;
    }

    std::string goldenPath() { return std::string (ANALOG_TEST_DIR) + "/golden/legacy-renders.txt"; }
}

TEST_CASE ("legacy: every factory sound and edge patch renders as it did at state version 3", "[legacy][regression]")
{
    const double rates[] = { 44100.0, 48000.0, 96000.0 };
    const auto platform = osp::platformTag();
    std::map<std::string, std::string> stored;   // key -> line
    {
        std::ifstream in (goldenPath());
        for (std::string line; std::getline (in, line);)
        {
            std::istringstream s (line);
            std::string kind, key;
            s >> kind >> key;
            if (! key.empty())
                stored[kind + " " + key] = line;
        }
    }
    const bool update = std::getenv ("OSP_UPDATE_GOLDEN") != nullptr;
    std::ostringstream written;
    int hashesCompared = 0;
    for (const auto& c : cases())
        for (const double rate : rates)
        {
            auto key = c.name + "@" + std::to_string (static_cast<int> (rate));
            std::replace (key.begin(), key.end(), ' ', '_');
            INFO (key);
            const auto f = fingerprintOf (c, rate);
            std::ostringstream contour;
            contour.precision (9);
            for (const double r : f.rms)
                contour << ' ' << r;
            if (update)
            {
                written << "rms " << key << contour.str() << '\n';
                // Keep other platforms' hashes, replace this one's.
                for (const auto& [k, line] : stored)
                    if (k.rfind ("hash:", 0) == 0 && k.find (" " + key) != std::string::npos && k.rfind ("hash:" + platform + " ", 0) != 0)
                        written << line << '\n';
                written << "hash:" << platform << ' ' << key << ' ' << f.hash << '\n';
                continue;
            }
            const auto rmsLine = stored.find ("rms " + key);
            REQUIRE (rmsLine != stored.end());
            std::istringstream s (rmsLine->second);
            std::string kind, k;
            s >> kind >> k;
            for (const double r : f.rms)
            {
                double expected = 0.0;
                s >> expected;
                CHECK (std::abs (r - expected) <= 1.0e-3 * std::max (expected, 1.0e-4));
            }
            if (const auto hashLine = stored.find ("hash:" + platform + " " + key); hashLine != stored.end())
            {
                std::istringstream hs (hashLine->second);
                std::uint64_t expected = 0;
                hs >> kind >> k >> expected;
                CHECK (f.hash == expected);
                ++hashesCompared;
            }
        }
    if (update)
    {
        std::ofstream (goldenPath()) << written.str();
        WARN ("legacy renders rewritten: " << goldenPath());
    }
    else if (hashesCompared == 0)
        WARN ("no exact hashes stored for " << platform << ": loudness contours compared only");
}

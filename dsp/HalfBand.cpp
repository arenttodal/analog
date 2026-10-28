#include "HalfBand.h"

#include <algorithm>
#include <cmath>

namespace andor::analog
{

namespace
{
    double besselI0 (double x)
    {
        double sum = 1.0, term = 1.0;
        for (int k = 1; k < 64; ++k)
        {
            term *= (x / (2.0 * k)) * (x / (2.0 * k));
            sum += term;
            if (term < 1.0e-14 * sum)
                break;
        }
        return sum;
    }
}

void HalfBandDecimator::design (double transition, double attenuationDb)
{
    // Kaiser's estimates for the window's length and shape.
    const double a = std::max (attenuationDb, 21.0);
    const double beta = a > 50.0 ? 0.1102 * (a - 8.7) : 0.5842 * std::pow (a - 21.0, 0.4) + 0.07886 * (a - 21.0);
    int n = static_cast<int> (std::ceil ((a - 8.0) / (2.285 * 2.0 * 3.14159265358979323846 * transition))) + 1;
    // A half-band filter's length is 4m + 3 (odd, and its outermost taps non-zero).
    while ((n - 3) % 4 != 0)
        ++n;
    taps = n;
    const int mid = (taps - 1) / 2;
    std::vector<double> h (static_cast<std::size_t> (taps), 0.0);
    double sum = 0.0;
    for (int i = 0; i < taps; ++i)
    {
        const int k = i - mid;
        const double sinc = k == 0 ? 0.5 : std::sin (0.5 * 3.14159265358979323846 * k) / (3.14159265358979323846 * k);
        const double r = static_cast<double> (k) / mid;
        const double w = besselI0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / besselI0 (beta);
        h[static_cast<std::size_t> (i)] = (k % 2 == 0 && k != 0) ? 0.0 : sinc * w;
        sum += h[static_cast<std::size_t> (i)];
    }
    for (auto& v : h)
        v /= sum;   // unity gain at DC
    centre = static_cast<float> (h[static_cast<std::size_t> (mid)]);
    odd.clear();
    for (int k = 1; k <= mid; k += 2)
        odd.push_back (static_cast<float> (h[static_cast<std::size_t> (mid + k)]));
    history.assign (static_cast<std::size_t> (2 * taps), 0.0f);
    write = 0;
}

void HalfBandDecimator::reset() noexcept
{
    std::fill (history.begin(), history.end(), 0.0f);
    write = 0;
}

float HalfBandDecimator::process (float a, float b) noexcept
{
    auto push = [this] (float x) {
        history[static_cast<std::size_t> (write)] = x;
        history[static_cast<std::size_t> (write + taps)] = x;
        write = write + 1 == taps ? 0 : write + 1;
    };
    push (a);
    push (b);
    // history[write .. write + taps - 1] is the window, oldest first.
    const float* x = history.data() + write;
    const int mid = (taps - 1) / 2;
    float y = centre * x[mid];
    const int pairs = static_cast<int> (odd.size());
    for (int j = 0; j < pairs; ++j)
    {
        const int k = 2 * j + 1;
        y += odd[static_cast<std::size_t> (j)] * (x[mid - k] + x[mid + k]);
    }
    return y;
}

std::vector<double> HalfBandDecimator::impulseResponse() const
{
    const int mid = (taps - 1) / 2;
    std::vector<double> h (static_cast<std::size_t> (taps), 0.0);
    h[static_cast<std::size_t> (mid)] = centre;
    for (std::size_t j = 0; j < odd.size(); ++j)
    {
        const int k = 2 * static_cast<int> (j) + 1;
        h[static_cast<std::size_t> (mid + k)] = h[static_cast<std::size_t> (mid - k)] = odd[j];
    }
    return h;
}

} // namespace andor::analog

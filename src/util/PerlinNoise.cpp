// SPDX-License-Identifier: MIT

#include "util/PerlinNoise.h"

#include <algorithm>
#include <cmath>

PerlinNoise::PerlinNoise(unsigned seed)
{
    std::vector<int> permutation(256);
    std::iota(permutation.begin(), permutation.end(), 0);

    std::default_random_engine engine(seed);
    std::shuffle(permutation.begin(), permutation.end(), engine);

    for (int i = 0; i < 256; ++i) {
        m_permutation[i] = permutation[i];
        m_permutation[256 + i] = permutation[i];
    }
}

double PerlinNoise::noise(double x, double y, double z) const
{
    const int X = static_cast<int>(std::floor(x)) & 255;
    const int Y = static_cast<int>(std::floor(y)) & 255;
    const int Z = static_cast<int>(std::floor(z)) & 255;

    x -= std::floor(x);
    y -= std::floor(y);
    z -= std::floor(z);

    const double u = fade(x);
    const double v = fade(y);
    const double w = fade(z);

    const int A = m_permutation[X] + Y;
    const int AA = m_permutation[A] + Z;
    const int AB = m_permutation[A + 1] + Z;
    const int B = m_permutation[X + 1] + Y;
    const int BA = m_permutation[B] + Z;
    const int BB = m_permutation[B + 1] + Z;

    return lerp(w,
        lerp(v,
            lerp(u, grad(m_permutation[AA], x, y, z), grad(m_permutation[BA], x - 1, y, z)),
            lerp(u, grad(m_permutation[AB], x, y - 1, z), grad(m_permutation[BB], x - 1, y - 1, z))),
        lerp(v,
            lerp(u, grad(m_permutation[AA + 1], x, y, z - 1), grad(m_permutation[BA + 1], x - 1, y, z - 1)),
            lerp(u, grad(m_permutation[AB + 1], x, y - 1, z - 1), grad(m_permutation[BB + 1], x - 1, y - 1, z - 1))));
}

double PerlinNoise::octaveNoise(double x, double y, double z, int octaves, double persistence) const
{
    double total = 0.0;
    double frequency = 1.0;
    double amplitude = 1.0;
    double maxValue = 0.0;

    for (int i = 0; i < octaves; ++i) {
        total += noise(x * frequency, y * frequency, z * frequency) * amplitude;
        maxValue += amplitude;
        amplitude *= persistence;
        frequency *= 2.0;
    }

    return total / maxValue;
}

double PerlinNoise::fade(double t)
{
    return t * t * t * (t * (t * 6 - 15) + 10);
}

double PerlinNoise::lerp(double t, double a, double b)
{
    return a + t * (b - a);
}

double PerlinNoise::grad(int hash, double x, double y, double z)
{
    const int h = hash & 15;
    const double u = h < 8 ? x : y;
    const double v = h < 4 ? y : (h == 12 || h == 14 ? x : z);
    return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
}

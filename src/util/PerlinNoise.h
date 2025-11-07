// SPDX-License-Identifier: MIT
#pragma once

#include <array>
#include <random>
#include <vector>

class PerlinNoise {
public:
    explicit PerlinNoise(unsigned seed = std::default_random_engine::default_seed);

    double noise(double x, double y, double z) const;
    double octaveNoise(double x, double y, double z, int octaves, double persistence) const;

private:
    static double fade(double t);
    static double lerp(double t, double a, double b);
    static double grad(int hash, double x, double y, double z);

    std::array<int, 512> m_permutation {};
};

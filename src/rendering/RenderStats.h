// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>

struct RenderStats {
    std::uint64_t drawCalls { 0 };
    std::uint64_t triangles { 0 };

    void reset()
    {
        drawCalls = 0;
        triangles = 0;
    }

    void addDraw(std::uint64_t drawCallCount, std::uint64_t triangleCount)
    {
        drawCalls += drawCallCount;
        triangles += triangleCount;
    }

    void addDraw(std::uint64_t triangleCount)
    {
        addDraw(1, triangleCount);
    }
};

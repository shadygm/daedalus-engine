#pragma once
#include <filesystem>
#include <optional>

[[nodiscard]] std::optional<std::filesystem::path> pickSaveFile(const char* pFilterList = nullptr);
[[nodiscard]] std::optional<std::filesystem::path> pickOpenFile(const char* pFilterList = nullptr);

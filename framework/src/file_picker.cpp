#include "file_picker.h"
// Suppress warnings in third-party code.
#include <framework/disable_all_warnings.h>
DISABLE_WARNINGS_PUSH()
#include <nativefiledialog/nfd.h>
DISABLE_WARNINGS_POP()
#include <iostream>

std::optional<std::filesystem::path> pickSaveFile(const char* pFilterList)
{
    nfdchar_t* pOutPath = nullptr;
    const nfdresult_t result = NFD_SaveDialog(pFilterList, nullptr, &pOutPath);

    if (result == NFD_OKAY) {
        std::filesystem::path outPath { pOutPath };
        free(pOutPath);
        return outPath;
    } else if (result != NFD_CANCEL) {
        std::cerr << "Native file dialog error: " <<  NFD_GetError() << std::endl;
    }

    return {};
}

std::optional<std::filesystem::path> pickOpenFile(const char* pFilterList)
{
    nfdchar_t* pOutPath = nullptr;
    const nfdresult_t result = NFD_OpenDialog(pFilterList, nullptr, &pOutPath);

    if (result == NFD_OKAY) {
        std::filesystem::path outPath { pOutPath };
        free(pOutPath);
        return outPath;
    } else if (result != NFD_CANCEL) {
        std::cerr << "Native file dialog error: " <<  NFD_GetError() << std::endl;
    }

    return {};
}

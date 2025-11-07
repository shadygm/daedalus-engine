#pragma once
#include <framework/disable_all_warnings.h>
DISABLE_WARNINGS_PUSH()
#include <glm/vec3.hpp>
#include <imgui/imgui.h>
DISABLE_WARNINGS_POP()
#include <framework/trackball.h>
#include <framework/window.h>
#include <vector>

struct ImGuiDynamicArraySettings {
    bool allowEmptyArray { false };
    bool showReorder { true };
    bool showAddButton { true };
    bool showRemoveButton { true };
};

template <typename T, typename F>
inline void drawImGuiDynamicArray(std::vector<T>& items, F&& itemUI, ImGuiDynamicArraySettings settings = {})
{
    if (settings.showAddButton) {
        if (ImGui::Button("Add")) {
            if (items.empty())
                items.emplace_back();
            else
                items.push_back(items.back());
        }
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
    }

    ImGui::BeginChild(ImGui::GetID((void*)(intptr_t)123), ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);
    static int selectedIndex = -1;
    int eraseIndex = -1;
    bool anyActive = false;
    for (int index = 0; index < items.size(); ++index) {
        auto& item = items[index];
        ImGui::PushID(index);

        if (settings.showReorder) {
            ImGui::Selectable("DRAG TO REORDER");
            // Start drag-and-drop.
            if (selectedIndex == -1 && ImGui::IsItemActive()) {
                selectedIndex = index;
            }
            // End drag-and-drop.
            anyActive |= ImGui::IsItemActive();

            // Reorder items while drag-dropping.
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) && selectedIndex >= 0 && selectedIndex != index) {
                std::swap(items[selectedIndex], items[index]);
                selectedIndex = index;
            }

            // Draw item being dragged using red text.
            if (index == selectedIndex)
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
        }

        itemUI(item);

        if (settings.showRemoveButton) {
            ImGui::Spacing();
            ImGui::BeginDisabled(!settings.allowEmptyArray && items.size() == 1);
            if (ImGui::Button("Remove"))
                eraseIndex = index; // Don't erase while we are still iterating over the items.
            ImGui::EndDisabled();
        }

        ImGui::Spacing();
        if (index != items.size() - 1) {
            ImGui::Separator();
            ImGui::Spacing();
        }

        // Disable colored text.
        if (settings.showReorder && index == selectedIndex)
            ImGui::PopStyleColor();

        ImGui::PopID();
    }

    if (!anyActive)
        selectedIndex = -1;
    if (eraseIndex >= 0)
        items.erase(std::begin(items) + eraseIndex);

    ImGui::EndChild();
}

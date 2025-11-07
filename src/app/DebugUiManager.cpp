// SPDX-License-Identifier: MIT

#include "app/DebugUiManager.h"

#include <framework/disable_all_warnings.h>
DISABLE_WARNINGS_PUSH()
#include <imgui/imgui.h>
DISABLE_WARNINGS_POP()

#include <algorithm>

namespace {
constexpr ImGuiWindowFlags kDebugWindowFlags = ImGuiWindowFlags_NoCollapse;
constexpr ImGuiTabBarFlags kTabBarFlags = ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_TabListPopupButton;

std::string sanitizeId(const std::string& id)
{
    if (!id.empty())
        return id;
    return "tab_";
}

}

DebugUiManager::DebugUiManager() = default;

DebugUiManager::TabHandle DebugUiManager::registerTab(const TabDescriptor& descriptor)
{
    const std::string key = sanitizeId(descriptor.id);
    if (TabRecord* existing = findRecord(key)) {
        existing->label = descriptor.label;
        existing->draw = descriptor.draw;
        existing->order = descriptor.order;
        existing->visible = descriptor.visible;
        existing->closable = descriptor.closable;
        return TabHandle { existing->id };
    }

    TabRecord record;
    record.id = key;
    record.label = descriptor.label;
    record.draw = descriptor.draw;
    record.order = descriptor.order;
    record.visible = descriptor.visible;
    record.closable = descriptor.closable;
    record.registrationIndex = m_nextRegistrationIndex++;
    m_tabs.push_back(std::move(record));
    m_tabsDirty = true;
    return TabHandle { key };
}

void DebugUiManager::unregisterTab(const TabHandle& handle)
{
    if (handle.id.empty())
        return;
    auto it = std::remove_if(m_tabs.begin(), m_tabs.end(), [&](const TabRecord& record) {
        return record.id == handle.id;
    });
    if (it != m_tabs.end()) {
        m_tabs.erase(it, m_tabs.end());
        m_tabsDirty = true;
    }
}

void DebugUiManager::setTabVisible(const TabHandle& handle, bool visible)
{
    if (TabRecord* record = findRecord(handle.id))
        record->visible = visible;
}

void DebugUiManager::setTabOrder(const TabHandle& handle, int order)
{
    if (TabRecord* record = findRecord(handle.id)) {
        record->order = order;
        m_tabsDirty = true;
    }
}

void DebugUiManager::draw()
{
    if (!m_showWindow)
        return;

    sortTabsIfNeeded();

    if (!ImGui::Begin("Debug Tools", &m_showWindow, kDebugWindowFlags)) {
        ImGui::End();
        return;
    }

    if (ImGui::BeginTabBar("DebugTabs", kTabBarFlags)) {
        for (TabRecord& tab : m_tabs) {
            if (!tab.visible || !tab.draw)
                continue;

            ImGui::PushID(tab.id.c_str());
            bool open = tab.closable ? tab.visible : true;
            bool active = tab.closable ? ImGui::BeginTabItem(tab.label.c_str(), &open) : ImGui::BeginTabItem(tab.label.c_str());
            if (active) {
                if (ImGui::BeginChild("TabContent", ImVec2(0, 0), false, ImGuiWindowFlags_AlwaysVerticalScrollbar)) {
                    ImGui::PushID("content");
                    tab.draw();
                    ImGui::PopID();
                }
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            if (tab.closable)
                tab.visible = open;
            ImGui::PopID();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();
}

void DebugUiManager::setShowWindow(bool show)
{
    m_showWindow = show;
}

bool DebugUiManager::showWindow() const
{
    return m_showWindow;
}

std::vector<DebugUiManager::TabInfo> DebugUiManager::tabs() const
{
    std::vector<TabInfo> info;
    info.reserve(m_tabs.size());
    for (const TabRecord& tab : m_tabs) {
        info.push_back(TabInfo {
            .id = tab.id,
            .label = tab.label,
            .visible = tab.visible,
            .order = tab.order,
            .closable = tab.closable,
        });
    }
    return info;
}

DebugUiManager::TabRecord* DebugUiManager::findRecord(const std::string& id)
{
    if (id.empty())
        return nullptr;
    for (TabRecord& tab : m_tabs) {
        if (tab.id == id)
            return &tab;
    }
    return nullptr;
}

const DebugUiManager::TabRecord* DebugUiManager::findRecord(const std::string& id) const
{
    if (id.empty())
        return nullptr;
    for (const TabRecord& tab : m_tabs) {
        if (tab.id == id)
            return &tab;
    }
    return nullptr;
}

void DebugUiManager::sortTabsIfNeeded()
{
    if (!m_tabsDirty)
        return;

    std::stable_sort(m_tabs.begin(), m_tabs.end(), [](const TabRecord& lhs, const TabRecord& rhs) {
        if (lhs.order == rhs.order)
            return lhs.registrationIndex < rhs.registrationIndex;
        return lhs.order < rhs.order;
    });
    m_tabsDirty = false;
}


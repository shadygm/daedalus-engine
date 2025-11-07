// SPDX-License-Identifier: MIT
#pragma once

#include <functional>
#include <string>
#include <vector>

class DebugUiManager {
public:
    using DrawCallback = std::function<void()>;

    struct TabHandle {
        std::string id;
    };

    struct TabDescriptor {
        std::string id;
        std::string label;
        DrawCallback draw;
        int order { 0 };
        bool visible { true };
        bool closable { false };
    };

    struct TabInfo {
        std::string id;
        std::string label;
        bool visible { true };
        int order { 0 };
        bool closable { false };
    };

    DebugUiManager();

    [[nodiscard]] TabHandle registerTab(const TabDescriptor& descriptor);
    void unregisterTab(const TabHandle& handle);
    void setTabVisible(const TabHandle& handle, bool visible);
    void setTabOrder(const TabHandle& handle, int order);

    void draw();

    void setShowWindow(bool show);
    [[nodiscard]] bool showWindow() const;

    [[nodiscard]] std::vector<TabInfo> tabs() const;

private:
    struct TabRecord {
        std::string id;
        std::string label;
        DrawCallback draw;
        int order { 0 };
        bool visible { true };
        bool closable { false };
        std::size_t registrationIndex { 0 };
    };

    [[nodiscard]] TabRecord* findRecord(const std::string& id);
    [[nodiscard]] const TabRecord* findRecord(const std::string& id) const;
    void sortTabsIfNeeded();

    bool m_showWindow { true };
    bool m_tabsDirty { false };
    std::size_t m_nextRegistrationIndex { 0 };
    std::vector<TabRecord> m_tabs;
};

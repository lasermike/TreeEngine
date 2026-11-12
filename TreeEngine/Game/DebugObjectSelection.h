#pragma once
#include "ObjectSelection.h"
#include "imgui.h"
#include "imgui_internal.h"

namespace DebugObjectSelection
{
    inline void RevealRow(const void* object, ObjectSelection& selection)
    {
        if (selection.reveal != object) return;
        ImGui::ScrollToItem(ImGuiScrollFlags_KeepVisibleEdgeY);
        selection.reveal = nullptr;
    }
}

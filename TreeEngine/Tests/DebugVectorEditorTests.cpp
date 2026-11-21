#include "DebugVectorEditor.h"
#include <iostream>
#include <stdexcept>
#include <functional>

using namespace DirectX;

namespace
{
    void Check(bool value, const char* message)
    {
        if (!value) throw std::runtime_error(message);
    }

    void Frame(const std::function<void()>& contents)
    {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(500, 500));
        ImGui::Begin("Test", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings);
        contents();
        ImGui::End();
        ImGui::Render();
        const ImDrawData* data = ImGui::GetDrawData();
        for (int list = 0; list < data->CmdListsCount; ++list)
        {
            for (const auto& vertex : data->CmdLists[list]->VtxBuffer)
            {
                Check(std::isfinite(vertex.pos.x) && std::isfinite(vertex.pos.y), "non-finite gizmo draw coordinates");
            }
        }
    }
}

int main()
{
    try
    {
        ImGui::CreateContext();
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(640, 640);
        io.DeltaTime = 1.0f / 60;
        unsigned char* pixels;
        int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        XMFLOAT3 position(1, 2, 3);
        ImVec2 field;
        auto positionRow = [&]()
        {
            DebugVectorEditor::Edit("Position", &position.x, 3, 0.1f, DebugVectorEditor::Tools::None);
            const auto low = ImGui::GetItemRectMin();
            const auto high = ImGui::GetItemRectMax();
            field = ImVec2((low.x + high.x) * 0.5f, (low.y + high.y) * 0.5f);
        };
        Frame(positionRow);
        Frame(positionRow);
        io.AddMousePosEvent(field.x, field.y);
        io.AddKeyEvent(ImGuiMod_Ctrl, true);
        io.AddMouseButtonEvent(0, true);
        Frame(positionRow);
        io.AddMouseButtonEvent(0, false);
        io.AddKeyEvent(ImGuiMod_Ctrl, false);
        Frame(positionRow);
        io.AddInputCharactersUTF8("12.5");
        Frame(positionRow);
        io.AddKeyEvent(ImGuiKey_Enter, true);
        Frame(positionRow);
        io.AddKeyEvent(ImGuiKey_Enter, false);
        Frame(positionRow);
        Check(position.x == 1 && position.y == 2 && position.z == 12.5f, "Ctrl-click numeric entry did not edit only Z");

        XMFLOAT4 quaternion = DebugVectorMath::FromDegrees(XMFLOAT3(25, 40, -15));
        const auto unchanged = quaternion;
        XMFLOAT3 angles(0, 0, 0);
        Frame([&]()
        {
            DebugVectorEditor::Edit("Orientation (degrees)", &angles.x, 3, 0.25f,
                DebugVectorEditor::Tools::Rotation, &quaternion);
        });
        Check(XMVector4Equal(XMLoadFloat4(&quaternion), XMLoadFloat4(&unchanged)), "idle orientation editor mutated pose");

        // Exercise every popup with real opening clicks. This also checks adapter math and draw output.
        for (auto tool : { DebugVectorEditor::Tools::Camera, DebugVectorEditor::Tools::Position,
            DebugVectorEditor::Tools::LightPosition, DebugVectorEditor::Tools::Direction,
            DebugVectorEditor::Tools::Rotation, DebugVectorEditor::Tools::Scale })
        {
            DebugVectorEditor::Reset();
            position = XMFLOAT3(1, 2, 3);
            quaternion = XMFLOAT4(0, 0, 0, 1);
            ImVec2 tools;
            auto row = [&]()
            {
                const auto start = ImGui::GetCursorScreenPos();
                const auto& style = ImGui::GetStyle();
                tools = ImVec2(start.x + ImGui::CalcTextSize("Vector").x + style.ItemSpacing.x
                    + ImGui::CalcTextSize("Copy").x + 2 * style.FramePadding.x + style.ItemSpacing.x + 10,
                    start.y + ImGui::GetFontSize() * 0.5f);
                DebugVectorEditor::Edit("Vector", &position.x, 3, 0.1f, tool, &quaternion, &quaternion);
            };
            Frame(row);
            io.AddMousePosEvent(tools.x, tools.y);
            Frame(row);
            io.AddMouseButtonEvent(0, true);
            Frame(row);
            io.AddMouseButtonEvent(0, false);
            Frame(row);
            Frame(row);
            if (!ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId))
            {
                throw std::runtime_error("tools flyout did not open: " + std::to_string(static_cast<int>(tool))
                    + " at " + std::to_string(tools.x) + ", " + std::to_string(tools.y));
            }
            Frame(row);
            io.AddKeyEvent(ImGuiKey_Escape, true);
            Frame(row);
            io.AddKeyEvent(ImGuiKey_Escape, false);
            Frame(row);
            io.AddMousePosEvent(630, 630);
            Frame(row);
            io.AddMouseButtonEvent(0, true);
            Frame(row);
            io.AddMouseButtonEvent(0, false);
            Frame(row);
        }

        // Compare reflected quaternion rotation against reflected engine forward vector.
        auto engine = DebugVectorMath::FromDegrees(XMFLOAT3(30, 70, 20));
        auto gizmo = DebugVectorMath::GizmoRotation(engine);
        auto actual = ImViewGuizmo::GizmoMath::multiply_qv(gizmo, XMFLOAT3(0, 0, -1));
        auto expected = DebugVectorMath::GizmoPosition(DebugVectorEditor::Forward(engine));
        Check(ImViewGuizmo::GizmoMath::length(ImViewGuizmo::GizmoMath::subtract_vv(actual, expected)) < 1e-5f,
            "gizmo forward does not match engine forward");
        ImGui::DestroyContext();
        std::cout << "Vector editor numeric entry, idle pose, six flyouts, finite drawing, and coordinate conversion passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

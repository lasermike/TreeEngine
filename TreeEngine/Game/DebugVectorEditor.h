#pragma once
#include "DebugGizmoAdapter.h"
#include <map>

namespace DebugVectorEditor
{
    using namespace DirectX;
    using namespace DebugVectorMath;
    enum class Tools
    {
        None,
        Camera,
        Position,
        LightPosition,
        Rotation,
        Direction,
        Scale
    };

    struct State
    {
        bool linked = false;
        bool editingAngles = false;
        XMFLOAT3 angles = XMFLOAT3(0, 0, 0);
        XMFLOAT3 pivot = XMFLOAT3(0, 0, 0);
        float distance = 10;
    };
    inline std::map<ImGuiID, State>& States()
    {
        static std::map<ImGuiID, State> states;
        return states;
    }
    inline void Reset()
    {
        States().clear();
        ImViewGuizmo::GetContext() = ImViewGuizmo::Context();
    }
    inline void Copy(const char* label, const float* values, int count)
    {
        if (ImGui::SmallButton("Copy"))
        {
            ImGui::SetClipboardText(Format(label, values, count).c_str());
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Copy field name and values");
    }
    inline void ReadOnly(const char* label, const float* values, int count)
    {
        ImGui::PushID(label);
        ImGui::TextWrapped("%s", Format(label, values, count).c_str());
        Copy(label, values, count);
        ImGui::PopID();
    }
    inline XMFLOAT3 Forward(const XMFLOAT4& q)
    {
        XMFLOAT3 result;
        XMStoreFloat3(&result, XMVector3Rotate(g_XMIdentityR2, XMLoadFloat4(&q)));
        return result;
    }

    // Rotation edits return a complete quaternion through rotation; camera navigation can edit both pose fields.
    inline bool Edit(const char* label, float* values, int count, float speed, Tools tools,
                     XMFLOAT4* rotation = nullptr, const XMFLOAT4* viewRotation = nullptr,
                     const XMFLOAT3* center = nullptr, const char* copyLabel = nullptr)
    {
        ImGui::PushID(label);
        State& state = States()[ImGui::GetID("state")];
        const bool orientation = tools == Tools::Rotation;
        float previous[4];
        std::copy(values, values + count, previous);
        const XMFLOAT4 oldRotation = rotation ? *rotation : XMFLOAT4(0, 0, 0, 1);
        if (orientation && !state.editingAngles)
            state.angles = ToDegrees(*rotation);
        float* components = orientation ? &state.angles.x : values;
        const int componentCount = orientation ? 3 : count;
        bool changed = false;
        bool active = false;
        ImGui::TextUnformatted(label);
        ImGui::SameLine();
        Copy(copyLabel ? copyLabel : label, components, componentCount);
        if (tools != Tools::None)
        {
            ImGui::SameLine();
            if (ImGui::SmallButton("Tools"))
            {
                ImViewGuizmo::GetContext() = ImViewGuizmo::Context();
                if (tools == Tools::Camera)
                {
                    const XMFLOAT3 forward = Forward(*rotation);
                    for (int i = 0; i < 3; ++i)
                        (&state.pivot.x)[i] = values[i] + (&forward.x)[i] * state.distance;
                }
                ImGui::OpenPopup("tools");
            }
        }
        if (tools == Tools::Scale)
        {
            ImGui::SameLine();
            ImGui::Checkbox("Link", &state.linked);
        }
        const char* axes[] = {"X", "Y", "Z", "W"};
        const char* angles[] = {"Pitch", "Yaw", "Roll"};
        const float width = std::max(
            30.0f, (ImGui::GetContentRegionAvail().x - (componentCount - 1) * ImGui::GetStyle().ItemSpacing.x) /
                       componentCount);
        for (int i = 0; i < componentCount; ++i)
        {
            if (i)
                ImGui::SameLine();
            ImGui::PushID(i);
            ImGui::SetNextItemWidth(width);
            float next = components[i];
            const bool disabled = tools == Tools::Scale && state.linked && std::abs(next) < 1e-6f;
            ImGui::BeginDisabled(disabled);
            const char* name = orientation ? angles[i] : axes[i];
            std::string format = std::string(name) + ": %.3f";
            if (ImGui::DragFloat("##value", &next, speed, 0, 0, format.c_str()) && std::isfinite(next))
            {
                if (tools == Tools::Scale && state.linked)
                {
                    changed |= ScaleLinked(components, count, i, next);
                }
                else
                {
                    components[i] = next;
                    changed = true;
                }
            }
            active |= ImGui::IsItemActive();
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            {
                ImGui::SetTooltip(disabled ? "Unlink to edit a zero component"
                                           : "Drag to adjust; Ctrl-click or double-click to type");
            }
            ImGui::PopID();
        }
        state.editingAngles = orientation && active;
        if (orientation && changed)
            *rotation = FromDegrees(state.angles);
        if (tools == Tools::Direction && (values[0] * values[0] + values[1] * values[1] + values[2] * values[2]) < 1e-12f)
        {
            std::copy(previous, previous + count, values);
            changed = false;
        }
        if (ImGui::BeginPopup("tools"))
        {
            ImGui::TextUnformatted(label);
            if (tools == Tools::Scale)
            {
                ImGui::Checkbox("Link components", &state.linked);
                ImGui::Button("Scale: drag left / right", ImVec2(240, 70));
                if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
                {
                    const float factor = std::exp(ImGui::GetIO().MouseDelta.x * 0.01f);
                    for (int i = 0; i < count; ++i)
                        values[i] *= factor;
                    changed = true;
                }
            }
            else
            {
                if (tools == Tools::Camera)
                {
                    if (ImGui::DragFloat("Orbit distance", &state.distance, 0.1f, 0.01f, 100000.0f, "%.2f",
                                         ImGuiSliderFlags_AlwaysClamp))
                    {
                        const auto f = Forward(*rotation);
                        for (int i = 0; i < 3; ++i)
                            (&state.pivot.x)[i] = values[i] + (&f.x)[i] * state.distance;
                    }
                }
                XMFLOAT3 position(values[0], values[1], values[2]);
                XMFLOAT4 q = rotation ? *rotation : viewRotation ? *viewRotation : XMFLOAT4(0, 0, 0, 1);
                XMFLOAT3 pivot = tools == Tools::Camera ? state.pivot : center ? *center : XMFLOAT3(0, 0, 0);
                if (orientation || tools == Tools::Direction)
                {
                    pivot = XMFLOAT3(0, 0, 0);
                    if (tools == Tools::Direction)
                    {
                        const auto d = GizmoPosition(position);
                        q = GizmoRotation(ImViewGuizmo::GizmoMath::quatLookAt(d, XMFLOAT3(0, 1, 0)));
                    }
                    const auto f = Forward(q);
                    position = XMFLOAT3(-f.x, -f.y, -f.z);
                }
                else if (tools == Tools::LightPosition)
                {
                    XMFLOAT3 d(pivot.x - position.x, pivot.y - position.y, pivot.z - position.z);
                    if (XMVectorGetX(XMVector3LengthSq(XMLoadFloat3(&d))) > 1e-8f)
                    {
                        q = GizmoRotation(ImViewGuizmo::GizmoMath::quatLookAt(GizmoPosition(d), XMFLOAT3(0, 1, 0)));
                    }
                }
                auto gp = GizmoPosition(position);
                auto gq = GizmoRotation(q);
                auto target = GizmoPosition(pivot);
                auto& style = ImViewGuizmo::GetStyle();
                style.scale = ImGui::GetFontSize() / 20.0f;
                style.animateSnap = false;
                ImViewGuizmo::BeginFrame();
                const float radius = tools == Tools::Position ? 0 : 150 * style.scale;
                const auto top = ImGui::GetCursorScreenPos();
                bool manipulated = false;
                if (tools != Tools::Position)
                {
                    // Coincident light/pivot positions cannot define an orbit.
                    if (ImViewGuizmo::GizmoMath::length2(ImViewGuizmo::GizmoMath::subtract_vv(gp, target)) < 1e-8f)
                        gp.z += 0.01f;
                    manipulated |= ImViewGuizmo::Rotate(gp, gq, target, ImVec2(top.x + radius, top.y + radius));
                }
                if (!orientation && tools != Tools::Direction)
                {
                    manipulated |= ImViewGuizmo::Dolly(gp, gq, ImVec2(top.x + 30, top.y + 2 * radius + 30));
                    const auto beforePan = gp;
                    const bool panned = ImViewGuizmo::Pan(gp, gq, ImVec2(top.x + 100, top.y + 2 * radius + 30));
                    if (panned && tools == Tools::Camera)
                    {
                        target = ImViewGuizmo::GizmoMath::add_vv(target,
                                                                 ImViewGuizmo::GizmoMath::subtract_vv(gp, beforePan));
                        state.pivot = GizmoPosition(target);
                    }
                    manipulated |= panned;
                }
                ImGui::Dummy(ImVec2(std::max(160.0f, 2 * radius), 2 * radius + 65));
                ImGui::TextUnformatted(orientation || tools == Tools::Direction
                                           ? "Drag to rotate; click an axis to snap"
                                           : tools == Tools::Position ? "Magnifier: dolly; arrows: pan"
                                                                     : "Drag axes to orbit; magnifier: dolly; arrows: pan");
                if (manipulated)
                {
                    q = GizmoRotation(gq);
                    Normalize(q);
                    if (orientation)
                        *rotation = q;
                    else if (tools == Tools::Direction)
                    {
                        const auto direction = Forward(q);
                        std::copy(&direction.x, &direction.x + 3, values);
                    }
                    else
                    {
                        const auto p = GizmoPosition(gp);
                        std::copy(&p.x, &p.x + 3, values);
                        if (tools == Tools::Camera)
                        {
                            *rotation = q;
                            state.distance = std::max(0.01f, ImViewGuizmo::GizmoMath::length(
                                                                 ImViewGuizmo::GizmoMath::subtract_vv(gp, target)));
                        }
                    }
                    changed = true;
                }
                if (orientation)
                {
                    XMFLOAT4 edited = *rotation;
                    if (ImGui::InputFloat4("Quaternion XYZW", &edited.x) && Normalize(edited))
                    {
                        *rotation = edited;
                        changed = true;
                    }
                    std::string quaternionLabel = copyLabel ? copyLabel : label;
                    const auto units = quaternionLabel.find(" (degrees)");
                    if (units != std::string::npos)
                    {
                        quaternionLabel.erase(units, 10);
                    }
                    Copy((quaternionLabel + " quaternion XYZW").c_str(), &rotation->x, 4);
                }
            }
            ImGui::EndPopup();
        }
        if (!Finite(values, count) || (changed && rotation && !Normalize(*rotation)))
        {
            std::copy(previous, previous + count, values);
            if (rotation)
                *rotation = oldRotation;
            changed = false;
        }
        ImGui::PopID();
        return changed;
    }
} // namespace DebugVectorEditor

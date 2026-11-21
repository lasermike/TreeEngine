#include "ObjectSelection.h"
#include "HighlightGeometry.h"
#include "DebugObjectSelection.h"
#include <cassert>
#include <cstdio>

int main()
{
    int a = 0, b = 0;
    ObjectSelection selection;
    selection.Click(0.5f, 0.5f, false);
    auto first = selection.requests.back();
    selection.Click(0.4f, 0.5f, true);
    auto second = selection.requests.back();
    selection.results.push_back({ first, &a });
    selection.results.push_back({ second, &b });
    selection.ConsumeResults();
    assert(selection.Contains(&a) && selection.Contains(&b) && selection.reveal == &b);
    selection.Apply(&a, true);
    assert(!selection.Contains(&a) && selection.Contains(&b));
    selection.Apply(nullptr, true);
    assert(selection.Contains(&b));
    selection.Apply(nullptr, false);
    assert(selection.objects.empty() && !selection.reveal);

    // A newer replacement cancels older GPU results, including those already in flight.
    selection.Click(0.2f, 0.2f, false);
    selection.results.push_back({ first, &a });
    selection.ConsumeResults();
    assert(selection.objects.empty());
    selection.results.push_back({ selection.requests.back(), &b });
    selection.Invalidate(true);
    selection.ConsumeResults();
    assert(selection.objects.empty());
    selection.SelectRow(&a, false);
    selection.SelectRow(&b, true);
    assert(selection.objects.size() == 2 && !selection.reveal);
    selection.SelectRow(&b, true);
    assert(selection.objects.size() == 1 && selection.latest == &a);
    selection.Invalidate();
    assert(selection.Contains(&a)); // Device recreation preserves live selections.

    selection.Invalidate(true);
    selection.Click(0.5f, 0.5f, false);
    first = selection.requests.back();
    selection.Click(0.4f, 0.5f, true);
    second = selection.requests.back();
    selection.results.push_back({ second, &b });
    selection.ConsumeResults();
    assert(selection.objects.empty()); // Results must be applied in gesture order.
    selection.SelectRow(&a, true); // A UI Ctrl-click also waits for earlier GPU picks.
    selection.results.push_back({ first, &a });
    selection.ConsumeResults();
    assert(selection.objects.size() == 1 && selection.Contains(&b));

    using namespace DirectX;
    XMFLOAT4 start(0, 0, -1, 1), end(0, 0, 0.5f, 1);
    assert(HighlightGeometry::Clip(start, end) && start.z == 0);
    start = XMFLOAT4(-3, 0, 0.5f, 1);
    end = XMFLOAT4(3, 0, 0.5f, 1);
    assert(HighlightGeometry::Clip(start, end) && start.x == -1 && end.x == 1);
    start = XMFLOAT4(0, 0, -2, 1);
    end = XMFLOAT4(0, 0, -1, 1);
    assert(!HighlightGeometry::Clip(start, end));

    ObjectHighlight highlight;
    highlight.bounds[0] = XMFLOAT3(-0.5f, -0.5f, 0.2f);
    highlight.bounds[1] = XMFLOAT3(0.5f, 0.5f, 0.8f);
    highlight.color = XMFLOAT4(1, 0, 0, 0.5f);
    highlight.occlusion = HighlightOcclusion::SceneDepth;
    std::vector<HighlightGeometry::Vertex> vertices;
    HighlightGeometry::Append(highlight, XMMatrixIdentity(), 800, 600, vertices);
    assert(!vertices.empty() && vertices.size() % 6 == 0);
    for (const auto& vertex : vertices)
        assert(vertex.color.y == 0 && vertex.color.w == 0.5f && vertex.depthTest == 1);
    const auto count = vertices.size();
    highlight.occlusion = HighlightOcclusion::AlwaysVisible;
    HighlightGeometry::Append(highlight, XMMatrixIdentity(), 800, 600, vertices);
    assert(vertices.size() == 2 * count && vertices.back().depthTest == 0);
    highlight.adornments.clear();
    HighlightGeometry::Append(highlight, XMMatrixIdentity(), 800, 600, vertices);
    assert(vertices.size() == 2 * count);

    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(640, 480);
    io.DeltaTime = 1.0f / 60;
    io.Fonts->Build();
    int rows[100] = {};
    float revealedScroll = 0;
    for (int frame = 0; frame < 6; ++frame)
    {
        if (frame == 1 || frame == 3) selection.reveal = &rows[99];
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(300, 200));
        ImGui::Begin("Objects");
        if (frame == 2) revealedScroll = ImGui::GetScrollY();
        if (frame == 4)
        {
            assert(ImGui::GetScrollY() == revealedScroll); // Visible row isn't re-centered.
            ImGui::SetScrollY(0);
        }
        if (frame == 5) assert(ImGui::GetScrollY() == 0); // Browsing away doesn't snap back.
        for (int row = 0; row < 100; ++row)
        {
            ImGui::Text("Object %d", row);
            DebugObjectSelection::RevealRow(&rows[row], selection);
            if (frame == 2 && row == 99) assert(ImGui::IsItemVisible());
        }
        ImGui::End();
        ImGui::Render();
    }
    assert(revealedScroll > 0 && !selection.reveal);
    ImGui::DestroyContext();
    puts("Object selection and highlight geometry tests passed.");
}

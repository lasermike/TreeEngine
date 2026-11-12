#pragma once
#include <algorithm>
#include <cstdint>
#include <deque>
#include <vector>

// CPU interaction state. GPU results carry an epoch so destroyed scene objects are never dereferenced.
struct ObjectPickRequest
{
    float u = 0, v = 0;
    uint64_t epoch = 0;
    bool toggle = false;
    uint64_t sequence = 0;
};

struct ObjectPickResult
{
    ObjectPickRequest request;
    const void* object = nullptr;
};

class ObjectSelection
{
    struct Operation
    {
        ObjectPickRequest request;
        const void* object = nullptr;
        bool ready = false;
        bool reveal = true;
    };
    std::deque<Operation> m_operations;
    uint64_t m_sequence = 0;
public:
    std::vector<const void*> objects;
    std::deque<ObjectPickRequest> requests;
    std::deque<ObjectPickResult> results;
    const void* reveal = nullptr;
    const void* latest = nullptr;
    uint64_t epoch = 1;

    bool Contains(const void* object) const
    {
        return std::find(objects.begin(), objects.end(), object) != objects.end();
    }

    void Invalidate(bool clear = false)
    {
        ++epoch;
        requests.clear();
        results.clear();
        m_operations.clear();
        if (clear)
        {
            objects.clear();
            reveal = latest = nullptr;
        }
    }

    void Apply(const void* object, bool toggle)
    {
        if (!toggle)
        {
            objects.clear();
            reveal = latest = nullptr;
        }
        if (!object) return;
        const auto found = std::find(objects.begin(), objects.end(), object);
        if (found != objects.end())
        {
            objects.erase(found);
            if (reveal == object) reveal = nullptr;
            if (latest == object) latest = objects.empty() ? nullptr : objects.back();
        }
        else
        {
            objects.push_back(object);
            reveal = latest = object;
        }
    }

    void Click(float u, float v, bool toggle)
    {
        if (!toggle) Invalidate();
        ObjectPickRequest request = { u, v, epoch, toggle, ++m_sequence };
        requests.push_back(request);
        m_operations.push_back({ request });
    }

    void SelectRow(const void* object, bool toggle)
    {
        if (!toggle) Invalidate();
        ObjectPickRequest request = { 0, 0, epoch, toggle, ++m_sequence };
        m_operations.push_back({ request, object, true, false });
        ConsumeResults();
    }

    void ConsumeResults()
    {
        for (const auto& result : results)
        {
            if (result.request.epoch != epoch) continue;
            for (auto& operation : m_operations)
            {
                if (operation.request.sequence != result.request.sequence) continue;
                operation.object = result.object;
                operation.ready = true;
                break;
            }
        }
        results.clear();
        while (!m_operations.empty() && m_operations.front().ready)
        {
            const auto operation = m_operations.front();
            Apply(operation.object, operation.request.toggle);
            if (!operation.reveal) reveal = nullptr;
            m_operations.pop_front();
        }
    }
};

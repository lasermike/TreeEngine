#pragma once

// Grading a terrain's finest height grid under its roads (Tools/terrain: TerrainTool pack --roads), so the ground
// the road surfaces drape on (RoadMesh.h) is a road bed: smooth along the road, crowned across it, with shoulders,
// and with ditches beside county roads, blending back into the surveyed ground beyond.

#include "RoadNetwork.h"

namespace Roads
{
    // Cross-section, meters per meter: the crown falls from the centerline to the edge of the traveled way, the
    // shoulder falls a little more steeply, and with no ditch the bed blends back into the ground over BlendWidth.
    constexpr float PavedCrown = 0.02f, GravelCrown = 0.03f, DrivewayCrown = 0.01f, ShoulderSlope = 0.04f;
    constexpr float BlendWidth = 3.0f;
    // Along the road: the bed follows the ground's running median over MedianWindow samples, then its running mean
    // over SmoothingWindow samples (samples are SampleSpacing apart), so bumps under 10 m or so are planed off.
    constexpr int MedianWindow = 7, SmoothingWindow = 17;

    struct GradeReport
    {
        size_t graded = 0;          // Samples whose height changed.
        size_t ditched = 0;         // Of those, the ones in a ditch.
        float deepestCut = 0;       // Meters taken off the ground at most,
        float highestFill = 0;      // and added at most.
    };

    // Grades `heights`: samples x samples, row-major from -x/-z, `spacing` meters apart, centered on the package
    // origin (the network's coordinates). Where roads overlap the more important one wins: a sample on a traveled
    // way takes it (paved roads over gravel roads over driveways), else the nearest line's shoulder, ditch or blend.
    GradeReport Grade(const Network& network, std::vector<float>& heights, int samples, float spacing);
}

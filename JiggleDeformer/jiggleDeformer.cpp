#include "jiggleDeformer.h"

#include <maya/MFnNumericAttribute.h>
#include <maya/MFnUnitAttribute.h>
#include <maya/MFnMatrixAttribute.h>
#include <maya/MFnCompoundAttribute.h>
#include <maya/MFnMesh.h>
#include <maya/MItGeometry.h>
#include <maya/MDataBlock.h>
#include <maya/MDataHandle.h>
#include <maya/MArrayDataHandle.h>
#include <maya/MMatrix.h>
#include <maya/MTime.h>
#include <maya/MPlug.h>
#include <maya/MPlugArray.h>
#include <maya/MDoubleArray.h>

#include <algorithm>
#include <cmath>


// 0x00000 - 0x7ffff is Autodesk's range for local/internal plugins.
// Register a unique block with Autodesk before distributing the plugin.
const MTypeId JiggleDeformer::kTypeId(0x00070001);

MObject JiggleDeformer::aEnable;
MObject JiggleDeformer::aTime;
MObject JiggleDeformer::aStartFrame;
MObject JiggleDeformer::aStiffnessMagnitude;
MObject JiggleDeformer::aDampingMagnitude;
MObject JiggleDeformer::aMaxInward;
MObject JiggleDeformer::aFoldCorrection;
MObject JiggleDeformer::aFoldIterations;
MObject JiggleDeformer::aPerGeometry;
MObject JiggleDeformer::aWeight;
MObject JiggleDeformer::aStiffness;
MObject JiggleDeformer::aDamping;
MObject JiggleDeformer::aStiffnessMap;
MObject JiggleDeformer::aDampingMap;
MObject JiggleDeformer::aWorldMatrix;


// Default slider values
const float kDefaultWeight    = 1.0f;
const float kDefaultStiffness = 0.5f;
const float kDefaultDamping   = 0.3f;


// =============================================================================
//  Node setup
// =============================================================================

void* JiggleDeformer::creator()
{
    return new JiggleDeformer();
}

MStatus JiggleDeformer::initialize()
{
    MFnNumericAttribute  nAttr;
    MFnUnitAttribute     uAttr;
    MFnMatrixAttribute   mAttr;
    MFnCompoundAttribute cAttr;

    // ---- Global attributes ----

    aEnable = nAttr.create("enable", "ena", MFnNumericData::kBoolean, true);
    nAttr.setKeyable(true);
    addAttribute(aEnable);
    attributeAffects(aEnable, outputGeom);

    aTime = uAttr.create("time", "tm", MFnUnitAttribute::kTime, 0.0);
    addAttribute(aTime);
    attributeAffects(aTime, outputGeom);

    aStartFrame = nAttr.create("startFrame", "sf", MFnNumericData::kDouble, 1.0);
    nAttr.setKeyable(true);
    addAttribute(aStartFrame);
    attributeAffects(aStartFrame, outputGeom);

    // Overall multipliers on top of every geometry's stiffness / damping.
    // The slider goes 0-1, but higher values can be typed in.
    aStiffnessMagnitude = nAttr.create("stiffnessMagnitude", "stfm", MFnNumericData::kFloat, 1.0);
    nAttr.setKeyable(true);
    nAttr.setMin(0.0);
    nAttr.setSoftMax(1.0);
    addAttribute(aStiffnessMagnitude);
    attributeAffects(aStiffnessMagnitude, outputGeom);

    aDampingMagnitude = nAttr.create("dampingMagnitude", "dmpm", MFnNumericData::kFloat, 1.0);
    nAttr.setKeyable(true);
    nAttr.setMin(0.0);
    nAttr.setSoftMax(1.0);
    addAttribute(aDampingMagnitude);
    attributeAffects(aDampingMagnitude, outputGeom);

    // How far a vertex may sink into the surface (against its normal),
    // measured in edge lengths around that vertex:  1.0 = one edge length.
    // Outward movement is never limited.
    aMaxInward = nAttr.create("maxInward", "mxin", MFnNumericData::kFloat, 1.0);
    nAttr.setKeyable(true);
    nAttr.setMin(0.0);
    nAttr.setSoftMax(2.0);
    addAttribute(aMaxInward);
    attributeAffects(aMaxInward, outputGeom);

    // How strongly a folded (flipped) face is pulled back toward the goal, per iteration.
    aFoldCorrection = nAttr.create("foldCorrection", "fcor", MFnNumericData::kFloat, 0.5);
    nAttr.setKeyable(true);
    nAttr.setMin(0.0);
    nAttr.setMax(1.0);
    addAttribute(aFoldCorrection);
    attributeAffects(aFoldCorrection, outputGeom);

    // How many times per frame the folds are checked and fixed.  0 = off.
    aFoldIterations = nAttr.create("foldIterations", "fitr", MFnNumericData::kInt, 3);
    nAttr.setKeyable(true);
    nAttr.setMin(0);
    nAttr.setSoftMax(10);
    addAttribute(aFoldIterations);
    attributeAffects(aFoldIterations, outputGeom);

    // ---- Per geometry: sliders ----

    aWeight = nAttr.create("weight", "pgw", MFnNumericData::kFloat, kDefaultWeight);
    nAttr.setKeyable(true);
    nAttr.setMin(0.0);
    nAttr.setMax(1.0);

    aStiffness = nAttr.create("stiffness", "stf", MFnNumericData::kFloat, kDefaultStiffness);
    nAttr.setKeyable(true);
    nAttr.setMin(0.0);
    nAttr.setMax(1.0);

    aDamping = nAttr.create("damping", "dmp", MFnNumericData::kFloat, kDefaultDamping);
    nAttr.setKeyable(true);
    nAttr.setMin(0.0);
    nAttr.setMax(1.0);

    // ---- Per geometry: paintable maps (one float per vertex) ----

    aStiffnessMap = nAttr.create("stiffnessMap", "smap", MFnNumericData::kFloat, 1.0);
    nAttr.setArray(true);
    nAttr.setUsesArrayDataBuilder(true);
    nAttr.setMin(0.0);
    nAttr.setMax(1.0);

    aDampingMap = nAttr.create("dampingMap", "dmap", MFnNumericData::kFloat, 1.0);
    nAttr.setArray(true);
    nAttr.setUsesArrayDataBuilder(true);
    nAttr.setMin(0.0);
    nAttr.setMax(1.0);

    // ---- Per geometry: world matrix of the object ----
    // Connected from  shape.worldMatrix[0]  so that moving the object
    // makes Maya call deform(), even when the frame does not change.

    aWorldMatrix = mAttr.create("worldMatrix", "pgwm");
    mAttr.setHidden(true);   // internal connection; also avoids a Maya 2026 Attribute Editor bug with matrices in compound arrays

    // ---- Per geometry: the parent compound ----
    // Only the compound is added with addAttribute(), its children are added with it.

    aPerGeometry = cAttr.create("perGeometry", "pg");
    cAttr.setArray(true);
    cAttr.setUsesArrayDataBuilder(true);
    cAttr.addChild(aWeight);
    cAttr.addChild(aStiffness);
    cAttr.addChild(aDamping);
    cAttr.addChild(aStiffnessMap);
    cAttr.addChild(aDampingMap);
    cAttr.addChild(aWorldMatrix);
    addAttribute(aPerGeometry);

    attributeAffects(aPerGeometry,  outputGeom);
    attributeAffects(aWeight,       outputGeom);
    attributeAffects(aStiffness,    outputGeom);
    attributeAffects(aDamping,      outputGeom);
    attributeAffects(aStiffnessMap, outputGeom);
    attributeAffects(aDampingMap,   outputGeom);
    attributeAffects(aWorldMatrix,  outputGeom);

    return MS::kSuccess;
}


// =============================================================================
//  Dirty tracking
// =============================================================================

MStatus JiggleDeformer::setDependentsDirty(const MPlug& plug, MPlugArray& affectedPlugs)
{
    MObject attribute = plug.attribute();

    // ---- A painted map value changed ----
    // The plug looks like:  perGeometry[i].stiffnessMap[v]   or   weightList[i].weights[v]
    if (attribute == aStiffnessMap || attribute == aDampingMap || attribute == weights)
    {
        // Go up from  stiffnessMap[v]  to  stiffnessMap
        MPlug mapPlug = plug;
        if (plug.isElement())
            mapPlug = plug.array();

        // Go up from  stiffnessMap  to  perGeometry[i]  and read i
        if (mapPlug.isChild() && mapPlug.parent().isElement())
        {
            unsigned int geometryIndex = mapPlug.parent().logicalIndex();

            if (attribute == aStiffnessMap) stiffnessMapDirty_[geometryIndex] = true;
            if (attribute == aDampingMap)   dampingMapDirty_[geometryIndex]   = true;
            if (attribute == weights)       weightMapDirty_[geometryIndex]    = true;
        }
    }

    // ---- A whole perGeometry[i] or weightList[i] was set ----
    else if (attribute == aPerGeometry || attribute == weightList)
    {
        if (plug.isElement())
        {
            // Only geometry i
            markAllMapsDirty(plug.logicalIndex());
        }
        else
        {
            // The whole array: every geometry
            for (auto it = positions_.begin(); it != positions_.end(); ++it)
                markAllMapsDirty(it->first);
        }
    }

    return MPxDeformerNode::setDependentsDirty(plug, affectedPlugs);
}

void JiggleDeformer::markAllMapsDirty(unsigned int geometryIndex)
{
    weightMapDirty_[geometryIndex]    = true;
    stiffnessMapDirty_[geometryIndex] = true;
    dampingMapDirty_[geometryIndex]   = true;
}


// =============================================================================
//  Deform
// =============================================================================

MStatus JiggleDeformer::deform(MDataBlock& data, MItGeometry& itGeo,
                               const MMatrix& localToWorld, unsigned int geometryIndex)
{
    // Disabled: leave the geometry untouched, start fresh when enabled again.
    bool enable = data.inputValue(aEnable).asBool();
    if (!enable)
    {
        initialized_[geometryIndex] = false;
        return MS::kSuccess;
    }

    float  envelopeValue = data.inputValue(envelope).asFloat();
    double frame         = data.inputValue(aTime).asTime().as(MTime::uiUnit());
    double startFrame    = data.inputValue(aStartFrame).asDouble();

    float stiffnessMagnitude = data.inputValue(aStiffnessMagnitude).asFloat();
    float dampingMagnitude   = data.inputValue(aDampingMagnitude).asFloat();

    float maxInward      = data.inputValue(aMaxInward).asFloat();
    float foldCorrection = data.inputValue(aFoldCorrection).asFloat();
    int   foldIterations = data.inputValue(aFoldIterations).asInt();


    // ---- 1. Goal: the incoming shape, in world space ----
    // World space so that moving the transform also makes it jiggle.

    MPointArray goal;
    itGeo.allPositions(goal);

    unsigned int vertexCount = goal.length();

    for (unsigned int i = 0; i < vertexCount; ++i)
        goal[i] = goal[i] * localToWorld;


    // ---- 2. Check if the deformed vertices changed ----
    // Compare the vertices the iterator gives us now with the ones we stored.
    // Catches a different vertex count and membership edits (same count, other vertices).
    // A new geometry has no stored membership yet, so this also runs the first time.

    bool verticesChanged = false;

    if (membership_[geometryIndex].length() != vertexCount)
    {
        verticesChanged = true;
    }
    else
    {
        unsigned int i = 0;
        for (itGeo.reset(); !itGeo.isDone(); itGeo.next())
        {
            if (itGeo.index() != membership_[geometryIndex][i])
            {
                verticesChanged = true;
                break;
            }
            ++i;
        }
    }

    // Rebuild the stored membership
    if (verticesChanged)
    {
        membership_[geometryIndex].setLength(vertexCount);
        maxVertexId_[geometryIndex] = 0;

        unsigned int i = 0;
        for (itGeo.reset(); !itGeo.isDone(); itGeo.next())
        {
            membership_[geometryIndex][i] = itGeo.index();

            if (itGeo.index() > maxVertexId_[geometryIndex])
                maxVertexId_[geometryIndex] = itGeo.index();

            ++i;
        }

        // Rebuild the face list.
        // Only meshes have faces; for other geometry (lattice, curves...) the list stays
        // empty and the fold sections below simply do nothing.
        faceVertexCounts_[geometryIndex].clear();
        faceVertexIndices_[geometryIndex].clear();

        MArrayDataHandle inputHandle = data.outputArrayValue(input);
        inputHandle.jumpToElement(geometryIndex);
        MObject inputMesh = inputHandle.outputValue().child(inputGeom).asMesh();

        if (!inputMesh.isNull())
        {
            MFnMesh meshFn(inputMesh);
            MIntArray polygonCounts;
            MIntArray polygonVertexIds;
            meshFn.getVertices(polygonCounts, polygonVertexIds);

            // Vertex id -> iteration index  (-1 = not affected by the deformer)
            MIntArray indexById(maxVertexId_[geometryIndex] + 1, -1);
            for (unsigned int i = 0; i < vertexCount; ++i)
                indexById[membership_[geometryIndex][i]] = i;

            unsigned int offset = 0;
            for (unsigned int f = 0; f < polygonCounts.length(); ++f)
            {
                int count = polygonCounts[f];

                // Keep the face only if all its vertices are deformed
                bool allAffected = true;
                for (int j = 0; j < count; ++j)
                {
                    int vertexId = polygonVertexIds[offset + j];
                    if (vertexId > maxVertexId_[geometryIndex] || indexById[vertexId] == -1)
                        allAffected = false;
                }

                if (allAffected)
                {
                    faceVertexCounts_[geometryIndex].append(count);
                    for (int j = 0; j < count; ++j)
                        faceVertexIndices_[geometryIndex].append(indexById[polygonVertexIds[offset + j]]);
                }

                offset += count;
            }
        }

        // Different vertices: the maps must be re-read and the simulation restarted.
        markAllMapsDirty(geometryIndex);
        initialized_[geometryIndex] = false;
    }


    // ---- 3. Time difference since the last evaluation ----
    //   0           -> same frame: no step, the simulation follows the object (see step 7)
    //   1           -> next frame: simulate one step
    //   > 1 or < 0  -> skipped frames or went backward: reset

    double timeDifference = frame - lastFrame_[geometryIndex];

    if (timeDifference > 1.0 || timeDifference < 0.0 || frame <= startFrame)
        initialized_[geometryIndex] = false;


    // ---- 4. Reset the simulation to the goal ----

    if (!initialized_[geometryIndex])
    {
        positions_[geometryIndex]   = goal;
        lastGoal_[geometryIndex]    = goal;
        velocities_[geometryIndex]  = MVectorArray(vertexCount, MVector::zero);
        lastFrame_[geometryIndex]   = frame;
        initialized_[geometryIndex] = true;
        return MS::kSuccess;
    }


    // ---- 5. Read the per-geometry sliders ----
    // perGeometry[geometryIndex] may not exist yet if the user never touched it.

    float weight    = kDefaultWeight;
    float stiffness = kDefaultStiffness;
    float damping   = kDefaultDamping;

    MArrayDataHandle perGeometryHandle = data.inputArrayValue(aPerGeometry);
    bool hasPerGeometry = (perGeometryHandle.jumpToElement(geometryIndex) == MS::kSuccess);

    if (hasPerGeometry)
    {
        MDataHandle geometryHandle = perGeometryHandle.inputValue();
        weight    = geometryHandle.child(aWeight).asFloat();
        stiffness = geometryHandle.child(aStiffness).asFloat();
        damping   = geometryHandle.child(aDamping).asFloat();
    }


    // ---- 6. Copy the maps from Maya, only the ones that changed ----

    int maxVertexId = maxVertexId_[geometryIndex];

    // Built-in weight map
    if (weightMapDirty_[geometryIndex])
    {
        weightMap_[geometryIndex].setLength(vertexCount);

        for (unsigned int i = 0; i < vertexCount; ++i)
            weightMap_[geometryIndex][i] = weightValue(data, geometryIndex, membership_[geometryIndex][i]);

        weightMapDirty_[geometryIndex] = false;
    }

    // Custom maps:  perGeometry[geometryIndex].stiffnessMap / dampingMap
    //
    // Each map is sparse: vertices that were never painted have no element and count as 1.0.
    // So for each map we:
    //   a) fill 'valueById' with 1.0 for every vertex id
    //   b) overwrite the painted vertices with their painted value
    //   c) copy into our map in iteration order (element i = membership_[geometryIndex][i])

    // Stiffness map
    if (stiffnessMapDirty_[geometryIndex])
    {
        MFloatArray valueById(maxVertexId + 1, 1.0f);

        if (hasPerGeometry)
        {
            MArrayDataHandle mapHandle(perGeometryHandle.inputValue().child(aStiffnessMap));
            for (unsigned int e = 0; e < mapHandle.elementCount(); ++e)
            {
                mapHandle.jumpToArrayElement(e);
                int vertexId = mapHandle.elementIndex();
                if (vertexId <= maxVertexId)
                    valueById[vertexId] = mapHandle.inputValue().asFloat();
            }
        }

        stiffnessMap_[geometryIndex].setLength(vertexCount);

        for (unsigned int i = 0; i < vertexCount; ++i)
            stiffnessMap_[geometryIndex][i] = std::clamp(valueById[membership_[geometryIndex][i]], 0.0f, 1.0f);

        stiffnessMapDirty_[geometryIndex] = false;
    }

    // Damping map
    if (dampingMapDirty_[geometryIndex])
    {
        MFloatArray valueById(maxVertexId + 1, 1.0f);

        if (hasPerGeometry)
        {
            MArrayDataHandle mapHandle(perGeometryHandle.inputValue().child(aDampingMap));
            for (unsigned int e = 0; e < mapHandle.elementCount(); ++e)
            {
                mapHandle.jumpToArrayElement(e);
                int vertexId = mapHandle.elementIndex();
                if (vertexId <= maxVertexId)
                    valueById[vertexId] = mapHandle.inputValue().asFloat();
            }
        }

        dampingMap_[geometryIndex].setLength(vertexCount);

        for (unsigned int i = 0; i < vertexCount; ++i)
            dampingMap_[geometryIndex][i] = std::clamp(valueById[membership_[geometryIndex][i]], 0.0f, 1.0f);

        dampingMapDirty_[geometryIndex] = false;
    }


    // ---- 7. Simulate ----

    // Same frame, but the object moved (playback off, user moving it):
    // no simulation step, just move the simulated points by the same amount
    // as the goal, so the mesh follows the object and keeps its current jiggle.
    if (timeDifference == 0.0)
    {
        for (unsigned int i = 0; i < vertexCount; ++i)
        {
            MVector goalMove = goal[i] - lastGoal_[geometryIndex][i];
            positions_[geometryIndex][i] = positions_[geometryIndex][i] + goalMove;
        }
    }

    // Next frame: one simulation step
    else
    {
        for (unsigned int i = 0; i < vertexCount; ++i)
        {
            // magnitude (global)  x  slider (per geometry)  x  map (per vertex)
            double k = stiffnessMagnitude * stiffness * stiffnessMap_[geometryIndex][i];
            double d = dampingMagnitude   * damping   * dampingMap_[geometryIndex][i];

            // Keep them in 0-1: damping above 1 would flip the velocity and explode.
            k = std::clamp(k, 0.0, 1.0);
            d = std::clamp(d, 0.0, 1.0);

            // Spring pulls the point toward the goal, damping removes energy.
            MVector pull = (goal[i] - positions_[geometryIndex][i]) * k;

            velocities_[geometryIndex][i] = (velocities_[geometryIndex][i] + pull) * (1.0 - d);
            positions_[geometryIndex][i]  = positions_[geometryIndex][i] + velocities_[geometryIndex][i];
        }
    }


    // ---- 8. Normals and edge lengths of the goal ----
    // Face normal = sum of cross products over a fan from the first vertex.
    // It is not normalized: its length grows with the face area, so big faces
    // count more when they are added together into the vertex normals.

    unsigned int faceCount = faceVertexCounts_[geometryIndex].length();

    MVectorArray goalFaceNormals(faceCount, MVector::zero);
    MVectorArray goalVertexNormals(vertexCount, MVector::zero);
    MDoubleArray edgeLengthSum(vertexCount, 0.0);
    MIntArray    edgeCount(vertexCount, 0);

    unsigned int offset = 0;
    for (unsigned int f = 0; f < faceCount; ++f)
    {
        int count = faceVertexCounts_[geometryIndex][f];
        int first = faceVertexIndices_[geometryIndex][offset];

        for (int j = 1; j < count - 1; ++j)
        {
            int a = faceVertexIndices_[geometryIndex][offset + j];
            int b = faceVertexIndices_[geometryIndex][offset + j + 1];
            goalFaceNormals[f] += (goal[a] - goal[first]) ^ (goal[b] - goal[first]);
        }

        for (int j = 0; j < count; ++j)
        {
            int a = faceVertexIndices_[geometryIndex][offset + j];
            int b = faceVertexIndices_[geometryIndex][offset + (j + 1) % count];

            goalVertexNormals[a] += goalFaceNormals[f];

            // Edge a-b: add its length to both ends
            double length = goal[a].distanceTo(goal[b]);
            edgeLengthSum[a] += length;
            edgeLengthSum[b] += length;
            edgeCount[a] += 1;
            edgeCount[b] += 1;
        }

        offset += count;
    }

    for (unsigned int i = 0; i < vertexCount; ++i)
    {
        if (goalVertexNormals[i].length() > 0.0)
            goalVertexNormals[i].normalize();
    }


    // ---- 9. Limit how far a vertex can sink into the surface ----
    // Split the jiggle offset along the goal normal:
    //   positive = outward  -> always allowed
    //   negative = inward   -> at most  maxInward x (average edge length around the vertex)
    // Vertices without faces have a zero normal and are left alone.

    for (unsigned int i = 0; i < vertexCount; ++i)
    {
        if (edgeCount[i] == 0)
            continue;

        double limit  = maxInward * edgeLengthSum[i] / edgeCount[i];
        MVector offsetFromGoal = positions_[geometryIndex][i] - goal[i];
        double inward = offsetFromGoal * goalVertexNormals[i];   // dot product

        if (inward < -limit)
        {
            // Push the point back out to the limit
            positions_[geometryIndex][i] = positions_[geometryIndex][i] + goalVertexNormals[i] * (-limit - inward);

            // Remove the inward part of the velocity, so it does not push in again next frame
            double inwardSpeed = velocities_[geometryIndex][i] * goalVertexNormals[i];
            if (inwardSpeed < 0.0)
                velocities_[geometryIndex][i] = velocities_[geometryIndex][i] - goalVertexNormals[i] * inwardSpeed;
        }
    }


    // ---- 10. Fix folded faces ----
    // A face is folded when its simulated normal points against its goal normal
    // (dot product below 0).  Every vertex of a folded face is pulled toward the
    // goal and slowed down.  Fixing one face can fold its neighbour, so repeat
    // a few times, and stop early when nothing is folded anymore.

    for (int iteration = 0; iteration < foldIterations; ++iteration)
    {
        MIntArray folded(vertexCount, 0);
        bool anyFolded = false;

        offset = 0;
        for (unsigned int f = 0; f < faceCount; ++f)
        {
            int count = faceVertexCounts_[geometryIndex][f];
            int first = faceVertexIndices_[geometryIndex][offset];

            MVector simNormal = MVector::zero;
            for (int j = 1; j < count - 1; ++j)
            {
                int a = faceVertexIndices_[geometryIndex][offset + j];
                int b = faceVertexIndices_[geometryIndex][offset + j + 1];
                simNormal += (positions_[geometryIndex][a] - positions_[geometryIndex][first])
                           ^ (positions_[geometryIndex][b] - positions_[geometryIndex][first]);
            }

            if (simNormal * goalFaceNormals[f] < 0.0)
            {
                anyFolded = true;
                for (int j = 0; j < count; ++j)
                    folded[faceVertexIndices_[geometryIndex][offset + j]] = 1;
            }

            offset += count;
        }

        if (!anyFolded)
            break;

        for (unsigned int i = 0; i < vertexCount; ++i)
        {
            if (folded[i] == 1)
            {
                positions_[geometryIndex][i]  = positions_[geometryIndex][i] + (goal[i] - positions_[geometryIndex][i]) * foldCorrection;
                velocities_[geometryIndex][i] = velocities_[geometryIndex][i] * (1.0 - foldCorrection);
            }
        }
    }

    lastFrame_[geometryIndex] = frame;
    lastGoal_[geometryIndex]  = goal;


    // ---- 11. Output: blend from the goal toward the simulation, back to object space ----

    MMatrix worldToLocal = localToWorld.inverse();
    MPointArray result(vertexCount);

    for (unsigned int i = 0; i < vertexCount; ++i)
    {
        float amount = envelopeValue * weight * weightMap_[geometryIndex][i];

        MPoint blended = goal[i] + (positions_[geometryIndex][i] - goal[i]) * amount;
        result[i] = blended * worldToLocal;
    }

    itGeo.setAllPositions(result);

    return MS::kSuccess;
}

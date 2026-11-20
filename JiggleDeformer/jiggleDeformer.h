#pragma once

#include <maya/MPxDeformerNode.h>
#include <maya/MTypeId.h>
#include <maya/MPointArray.h>
#include <maya/MVectorArray.h>
#include <maya/MFloatArray.h>
#include <maya/MIntArray.h>

#include <map>


class JiggleDeformer : public MPxDeformerNode
{
public:
    static void*   creator();
    static MStatus initialize();

    MStatus deform(MDataBlock& data, MItGeometry& itGeo,
                   const MMatrix& localToWorld, unsigned int geometryIndex) override;

    MStatus setDependentsDirty(const MPlug& plug, MPlugArray& affectedPlugs) override;

    static const MTypeId kTypeId;

    // ---- Global attributes ----
    static MObject aEnable;
    static MObject aTime;
    static MObject aStartFrame;
    static MObject aStiffnessMagnitude;
    static MObject aDampingMagnitude;

    // ---- Per geometry attributes:  perGeometry[i] matches input[i] ----
    static MObject aPerGeometry;
    static MObject aWeight;
    static MObject aStiffness;
    static MObject aDamping;
    static MObject aStiffnessMap;
    static MObject aDampingMap;
    static MObject aWorldMatrix;

private:
    void markAllMapsDirty(unsigned int geometryIndex);

    // Every map below is keyed by the geometry index (the i in input[i]),
    // so each connected geometry has its own values.
    // Per-vertex arrays are in iteration order: element v belongs to membership_[i][v].

    // ---- Simulation state (must survive from one frame to the next) ----
    std::map<unsigned int, MPointArray>  positions_;      // world space
    std::map<unsigned int, MPointArray>  lastGoal_;       // world space goal of the last evaluation
    std::map<unsigned int, MVectorArray> velocities_;
    std::map<unsigned int, double>       lastFrame_;
    std::map<unsigned int, bool>         initialized_;

    // ---- Vertices affected by the deformer ----
    std::map<unsigned int, MIntArray>    membership_;
    std::map<unsigned int, int>          maxVertexId_;

    // ---- Painted maps (cached, only re-read when dirty) ----
    std::map<unsigned int, MFloatArray>  weightMap_;      // Maya's built-in weightList
    std::map<unsigned int, MFloatArray>  stiffnessMap_;
    std::map<unsigned int, MFloatArray>  dampingMap_;   

    std::map<unsigned int, bool>         weightMapDirty_;
    std::map<unsigned int, bool>         stiffnessMapDirty_;
    std::map<unsigned int, bool>         dampingMapDirty_;
};

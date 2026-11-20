#include "jiggleDeformer.h"

#include <maya/MFnPlugin.h>
#include <maya/MGlobal.h>


MStatus initializePlugin(MObject obj)
{
    MStatus status;
    MFnPlugin plugin(obj, "JiggleDeformer", "1.0", "Any");

    // Register the node
    status = plugin.registerNode("jiggleDeformer", JiggleDeformer::kTypeId,
                                 JiggleDeformer::creator, JiggleDeformer::initialize,
                                 MPxNode::kDeformerNode);
    CHECK_MSTATUS_AND_RETURN_IT(status);

    // Make the maps paintable with the Paint Attributes Tool
    MGlobal::executeCommand("makePaintable -attrType multiFloat -sm deformer jiggleDeformer weights;");
    MGlobal::executeCommand("makePaintable -attrType multiFloat -sm deformer jiggleDeformer stiffnessMap;");
    MGlobal::executeCommand("makePaintable -attrType multiFloat -sm deformer jiggleDeformer dampingMap;");

    // MEL command:  createJiggleDeformer()
    // Creates the deformer on the selection, connects the scene time to it,
    // and connects each object's world matrix to its perGeometry[i].worldMatrix.
    MGlobal::executeCommand(
        "global proc string createJiggleDeformer()\n"
        "{\n"
        "    string $nodes[] = `deformer -type jiggleDeformer`;\n"
        "    if (!size($nodes)) return \"\";\n"
        "    string $node = $nodes[0];\n"
        "\n"
        "    connectAttr -f time1.outTime ($node + \".time\");\n"
        "\n"
        "    string $shapes[]  = `deformer -q -g $node`;\n"
        "    int    $indices[] = `deformer -q -gi $node`;\n"
        "    int $i;\n"
        "    for ($i = 0; $i < size($shapes); $i++)\n"
        "        connectAttr -f ($shapes[$i] + \".worldMatrix[0]\") ($node + \".perGeometry[\" + $indices[$i] + \"].worldMatrix\");\n"
        "\n"
        "    select -r $node;\n"
        "    return $node;\n"
        "}\n");

    return MS::kSuccess;
}

MStatus uninitializePlugin(MObject obj)
{
    MStatus status;
    MFnPlugin plugin(obj);

    // Remove the paintable maps
    MGlobal::executeCommand("makePaintable -remove jiggleDeformer weights;");
    MGlobal::executeCommand("makePaintable -remove jiggleDeformer stiffnessMap;");
    MGlobal::executeCommand("makePaintable -remove jiggleDeformer dampingMap;");

    // Deregister the node
    status = plugin.deregisterNode(JiggleDeformer::kTypeId);
    CHECK_MSTATUS_AND_RETURN_IT(status);

    return MS::kSuccess;
}

#ifndef VRVIEW_H
#define VRVIEW_H
#ifndef PSX
// Head-tracked game camera for the OpenXR mode (see utils/vr_openxr.h).
bool VRView_Apply(int view);   // after InitCamera() in RenderGame2: adds the head pose
void VRView_Restore();         // end of RenderGame2: restores the game's projection distance
bool VRView_Tracked();         // the frame being drawn is head-tracked
int VRView_FrAng();            // horizontal culling half-angle covering the headset view
#endif
#endif

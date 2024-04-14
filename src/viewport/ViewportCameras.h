#pragma once
#include <unordered_map>
#include <string>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/camera.h>

PXR_NAMESPACE_USING_DIRECTIVE
//
// Manage and store the cameras owned by a viewport.
// A cameras can be a stage camera or an internal viewport camera.
// Only perspective iternal camera is implemented as of now.
//
// This class also provide a UI to select the current camera.
//
class ViewportCameras {
public:
    ViewportCameras();

    // The update function is called before every rendered frame.
    // This updates the internals of this structure following the changes that
    // happened between the previous frame and now
    void Update(const UsdStageRefPtr &stage, UsdTimeCode tc);
    
    // Change the camera aspect ratio
    void SetCameraAspectRatio(int width, int height);
    
    //
    // UI helpers
    //
    void DrawCameraList(const UsdStageRefPtr &stage);
    void DrawCameraEditor(const UsdStageRefPtr &stage, UsdTimeCode tc);
    
    // Accessors
    std::string GetCurrentCameraName() const;
    
    // Returning an editable camera which can be modified externaly by manipulators or any functions,
    // in the viewport. This will likely be reset at each frame. This is not thread safe and should be used only
    // in the main render loop.
    GfCamera & GetEditableCamera() { return *_renderCamera; }
    
    // Same as above but const
    const GfCamera & GetCurrentCamera() const { return *_renderCamera; }
       
    //
    const SdfPath & GetStageCameraPath() const ;

    // Used in the manipulators for editing stage
    // TODO IsEditingStageCamera()
    inline bool IsUsingStageCamera () const {
        return _currentConfig->_renderCameraType == StageCamera;
    }
    
private:
    inline bool IsPerspective() const { return _currentConfig->_renderCameraType == ViewportPerspective;}
   
    // Set a new Stage camera path
    void UseStageCamera(const UsdStageRefPtr &stage, const SdfPath &cameraPath);
    
    // Set the viewport camera
    void UseViewportPerspCamera(const UsdStageRefPtr &stage);

    // Points to a valid camera, stage or perspective that we share with the rest of the application
    GfCamera *_renderCamera;
    // Points to the current persp camera belonging to the stage
    GfCamera *_perspCamera;
    // A copy of the current stage camera used for editing
    GfCamera _stageCamera;
    // Keep track of the current stage to know when the stage has changed
    UsdStageRefPtr _currentStage;
    
    
    // Common to all stages, the perpective and ortho cams
    struct OwnedCameras {
        GfCamera _perspectiveCamera;
        // TODO Ortho cameras
    };
    
    
    // Internal viewport cameras
    static std::unordered_map<std::string, OwnedCameras> _viewportCamerasPerStage;

    enum CameraType {ViewportPerspective, StageCamera};
    struct CameraConfiguration {
        SdfPath _stageCameraPath = SdfPath::EmptyPath();
        // we keep track of the render camera type
        CameraType _renderCameraType = CameraType::ViewportPerspective;
    };
    // Per viewport we want to select particular camera
    // We want to keep the camera configuration per stage on each viewport
    std::unordered_map<std::string, CameraConfiguration> _perStageConfiguration;
    
    CameraConfiguration *_currentConfig = nullptr;

};

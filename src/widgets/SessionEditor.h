#pragma once
#include <pxr/usd/usd/stage.h>
#include "Selection.h" // TODO: ideally we should have only pxr headers here

PXR_NAMESPACE_USING_DIRECTIVE

void DrawSessionEditor(UsdStageRefPtr stage, Selection &selectedPaths);

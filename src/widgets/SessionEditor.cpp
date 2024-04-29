#include "SessionEditor.h"

#include <iostream>

#include "Commands.h"
#include "Gui.h"
#include "ImGuiHelpers.h"

// The following include contains the code which writes usd to text, but it's not
// distributed with the api
//#include <pxr/usd/sdf/fileIO_Common.h>

static std::string _session_log;


void DrawSessionEditor(UsdStageRefPtr stage, Selection &selectedPaths) {
    static std::string _session_uuid;

    _session_log += "line\n";

    ImGuiIO &io = ImGui::GetIO();
    ImGuiWindow *window = ImGui::GetCurrentWindow();

    ImGui::InputText("Session UUID", &_session_uuid);
    ImGui::Button("Connect");

    static char buf3[1024];
    ImGui::InputTextMultiline("", &_session_log,
                              ImVec2(ImGui::GetContentRegionMax().x, ImGui::GetContentRegionMax().y - ImGui::GetCursorPosY()),
                              ImGuiInputTextFlags_ReadOnly);
}

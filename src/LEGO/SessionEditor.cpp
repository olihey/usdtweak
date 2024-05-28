#define WIN32_LEAN_AND_MEAN
#define _WEBSOCKETPP_CPP11_RANDOM_DEVICE_
#include "SessionEditor.h"

#include <mutex>
#include <iostream>

#include <websocketpp/config/asio_no_tls_client.hpp>
#include <websocketpp/client.hpp>

#include <pxr/base/gf/matrix4f.h>

#include "Commands.h"
#include "Gui.h"
#include "ImGuiHelpers.h"
#include "json.hpp"
using json = nlohmann::json;

// The following include contains the code which writes usd to text, but it's not
// distributed with the api
//#include <pxr/usd/sdf/fileIO_Common.h>

static std::string _session_log;
static std::string _session_uuid = "96612afd-41a6-4619-b9d5-161e83a6e4e4";

std::once_flag _web_socket_init;
websocketpp::client<websocketpp::config::asio_client> _web_socket_client;
websocketpp::client<websocketpp::config::asio_client>::connection_ptr _web_socket_connection = nullptr; 

using websocketpp::lib::placeholders::_1;
using websocketpp::lib::placeholders::_2;
using websocketpp::lib::bind;

std::mutex _mutex;
std::vector<json> usd_commands;

// BAD
UsdStageRefPtr _stage = nullptr;

std::vector<std::string> split(std::string s, std::string delimiter, const size_t max_split = 0) {
    size_t pos_start = 0, pos_end, delim_len = delimiter.length();
    std::string token;
    std::vector<std::string> res;

    size_t split_counter = 0;
    while ((pos_end = s.find(delimiter, pos_start)) != std::string::npos) {
        token = s.substr(pos_start, pos_end - pos_start);
        pos_start = pos_end + delim_len;
        res.push_back(token);

        split_counter++;
        if (split_counter == max_split) {
            break;
        }
    }

    res.push_back(s.substr(pos_start));
    return res;
}

void build_usd(json usd_node, pxr::UsdPrim parent_prim) {
//    std::cout << usd_node << std::endl; 

    const std::string usd_node_name = usd_node["name"];
    pxr::SdfPath prim_path = parent_prim.GetPath().AppendPath(pxr::SdfPath(usd_node_name));
    json json_node_type = usd_node["prim_type"];

    pxr::UsdPrim prim;
    if (json_node_type.is_null()) {
        prim = _stage->DefinePrim(prim_path);
    } else {
        prim = _stage->DefinePrim(prim_path, TfToken(json_node_type.template get<std::string>()));
    }

    json json_prim_kind = usd_node["kind"];
    if (!json_prim_kind.is_null()) {
        prim.SetKind(TfToken(json_prim_kind.template get<std::string>()));
    }

    json prim_references = usd_node["references"];
    for (auto &[prim_reference_type, references] : prim_references.items()) {
        // here is stuff missing
        if (prim_reference_type == "prepended") {
            for (auto &reference : references) {
                pxr::SdfPath asset_prim_path;
                if (!reference["prim_path"].is_null()) {
                    asset_prim_path = pxr::SdfPath(reference["prim_path"].template get<std::string>());
                }

                prim.GetReferences().AddReference(reference["asset"], asset_prim_path);
            }
        }
    }

    for (auto &attribute : usd_node["attributes"]) {
        const std::string attribute_type = attribute["attribute_type"];
        bool is_array = false;
        if (!attribute["is_array"].is_null()) {
            is_array = attribute["is_array"];
        }
        if (attribute_type == "token") {
            pxr::UsdAttribute usd_attribute =
                prim.CreateAttribute(pxr::TfToken(attribute["name"].template get<std::string>()), is_array ? pxr::SdfValueTypeNames->TokenArray : pxr::SdfValueTypeNames->Token);
            if (is_array) {
                pxr::VtArray<TfToken> array;
                for (auto &attribute_value : attribute["value"]) {
                    array.push_back(TfToken(attribute_value.template get<std::string>()));
                }
                usd_attribute.Set(array);           
            } else {
                usd_attribute.Set(TfToken(attribute["value"][0].template get<std::string>()));
            }
        } else if (attribute_type == "string") {
            pxr::UsdAttribute usd_attribute =
                prim.CreateAttribute(pxr::TfToken(attribute["name"].template get<std::string>()),
                                     is_array ? pxr::SdfValueTypeNames->StringArray : pxr::SdfValueTypeNames->String);
            if (is_array) {
                usd_attribute.Set(pxr::VtArray<TfToken>());
            } else {
                usd_attribute.Set(attribute["value"][0].template get<std::string>());
            }
        } else if (attribute_type == "matrix4d") {
            pxr::UsdAttribute usd_attribute =
                prim.CreateAttribute(pxr::TfToken(attribute["name"].template get<std::string>()),
                                     is_array ? pxr::SdfValueTypeNames->Matrix4dArray : pxr::SdfValueTypeNames->Matrix4d);
            if (is_array) {
                usd_attribute.Set(pxr::VtArray<TfToken>());
            } else {
                json jm = attribute["value"];
                GfMatrix4d matrix(jm[0].template get<double>(), jm[1].template get<double>(), jm[2].template get<double>(),
                                  jm[3].template get<double>(), jm[4].template get<double>(), jm[5].template get<double>(),
                                  jm[6].template get<double>(), jm[7].template get<double>(), jm[8].template get<double>(),
                                  jm[9].template get<double>(), jm[10].template get<double>(), jm[11].template get<double>(),
                                  jm[12].template get<double>(), jm[13].template get<double>(), jm[14].template get<double>(),
                                  jm[15].template get<double>()
                );
                usd_attribute.Set(matrix);
            }
        } else {
            std::cout << attribute << std::endl;
        }
    }

    for (auto &child : usd_node["children"]) {
        build_usd(child, prim);
    }
}

void on_message(websocketpp::client<websocketpp::config::asio_client> *c, websocketpp::connection_hdl hdl,
                websocketpp::config::asio_client::message_type::ptr msg) {
//    std::cout << "on_message called with hdl: " << hdl.lock().get() << " and message: " << msg->get_payload() << std::endl;
    websocketpp::lib::error_code ec;
    const std::string message = msg->get_payload();
//    _session_log += message + "\n";

    if (_stage == nullptr) {
        return;
    }

    std::vector<std::string> comand = split(message, " ", 1);
//    std::cout << message << std::endl;

    if (comand.at(0) == "HELLO") {
        c->send(hdl, "MODE USD", websocketpp::frame::opcode::text);
    } else if (comand.at(0) == "USD") {
        json usd_data;
        try {
            usd_data = json::parse(comand.at(1));
        }
        catch (const json::exception &e) {
            // output exception information
            std::cout << "message: " << e.what() << '\n' << "exception id: " << e.id << std::endl;
            return;
        }

        { 
            std::lock_guard<std::mutex> lock_guard(_mutex);
            usd_commands.push_back(usd_data);
        }
    }

        //    else if (comand.at(0) == "ITEM_CHANGED") {
//        std::vector<std::string> comand = split(message, " ", 3);
//
//        std::string prim_uuid = comand.at(1);
//        std::replace(prim_uuid.begin(), prim_uuid.end(), '-', '_');
//        SdfPath attribute_path(std::string("/model/item_") + prim_uuid + ".xformOp:transform");
//
//        UsdAttribute attribute = _stage->GetAttributeAtPath(attribute_path);
//        if (!attribute.IsValid()) {
//            return;
//        }
//
////        std::cout << comand.at(1) << std::endl;
//        json command_data;
//        try {
//            command_data = json::parse(comand.at(2));
//        }
//        catch (const json::exception &e) {
//            // output exception information
//            std::cout << "message: " << e.what() << '\n' << "exception id: " << e.id << std::endl;
//            return;
//        }
//
//        const auto transformation = command_data["transformation"];
//        if (transformation == nullptr) {
//            return;
//        }
//
//        GfMatrix4d mat(transformation.at(0), transformation.at(1), transformation.at(2), transformation.at(3),
//                       transformation.at(4), transformation.at(5), transformation.at(6), transformation.at(3),
//                       transformation.at(8), transformation.at(9), transformation.at(10), transformation.at(11),
//                       transformation.at(12), transformation.at(13), transformation.at(14), transformation.at(15));
//
//        attribute.Set(mat);
//    }
}

void _web_socket_thread() {
    _web_socket_client.run();
    _web_socket_connection = nullptr;
}

void DrawSessionEditor(UsdStageRefPtr stage, Selection &selectedPaths) {

    std::call_once(_web_socket_init, [] {
//        _web_socket_client.set_access_channels(websocketpp::log::alevel::all);
//        _web_socket_client.clear_access_channels(websocketpp::log::alevel::frame_payload);

        // Initialize ASIO
        _web_socket_client.init_asio();

        // Register our message handler
        _web_socket_client.set_message_handler(bind(&on_message, &_web_socket_client, ::_1, ::_2));
       });

      if (_stage != nullptr) { 
        std::lock_guard<std::mutex> lock_guard(_mutex);

        pxr::SdfPath model_prim_path("/model");
        pxr::UsdPrim model_prim = _stage->GetPrimAtPath(model_prim_path);
        for (json usd_data : usd_commands) {
            for (auto &usd_command_data : usd_data) {
                const std::string usd_command = usd_command_data["command"];

                if (usd_command == "remove") {
                    for (auto &usd_node : usd_command_data["nodes"]) {
                        const std::string usd_node_name = usd_node["name"];
                        auto prim_path = model_prim_path.AppendPath(pxr::SdfPath(usd_node_name));
                        pxr::UsdPrim prim = _stage->GetPrimAtPath(prim_path);
                        if (prim.IsValid()) {
                            _stage->RemovePrim(prim_path);
                        }
                    }
                } else if (usd_command == "replace") {
                    for (auto &usd_node : usd_command_data["nodes"]) {
                        const std::string usd_node_name = usd_node["name"];
                        auto prim_path = model_prim_path.AppendPath(pxr::SdfPath(usd_node_name));
                        pxr::UsdPrim prim = _stage->GetPrimAtPath(prim_path);
                        if (_stage->GetPrimAtPath(prim_path).IsValid()) {
                            _stage->RemovePrim(prim_path);
                        }

                        build_usd(usd_node, model_prim);
                    }
                }
            }
        }

        usd_commands.clear();
    }


    ImGuiIO &io = ImGui::GetIO();
    ImGuiWindow *window = ImGui::GetCurrentWindow();

    ImGui::InputText("Session UUID", &_session_uuid);
    if (_web_socket_connection != nullptr) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Connect")) {
        websocketpp::lib::error_code web_socket_error_code;
        _web_socket_connection = _web_socket_client.get_connection(std::string("ws://gamepc:8000/livedm/") + _session_uuid +
                                                                   "/connect",
                                          web_socket_error_code);
        if (web_socket_error_code) {
            std::cout << "could not create connection because: " << web_socket_error_code.message() << std::endl;
        }
        // Note that connect here only requests a connection. No network messages are
        // exchanged until the event loop starts running in the next line.
        _web_socket_client.connect(_web_socket_connection);

        // BAD

        _stage = stage;


        // Start the ASIO io_service run loop
        // this will cause a single connection to be made to the server. c.run()
        // will exit when this connection is closed.
        websocketpp::lib::thread asio_thread(_web_socket_thread);
        asio_thread.detach(); // or asio_thread.join(); if you want it running in the main thread
    }
    if (_web_socket_connection != nullptr) {
        ImGui::EndDisabled();
    }

    // the lob UI
    ImGui::BeginDisabled();
    ImGui::InputTextMultiline("", &_session_log,
                              ImVec2(ImGui::GetContentRegionMax().x, ImGui::GetContentRegionMax().y - ImGui::GetCursorPosY()),
                              ImGuiInputTextFlags_ReadOnly);
    ImGui::EndDisabled();
}

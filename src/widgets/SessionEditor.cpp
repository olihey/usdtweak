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

void on_message(websocketpp::client<websocketpp::config::asio_client> *c, websocketpp::connection_hdl hdl,
                websocketpp::config::asio_client::message_type::ptr msg) {
//    std::cout << "on_message called with hdl: " << hdl.lock().get() << " and message: " << msg->get_payload() << std::endl;
    websocketpp::lib::error_code ec;
    const std::string message = msg->get_payload();
    _session_log += message + "\n";

    if (_stage == nullptr) {
        return;
    }

    std::vector<std::string> comand = split(message, " ", 1);
//    std::cout << comand.size() << std::endl;

    if (comand.at(0) == "ITEM_CHANGED") {
        std::vector<std::string> comand = split(message, " ", 3);

        std::string prim_uuid = comand.at(1);
        std::replace(prim_uuid.begin(), prim_uuid.end(), '-', '_');
        SdfPath attribute_path(std::string("/model/item_") + prim_uuid + ".xformOp:transform");

        UsdAttribute attribute = _stage->GetAttributeAtPath(attribute_path);
        if (!attribute.IsValid()) {
            return;
        }

//        std::cout << comand.at(1) << std::endl;
        json command_data;
        try {
            command_data = json::parse(comand.at(2));
        }
        catch (const json::exception &e) {
            // output exception information
            std::cout << "message: " << e.what() << '\n' << "exception id: " << e.id << std::endl;
            return;
        }

        const auto transformation = command_data["transformation"];
        if (transformation == nullptr) {
            return;
        }

        GfMatrix4d mat(transformation.at(0), transformation.at(1), transformation.at(2), transformation.at(3),
                       transformation.at(4), transformation.at(5), transformation.at(6), transformation.at(3),
                       transformation.at(8), transformation.at(9), transformation.at(10), transformation.at(11),
                       transformation.at(12), transformation.at(13), transformation.at(14), transformation.at(15));

        attribute.Set(mat);
    }
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

    ImGuiIO &io = ImGui::GetIO();
    ImGuiWindow *window = ImGui::GetCurrentWindow();

    ImGui::InputText("Session UUID", &_session_uuid);
    if (_web_socket_connection != nullptr) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Connect")) {
        websocketpp::lib::error_code web_socket_error_code;
        _web_socket_connection = _web_socket_client.get_connection(std::string("ws://127.0.0.1:8000/livedm/") + _session_uuid +
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

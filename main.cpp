#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <filesystem>
#include <windows.h>
#include <tlhelp32.h>
#include <winnt.h>
#include "sol/sol.hpp"
#include "KeyValue.hpp"

struct Manifest {
    std::string *name;
    std::string appid;
    std::string path;
};
const std::string STEAM_PATH = "C:\\Program Files (x86)\\Steam";

void addappid(uint32_t appid) {
    std::string app_list_path = STEAM_PATH + "\\AppList";
    if (!std::filesystem::exists(app_list_path)) {
        std::filesystem::create_directory(app_list_path);
    }

    for (const auto& entry : std::filesystem::directory_iterator(app_list_path)) {
        if (entry.is_regular_file()) {
            std::ifstream file(entry.path());
            std::string content;
            std::getline(file, content);
            if (content == std::to_string(appid)) {
                std::cout << "AppID " << appid << " already exists in " << entry.path().filename() << ", skipping." << std::endl;
                return;
            }
        }
    }

    // AppID doesn't exist, create new file
    int total_files = std::count_if(std::filesystem::directory_iterator(app_list_path), std::filesystem::directory_iterator{}, [](const auto& entry) {
        return entry.is_regular_file();
    });
    std::string app_list_file_path = app_list_path + "\\" + std::to_string(total_files) + ".txt";
    std::ofstream app_list_file(app_list_file_path);
    app_list_file << appid;
    app_list_file.close();
    std::cout << "Created new AppID entry: " << app_list_file_path << std::endl;
}

void addHash(uint32_t app_id, std::string hash) {
    std::cout << "Adding hash for appid: " << app_id << " with hash: " << hash << std::endl;
    std::ifstream file(STEAM_PATH + "\\config\\config.vdf");
    std::string content{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};

    KeyValueRoot kv;
    kv.Parse(content.c_str());
    file.close();

    auto& steam = kv["InstallConfigStore"]["Software"]["Valve"]["Steam"];
    if (steam["depots"].ChildCount() == 0) {
        steam.AddNode("depots");
    }

    if (steam["depots"][std::to_string(app_id).c_str()].ChildCount() == 0) {    
        steam["depots"].AddNode(std::to_string(app_id).c_str())->Add("DecryptionKey", hash.c_str());
    } else {
        std::cout << "Depot already exists for appid: " << app_id << std::endl;
    }

    std::ofstream out_file(STEAM_PATH + "\\config\\config.vdf");
    out_file << kv.ToString();
    out_file.close();
}

void setManifestid(int curr_appid, int appid, std::string hash) {
    auto manifest = "manifests/" + std::to_string(curr_appid) + "/" + std::to_string(appid) + "_" + hash + ".manifest";
    auto depotcache = STEAM_PATH + "\\depotcache";
    auto dest_file = depotcache + "\\" + std::to_string(appid) + "_" + hash + ".manifest";

    if (!std::filesystem::exists(depotcache)) {
        std::filesystem::create_directory(depotcache);
    }
    
    if (std::filesystem::exists(dest_file)) {
        std::filesystem::remove(dest_file);
    }

    std::cout << "Copying " << manifest << " to " << depotcache << std::endl;
    std::filesystem::copy_file(manifest, dest_file);
}

sol::state setup_lua() {
    sol::state lua{};
    lua.open_libraries(sol::lib::base);

    auto curr_appid = 0;
    lua["addappid"] = sol::overload(
        [&curr_appid](uint32_t appid) {
            curr_appid = appid;
            addappid(appid);
        },
        [](uint32_t appid, int type, const std::string& hash) {
            addappid(appid);
            addHash(appid, hash);
        }
    );

    lua["setManifestid"] = sol::overload(
        [&curr_appid](int appid, std::string hash, int arg_3) {
            setManifestid(curr_appid, appid, hash);
        },
        [&curr_appid](int appid, std::string hash) {
            setManifestid(curr_appid, appid, hash);
        }
    );
    return lua;
}

sol::state lua = setup_lua();

std::vector<Manifest> get_manifests() {
    std::vector<Manifest> manifests;
    std::string manifests_path = "./manifests";

    for (const auto & entry : std::filesystem::directory_iterator(manifests_path)) {
        std::string path = entry.path().string();
        std::string appid = entry.path().filename().string();
        std::cout << appid << std::endl;
        manifests.push_back({nullptr, appid, path});
    }

    return manifests;
}

void install(Manifest manifest) {
    auto user32_path = STEAM_PATH + "\\user32.dll";
    if (!std::filesystem::exists(user32_path)) {
        std::cout << "user32.dll not found in steam path" << std::endl;
        std::filesystem::copy_file("user32.dll", user32_path, std::filesystem::copy_options::overwrite_existing);
    }

    DWORD pid = 0;
    HANDLE hProcessSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32 pe32;
    pe32.dwSize = sizeof(PROCESSENTRY32);

    if (!Process32First(hProcessSnapshot, &pe32)) {
        std::cout << "Failed to retrieve first process info. Error: " << GetLastError() << std::endl;
        CloseHandle(hProcessSnapshot);
        return;
    }

    do {
        if (_stricmp(pe32.szExeFile, "steam.exe") == 0) {
            pid = pe32.th32ProcessID;
            break;
        }
    } while (Process32Next(hProcessSnapshot, &pe32));
    
    if (pid != 0) {
        HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
        if (hProcess != NULL) {
            std::cout << "Closing steam " << pid << std::endl;
            std::string command = STEAM_PATH + "\\steam.exe -shutdown";
            system(command.c_str());
            CloseHandle(hProcess);
        } else {
            std::cout << "Failed to open process" << std::endl;
        }
    }

    lua.script_file(manifest.path + "\\" + manifest.appid + ".lua");
    std::string command = "start steam://install/" + manifest.appid;
    system(command.c_str());
    std::cout << "Finished installing " << manifest.appid << std::endl;
}

int main() {
    std::vector<Manifest> manifests = get_manifests();
    if (!glfwInit())
        return 1;

    const char* glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    
    float main_scale = ImGui_ImplGlfw_GetContentScaleForMonitor(glfwGetPrimaryMonitor()); // Valid on GLFW 3.3+ only
    GLFWwindow* window = glfwCreateWindow((int)(500 * main_scale), (int)(350 * main_scale), "Steamless", nullptr, nullptr);
    if (window == nullptr)
        return 1;
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); 

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad; 

    ImGui::StyleColorsDark();

    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(main_scale);
    style.FontScaleDpi = main_scale; 

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        if (glfwGetWindowAttrib(window, GLFW_ICONIFIED) != 0)
        {
            ImGui_ImplGlfw_Sleep(10);
            continue;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        int w, h;
        glfwGetWindowSize(window, &w, &h);
        ImGui::SetNextWindowSize(ImVec2(w, h));
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::Begin("Steamless", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
        ImGui::Text("Steamless");
        for (const auto & manifest : manifests) {
            if (ImGui::Button(manifest.appid.c_str())) {
                install(manifest);
            }
        }
        ImGui::End();
        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
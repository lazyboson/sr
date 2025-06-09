// obs_capture_cross_platform.cpp - Cross-platform OBS screen capture for Windows and macOS
#include <obs.h>
#include <obs-module.h>
#include <iostream>
#include <thread>
#include <chrono>
#include <string>
#include <vector>
#include <cstring>
#include <fstream>
#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#include <shlwapi.h>
#pragma comment(lib, "shlwapi.lib")
#define PATH_SEPARATOR "\\"
#elif __APPLE__
#include <CoreGraphics/CoreGraphics.h>
#define PATH_SEPARATOR "/"
#endif

namespace fs = std::filesystem;

class OBSScreenCapture {
private:
    obs_source_t* screen_capture = nullptr;
    obs_source_t* mic_capture = nullptr;
    obs_source_t* desktop_audio = nullptr;
    obs_scene_t* scene = nullptr;
    obs_sceneitem_t* scene_item = nullptr;
    obs_output_t* output = nullptr;
    obs_encoder_t* video_encoder = nullptr;
    obs_encoder_t* audio_encoder = nullptr;

    std::string output_path;
    int capture_duration;
    std::string exe_dir;

    // Get the directory where the executable is located
    std::string get_exe_directory() {
        char buffer[MAX_PATH];
        GetModuleFileNameA(NULL, buffer, MAX_PATH);
        std::string exe_path = buffer;
        size_t pos = exe_path.find_last_of("\\/");
        return exe_path.substr(0, pos);
    }

    bool load_plugins() {
        std::vector<std::string> plugins = {
            "win-capture", "win-wasapi", "obs-ffmpeg",
            "obs-outputs", "obs-x264", "rtmp-services"
        };

        for (const auto& plugin : plugins) {
            std::string plugin_path = exe_dir + "\\" + plugin + ".dll";

            if (!fs::exists(plugin_path)) {
                std::cerr << "Warning: Plugin not found: " << plugin_path << std::endl;
                continue;
            }

            obs_module_t* module = nullptr;
            if (obs_open_module(&module, plugin_path.c_str(), nullptr) == MODULE_SUCCESS && module) {
                obs_init_module(module);
                std::cout << "Successfully loaded plugin: " << plugin << std::endl;
            }
            else {
                std::cerr << "Failed to load plugin: " << plugin << " from " << plugin_path << std::endl;
            }
        }
        return true;
    }

    void get_screen_resolution(int& width, int& height) {
        HDC hdc = GetDC(NULL);
        width = GetDeviceCaps(hdc, HORZRES);
        height = GetDeviceCaps(hdc, VERTRES);
        ReleaseDC(NULL, hdc);

        HMONITOR hMonitor = MonitorFromWindow(NULL, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO mi = { sizeof(mi) };
        if (GetMonitorInfo(hMonitor, &mi)) {
            width = mi.rcMonitor.right - mi.rcMonitor.left;
            height = mi.rcMonitor.bottom - mi.rcMonitor.top;
        }

        std::cout << "Screen resolution: " << width << "x" << height << std::endl;
    }

public:
    OBSScreenCapture(const std::string& file, int seconds)
        : output_path(file), capture_duration(seconds) {
        exe_dir = get_exe_directory();
        std::cout << "Working directory: " << exe_dir << std::endl;
    }

    bool initialize() {
        // Set up data paths - OBS needs to find its effect files
        std::string data_path = exe_dir + "\\data";
        std::string libobs_data = data_path + "\\libobs";

        // Check if data directory exists
        if (!fs::exists(libobs_data)) {
            std::cerr << "ERROR: OBS data directory not found: " << libobs_data << std::endl;
            std::cerr << "Please ensure the 'data\\libobs' folder with effect files is in:" << std::endl;
            std::cerr << "  " << exe_dir << std::endl;
            return false;
        }

        // Verify at least one effect file exists
        std::string test_effect = libobs_data + "\\default.effect";
        if (!fs::exists(test_effect)) {
            std::cerr << "ERROR: OBS effect files not found in: " << libobs_data << std::endl;
            std::cerr << "Please copy the libobs/data folder contents there." << std::endl;
            return false;
        }

        // Add data paths
        obs_add_data_path(data_path.c_str());
        std::cout << "Added OBS data path: " << data_path << std::endl;

        // Also add module data path for plugins
        obs_add_module_path(exe_dir.c_str(), (data_path + "\\obs-plugins\\%module%").c_str());

        // Initialize OBS
        if (!obs_startup("en-US", nullptr, nullptr)) {
            std::cerr << "Failed to initialize OBS core" << std::endl;
            return false;
        }

        std::cout << "OBS core initialized successfully" << std::endl;

        // Load plugins
        load_plugins();

        // Get screen resolution
        int screen_width, screen_height;
        get_screen_resolution(screen_width, screen_height);

        // Handle DPI scaling
        if (screen_width == 1707 && screen_height == 960) {
            std::cout << "Detected 125% DPI scaling on 1920x1080 display" << std::endl;
            screen_width = 1920;
            screen_height = 1080;
        }
        else if (screen_width < 1280 || screen_height < 720) {
            std::cout << "Warning: Low resolution detected, using 1920x1080" << std::endl;
            screen_width = 1920;
            screen_height = 1080;
        }

        // Setup video
        struct obs_video_info ovi = {};
        ovi.fps_num = 30;
        ovi.fps_den = 1;
        ovi.base_width = screen_width;
        ovi.base_height = screen_height;
        ovi.output_width = screen_width;
        ovi.output_height = screen_height;
        ovi.output_format = VIDEO_FORMAT_NV12;
        ovi.colorspace = VIDEO_CS_709;
        ovi.range = VIDEO_RANGE_PARTIAL;
        ovi.adapter = 0;
        ovi.gpu_conversion = true;
        ovi.scale_type = OBS_SCALE_BICUBIC;

        // Let OBS auto-detect the graphics module
        ovi.graphics_module = nullptr;

        int result = obs_reset_video(&ovi);
        if (result != OBS_VIDEO_SUCCESS) {
            std::cerr << "Failed to initialize video. Error code: " << result << std::endl;
            return false;
        }

        std::cout << "Video initialized successfully" << std::endl;

        // Setup audio
        struct obs_audio_info oai = {};
        oai.samples_per_sec = 48000;
        oai.speakers = SPEAKERS_STEREO;

        if (!obs_reset_audio(&oai)) {
            std::cerr << "Failed to initialize audio" << std::endl;
            return false;
        }

        std::cout << "Audio initialized successfully" << std::endl;
        return true;
    }

    bool setup_sources() {
        // Create scene
        scene = obs_scene_create("Main Scene");
        if (!scene) {
            std::cerr << "Failed to create scene" << std::endl;
            return false;
        }

        // Create screen capture
        obs_data_t* screen_settings = obs_data_create();
        obs_data_set_bool(screen_settings, "show_cursor", true);
        obs_data_set_int(screen_settings, "monitor", 0);  // Primary monitor

        screen_capture = obs_source_create("monitor_capture", "Screen", screen_settings, nullptr);
        obs_data_release(screen_settings);

        if (!screen_capture) {
            std::cerr << "Failed to create screen capture source" << std::endl;
            return false;
        }

        // Add to scene
        scene_item = obs_scene_add(scene, screen_capture);
        std::cout << "Screen capture source created" << std::endl;

        // Create audio sources
        obs_data_t* desktop_settings = obs_data_create();
        obs_data_t* mic_settings = obs_data_create();

        // Windows WASAPI for desktop audio
        desktop_audio = obs_source_create("wasapi_output_capture",
            "Desktop Audio", desktop_settings, nullptr);

        // Microphone capture
        obs_data_set_string(mic_settings, "device_id", "default");
        mic_capture = obs_source_create("wasapi_input_capture",
            "Microphone", mic_settings, nullptr);

        obs_data_release(desktop_settings);
        obs_data_release(mic_settings);

        // Set output sources
        obs_source_t* scene_source = obs_scene_get_source(scene);
        obs_set_output_source(0, scene_source);

        if (mic_capture) {
            obs_set_output_source(1, mic_capture);
            std::cout << "Microphone capture enabled" << std::endl;
        }

        if (desktop_audio) {
            obs_set_output_source(2, desktop_audio);
            std::cout << "Desktop audio capture enabled" << std::endl;
        }

        return true;
    }

    bool setup_encoding() {
        // Video encoder
        obs_data_t* video_settings = obs_data_create();
        obs_data_set_int(video_settings, "bitrate", 5000);
        obs_data_set_string(video_settings, "preset", "veryfast");

        video_encoder = obs_video_encoder_create("obs_x264", "Video Encoder",
            video_settings, nullptr);
        obs_data_release(video_settings);

        if (!video_encoder) {
            std::cerr << "Failed to create video encoder" << std::endl;
            return false;
        }

        // Audio encoder
        obs_data_t* audio_settings = obs_data_create();
        obs_data_set_int(audio_settings, "bitrate", 128);

        // Try Windows AAC first, then FFmpeg AAC
        audio_encoder = obs_audio_encoder_create("mf_aac", "Audio Encoder",
            audio_settings, 0, nullptr);
        if (!audio_encoder) {
            audio_encoder = obs_audio_encoder_create("ffmpeg_aac", "Audio Encoder",
                audio_settings, 0, nullptr);
        }

        obs_data_release(audio_settings);

        if (!audio_encoder) {
            std::cerr << "Failed to create audio encoder" << std::endl;
            return false;
        }

        obs_encoder_set_video(video_encoder, obs_get_video());
        obs_encoder_set_audio(audio_encoder, obs_get_audio());

        std::cout << "Encoders configured successfully" << std::endl;
        return true;
    }

    bool start_recording() {
        obs_data_t* output_settings = obs_data_create();
        obs_data_set_string(output_settings, "path", output_path.c_str());

        output = obs_output_create("mp4_output", "Recording", output_settings, nullptr);
        obs_data_release(output_settings);

        if (!output) {
            std::cerr << "Failed to create MP4 output" << std::endl;
            return false;
        }

        obs_output_set_video_encoder(output, video_encoder);
        obs_output_set_audio_encoder(output, audio_encoder, 0);

        if (!obs_output_start(output)) {
            const char* error = obs_output_get_last_error(output);
            std::cerr << "Failed to start output: " << (error ? error : "unknown") << std::endl;
            return false;
        }

        std::cout << "Recording started successfully" << std::endl;
        return true;
    }

    void record() {
        std::cout << "Initializing OBS..." << std::endl;

        if (!initialize()) {
            std::cerr << "Initialization failed" << std::endl;
            return;
        }

        std::cout << "Setting up sources..." << std::endl;
        if (!setup_sources()) {
            std::cerr << "Failed to setup sources" << std::endl;
            cleanup();
            return;
        }

        std::cout << "Setting up encoders..." << std::endl;
        if (!setup_encoding()) {
            std::cerr << "Failed to setup encoding" << std::endl;
            cleanup();
            return;
        }

        std::cout << "Starting recording to: " << output_path << std::endl;
        if (!start_recording()) {
            std::cerr << "Failed to start recording" << std::endl;
            cleanup();
            return;
        }

        std::cout << "Recording for " << capture_duration << " seconds..." << std::endl;
        std::cout << "Press Ctrl+C to stop early" << std::endl;

        std::this_thread::sleep_for(std::chrono::seconds(capture_duration));

        std::cout << "Stopping recording..." << std::endl;
        obs_output_stop(output);

        // Wait for output to finish
        while (obs_output_active(output)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        std::cout << "Recording complete!" << std::endl;
        std::cout << "File saved to: " << output_path << std::endl;

        cleanup();
    }

private:
    void cleanup() {
        // Stop output if still active
        if (output && obs_output_active(output)) {
            obs_output_stop(output);
            while (obs_output_active(output)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }

        // Clear all output sources
        for (int i = 0; i < 6; i++) {
            obs_set_output_source(i, nullptr);
        }

        // Release in reverse order
        if (output) {
            obs_output_release(output);
            output = nullptr;
        }

        if (audio_encoder) {
            obs_encoder_release(audio_encoder);
            audio_encoder = nullptr;
        }

        if (video_encoder) {
            obs_encoder_release(video_encoder);
            video_encoder = nullptr;
        }

        if (mic_capture) {
            obs_source_release(mic_capture);
            mic_capture = nullptr;
        }

        if (desktop_audio) {
            obs_source_release(desktop_audio);
            desktop_audio = nullptr;
        }

        if (scene && scene_item) {
            obs_sceneitem_remove(scene_item);
            scene_item = nullptr;
        }

        if (screen_capture) {
            obs_source_release(screen_capture);
            screen_capture = nullptr;
        }

        if (scene) {
            obs_scene_release(scene);
            scene = nullptr;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        obs_shutdown();
    }
};


int main(int argc, char* argv[]) {
    std::string output_file = "recording.mp4";
    int duration = 10;

    if (argc > 1) {
        duration = std::atoi(argv[1]);
    }
    if (argc > 2) {
        output_file = argv[2];
    }

    std::cout << "OBS Screen and Audio Capture" << std::endl;
    std::cout << "=============================" << std::endl;
    std::cout << "Output: " << output_file << std::endl;
    std::cout << "Duration: " << duration << " seconds" << std::endl;
    std::cout << "\nIMPORTANT: Grant necessary permissions if prompted!" << std::endl;
    std::cout << "Press Enter to start..." << std::endl;
    std::cin.get();

    OBSScreenCapture capture(output_file, duration);
    capture.record();

    return 0;
}


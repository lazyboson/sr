// obs_capture_multi_monitor.cpp - Multi-monitor OBS screen capture for Windows
/*
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
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#include <shlwapi.h>
#pragma comment(lib, "shlwapi.lib")
#endif

namespace fs = std::filesystem;

struct MonitorInfo {
    int index;
    std::string name;
    int x, y;
    int width, height;
    bool isPrimary;
    HMONITOR hMonitor;
};

class OBSScreenCapture {
private:
    std::vector<obs_source_t*> screen_captures;
    std::vector<obs_sceneitem_t*> scene_items;
    std::vector<MonitorInfo> monitors;

    obs_source_t* mic_capture = nullptr;
    obs_source_t* desktop_audio = nullptr;
    obs_scene_t* scene = nullptr;
    obs_output_t* output = nullptr;
    obs_encoder_t* video_encoder = nullptr;
    obs_encoder_t* audio_encoder = nullptr;

    std::string output_path;
    int capture_duration;
    std::string obs_path;
    int total_width = 0;
    int total_height = 0;

    static BOOL CALLBACK MonitorEnumProc(HMONITOR hMonitor, HDC hdcMonitor,
        LPRECT lprcMonitor, LPARAM dwData) {
        std::vector<MonitorInfo>* monitors = (std::vector<MonitorInfo>*)dwData;

        MONITORINFOEX mi;
        mi.cbSize = sizeof(mi);
        if (GetMonitorInfo(hMonitor, &mi)) {
            MonitorInfo info;
            info.hMonitor = hMonitor;
            info.index = static_cast<int>(monitors->size());
            // Convert WCHAR to std::string
            char deviceName[32];
            size_t convertedChars = 0;
            wcstombs_s(&convertedChars, deviceName, sizeof(deviceName), mi.szDevice, _TRUNCATE);
            info.name = deviceName;
            info.x = mi.rcMonitor.left;
            info.y = mi.rcMonitor.top;
            info.width = mi.rcMonitor.right - mi.rcMonitor.left;
            info.height = mi.rcMonitor.bottom - mi.rcMonitor.top;
            info.isPrimary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;

            monitors->push_back(info);

            std::cout << "Found Monitor " << info.index << ": " << info.name
                << " (" << info.width << "x" << info.height << ")"
                << " at position (" << info.x << ", " << info.y << ")"
                << (info.isPrimary ? " [PRIMARY]" : "") << std::endl;
        }

        return TRUE;
    }

    void detect_monitors() {
        monitors.clear();
        EnumDisplayMonitors(NULL, NULL, MonitorEnumProc, (LPARAM)&monitors);

        // Calculate total canvas size needed
        int min_x = INT_MAX, min_y = INT_MAX;
        int max_x = INT_MIN, max_y = INT_MIN;

        for (const auto& monitor : monitors) {
            min_x = (std::min)(min_x, monitor.x);
            min_y = (std::min)(min_y, monitor.y);
            max_x = (std::max)(max_x, monitor.x + monitor.width);
            max_y = (std::max)(max_y, monitor.y + monitor.height);
        }

        // Normalize positions so leftmost/topmost monitor is at (0,0)
        for (auto& monitor : monitors) {
            monitor.x -= min_x;
            monitor.y -= min_y;
            std::cout << "Adjusted Monitor " << monitor.index << " position to ("
                << monitor.x << ", " << monitor.y << ")" << std::endl;
        }

        total_width = max_x - min_x;
        total_height = max_y - min_y;

        std::cout << "\nTotal canvas size: " << total_width << "x" << total_height << std::endl;
        std::cout << "Number of monitors detected: " << monitors.size() << std::endl;
    }

    bool load_module(const std::string& bin_path, const std::string& data_path,
        const std::string& module_name) {
        obs_module_t* module = nullptr;

        std::string module_path = bin_path + "/" + module_name + ".dll";

        if (!fs::exists(module_path)) {
            std::cerr << "Module not found: " << module_path << std::endl;
            return false;
        }

        int code = obs_open_module(&module, module_path.c_str(), data_path.c_str());
        if (code != MODULE_SUCCESS) {
            std::cerr << "Failed to open module '" << module_name << "': ";
            switch (code) {
            case MODULE_FILE_NOT_FOUND:
                std::cerr << "File not found" << std::endl;
                break;
            case MODULE_MISSING_EXPORTS:
                std::cerr << "Missing exports" << std::endl;
                break;
            case MODULE_INCOMPATIBLE_VER:
                std::cerr << "Incompatible version" << std::endl;
                break;
            case MODULE_ERROR:
                std::cerr << "Generic error" << std::endl;
                break;
            default:
                std::cerr << "Unknown error " << code << std::endl;
            }
            return false;
        }

        if (!obs_init_module(module)) {
            std::cerr << "Failed to initialize module: " << module_name << std::endl;
            return false;
        }

        std::cout << "Successfully loaded module: " << module_name << std::endl;
        // Debug: Print scene bounds
        std::cout << "\nScene configuration:" << std::endl;
        std::cout << "Canvas size: " << total_width << "x" << total_height << std::endl;

        for (size_t i = 0; i < monitors.size(); i++) {
            const auto& mon = monitors[i];
            std::cout << "Monitor " << i << ": " << mon.width << "x" << mon.height
                << " at (" << mon.x << ", " << mon.y << ")" << std::endl;
        }

        return true;
    }

    bool load_required_modules() {
        std::string bin_path = obs_path + "/obs-plugins/64bit";
        std::string data_path = obs_path + "/data/obs-plugins";

        std::vector<std::string> modules = {
            "win-capture",
            "win-wasapi",
            "obs-outputs",
            "obs-ffmpeg",
            "obs-x264"
        };

        for (const auto& module : modules) {
            std::string module_data = data_path + "/" + module;
            if (!load_module(bin_path, module_data, module)) {
                std::cerr << "Failed to load required module: " << module << std::endl;
            }
        }

        return true;
    }

public:
    OBSScreenCapture(const std::string& file, int seconds)
        : output_path(file), capture_duration(seconds) {
        obs_path = "C:/Program Files/obs-studio";

        if (!fs::exists(obs_path)) {
            std::cerr << "Error: OBS Studio not found at: " << obs_path << std::endl;
        }
        std::cout << "OBS Path: " << obs_path << std::endl;
    }

    bool initialize() {
        // Detect all monitors first
        detect_monitors();

        if (monitors.empty()) {
            std::cerr << "No monitors detected!" << std::endl;
            return false;
        }

        // Set up module search paths BEFORE startup
        std::string bin_path = obs_path + "/bin/64bit";
        std::string plugin_bin_path = obs_path + "/obs-plugins/64bit";
        std::string data_path = obs_path + "/data/obs-plugins/%module%";

        obs_add_module_path(bin_path.c_str(), data_path.c_str());
        obs_add_module_path(plugin_bin_path.c_str(), data_path.c_str());

        // Initialize OBS core
        if (!obs_startup("en-US", nullptr, nullptr)) {
            std::cerr << "Failed to initialize OBS core" << std::endl;
            return false;
        }

        std::cout << "OBS core initialized successfully" << std::endl;

        // Load modules manually
        load_required_modules();

        // Critical: Call post load modules
        obs_post_load_modules();

        // Setup video with combined resolution of all monitors
        struct obs_video_info ovi = {};
        ovi.fps_num = 10;  // Reduced to 10 FPS as requested
        ovi.fps_den = 1;
        ovi.base_width = total_width;
        ovi.base_height = total_height;
        ovi.output_width = total_width;
        ovi.output_height = total_height;
        ovi.output_format = VIDEO_FORMAT_NV12;
        ovi.colorspace = VIDEO_CS_709;
        ovi.range = VIDEO_RANGE_PARTIAL;
        ovi.adapter = 0;
        ovi.gpu_conversion = true;
        ovi.scale_type = OBS_SCALE_BICUBIC;
        ovi.graphics_module = "libobs-d3d11";

        int result = obs_reset_video(&ovi);
        if (result != OBS_VIDEO_SUCCESS) {
            std::cerr << "Failed to initialize video. Error code: " << result << std::endl;
            return false;
        }

        std::cout << "Video initialized successfully with " << total_width << "x"
            << total_height << " @ 10 FPS" << std::endl;

        // Setup audio
        struct obs_audio_info oai = {};
        oai.samples_per_sec = 48000;
        oai.speakers = SPEAKERS_STEREO;

        if (!obs_reset_audio(&oai)) {
            std::cerr << "Failed to initialize audio" << std::endl;
            return false;
        }

        std::cout << "Audio initialized successfully" << std::endl;

        // Debug: List available source types
        std::cout << "\nAvailable source types:" << std::endl;
        size_t idx = 0;
        const char* id;
        while (obs_enum_source_types(idx++, &id)) {
            std::cout << "  - " << id << std::endl;
        }

        return true;
    }

    bool setup_sources() {
        // Create scene
        scene = obs_scene_create("Multi-Monitor Scene");
        if (!scene) {
            std::cerr << "Failed to create scene" << std::endl;
            return false;
        }

        // Create a screen capture source for each monitor
        for (const auto& monitor : monitors) {
            std::cout << "\nSetting up capture for Monitor " << monitor.index
                << " (" << monitor.name << ")"
                << "\n  Native resolution: " << monitor.width << "x" << monitor.height
                << "\n  Canvas position: (" << monitor.x << ", " << monitor.y << ")"
                << std::endl;

            obs_data_t* screen_settings = obs_data_create();
            obs_data_set_bool(screen_settings, "capture_cursor", true);
            obs_data_set_int(screen_settings, "monitor", monitor.index);
            // Force compatibility mode to ensure proper capture
            obs_data_set_bool(screen_settings, "compatibility", false);
            // Ensure we capture at native resolution
            obs_data_set_bool(screen_settings, "force_scaling", false);

            std::string source_name = "Monitor " + std::to_string(monitor.index) + " - " + monitor.name;
            obs_source_t* screen_capture = obs_source_create("monitor_capture",
                source_name.c_str(),
                screen_settings,
                nullptr);
            obs_data_release(screen_settings);

            if (!screen_capture) {
                std::cerr << "Failed to create screen capture for monitor " << monitor.index << std::endl;
                continue;
            }

            // Get the actual source dimensions to verify
            uint32_t source_width = obs_source_get_width(screen_capture);
            uint32_t source_height = obs_source_get_height(screen_capture);

            if (source_width > 0 && source_height > 0) {
                std::cout << "  Source dimensions: " << source_width << "x" << source_height << std::endl;
            }

            // Add to scene and position correctly
            obs_sceneitem_t* scene_item = obs_scene_add(scene, screen_capture);
            if (scene_item) {
                // Position the monitor capture at its correct location
                vec2 pos;
                pos.x = (float)monitor.x;
                pos.y = (float)monitor.y;
                obs_sceneitem_set_pos(scene_item, &pos);

                // Ensure no scaling is applied - capture at native resolution
                vec2 scale;
                scale.x = 1.0f;
                scale.y = 1.0f;
                obs_sceneitem_set_scale(scene_item, &scale);

                // Set bounds to ensure no cropping
                obs_bounds_type bounds_type = OBS_BOUNDS_NONE;
                obs_sceneitem_set_bounds_type(scene_item, bounds_type);

                // Set the crop to 0 to ensure full capture
                obs_sceneitem_crop crop = { 0, 0, 0, 0 };
                obs_sceneitem_set_crop(scene_item, &crop);

                // Ensure it's visible
                obs_sceneitem_set_visible(scene_item, true);

                std::cout << "  Successfully added to scene" << std::endl;

                screen_captures.push_back(screen_capture);
                scene_items.push_back(scene_item);
            }
        }

        std::cout << "\nSuccessfully set up " << screen_captures.size()
            << " monitor captures" << std::endl;

        // Verify scene setup
        std::cout << "\nVerifying scene setup:" << std::endl;
        for (size_t i = 0; i < scene_items.size(); i++) {
            if (scene_items[i]) {
                vec2 pos;
                obs_sceneitem_get_pos(scene_items[i], &pos);
                vec2 scale;
                obs_sceneitem_get_scale(scene_items[i], &scale);

                std::cout << "  Monitor " << i << ": Position=(" << pos.x << ", " << pos.y
                    << "), Scale=(" << scale.x << ", " << scale.y << ")" << std::endl;
            }
        }

        // Create audio sources
        obs_data_t* desktop_settings = obs_data_create();
        obs_data_t* mic_settings = obs_data_create();

        // Desktop audio
        desktop_audio = obs_source_create("wasapi_output_capture",
            "Desktop Audio", desktop_settings, nullptr);

        // Microphone
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
        // Video encoder settings - adjust bitrate for larger resolution
        obs_data_t* video_settings = obs_data_create();
        // Calculate bitrate based on total resolution
        // Base calculation: pixels * fps * bits_per_pixel_per_second
        // Using lower bits per pixel since it's 10 FPS
        double pixels_per_second = (double)total_width * total_height * 10;
        double base_pixels_per_second = 1920.0 * 1080.0 * 30.0;
        int bitrate = (int)((pixels_per_second / base_pixels_per_second) * 5000);
        bitrate = (std::max)(5000, (std::min)(bitrate, 50000)); // Clamp between 5-50 Mbps

        obs_data_set_int(video_settings, "bitrate", bitrate);
        obs_data_set_string(video_settings, "preset", "veryfast");
        obs_data_set_string(video_settings, "profile", "high");
        obs_data_set_string(video_settings, "level", "5.1");

        // For multi-monitor, ensure we have enough buffer
        obs_data_set_int(video_settings, "buffer_size", bitrate);

        const char* video_encoders[] = {
            "obs_x264",
            "ffmpeg_nvenc",
            "amd_amf_h264",
            "h264_texture_amf"
        };

        for (const auto& encoder_id : video_encoders) {
            video_encoder = obs_video_encoder_create(encoder_id, "Video Encoder",
                video_settings, nullptr);
            if (video_encoder) {
                std::cout << "Created video encoder using: " << encoder_id
                    << " with bitrate: " << bitrate << " kbps" << std::endl;
                break;
            }
        }

        obs_data_release(video_settings);

        if (!video_encoder) {
            std::cerr << "Failed to create any video encoder" << std::endl;
            return false;
        }

        // Audio encoder settings
        obs_data_t* audio_settings = obs_data_create();
        obs_data_set_int(audio_settings, "bitrate", 128);

        const char* audio_encoders[] = {
            "ffmpeg_aac",
            "mf_aac",
            "CoreAudio_AAC"
        };

        for (const auto& encoder_id : audio_encoders) {
            audio_encoder = obs_audio_encoder_create(encoder_id, "Audio Encoder",
                audio_settings, 0, nullptr);
            if (audio_encoder) {
                std::cout << "Created audio encoder using: " << encoder_id << std::endl;
                break;
            }
        }

        obs_data_release(audio_settings);

        if (!audio_encoder) {
            std::cerr << "Failed to create any audio encoder" << std::endl;
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

        const char* output_types[] = {
            "ffmpeg_muxer",
            "ffmpeg_output",
            "mp4_output"
        };

        for (const auto& output_id : output_types) {
            output = obs_output_create(output_id, "Recording", output_settings, nullptr);
            if (output) {
                std::cout << "Created output using: " << output_id << std::endl;
                break;
            }
        }

        obs_data_release(output_settings);

        if (!output) {
            std::cerr << "Failed to create any output" << std::endl;
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
        std::cout << "Initializing OBS for multi-monitor capture..." << std::endl;

        if (!initialize()) {
            std::cerr << "Initialization failed" << std::endl;
            return;
        }

        std::cout << "\nSetting up sources..." << std::endl;
        if (!setup_sources()) {
            std::cerr << "Failed to setup sources" << std::endl;
            cleanup();
            return;
        }

        std::cout << "\nSetting up encoders..." << std::endl;
        if (!setup_encoding()) {
            std::cerr << "Failed to setup encoding" << std::endl;
            cleanup();
            return;
        }

        std::cout << "\nStarting recording to: " << output_path << std::endl;
        if (!start_recording()) {
            std::cerr << "Failed to start recording" << std::endl;
            cleanup();
            return;
        }

        std::cout << "\nRecording " << monitors.size() << " monitors for "
            << capture_duration << " seconds at 10 FPS..." << std::endl;
        std::cout << "Total resolution: " << total_width << "x" << total_height << std::endl;
        std::cout << "Press Ctrl+C to stop early" << std::endl;

        std::this_thread::sleep_for(std::chrono::seconds(capture_duration));

        std::cout << "\nStopping recording..." << std::endl;
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

        // Clean up all monitor captures
        for (auto& capture : screen_captures) {
            if (capture) {
                obs_source_release(capture);
            }
        }
        screen_captures.clear();
        scene_items.clear();

        if (scene) {
            obs_scene_release(scene);
            scene = nullptr;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        obs_shutdown();
    }
};

int main(int argc, char* argv[]) {
    std::string output_file = "multi_monitor_recording.mp4";
    int duration = 10;

    if (argc > 1) {
        duration = std::atoi(argv[1]);
    }
    if (argc > 2) {
        output_file = argv[2];
    }

    std::cout << "OBS Multi-Monitor Screen Capture (Console Mode)" << std::endl;
    std::cout << "==============================================" << std::endl;
    std::cout << "Output: " << output_file << std::endl;
    std::cout << "Duration: " << duration << " seconds" << std::endl;
    std::cout << "FPS: 10" << std::endl;
    std::cout << "\nIMPORTANT: Make sure OBS Studio is installed in the default location" << std::endl;
    std::cout << "Press Enter to start..." << std::endl;
    std::cin.get();

    OBSScreenCapture capture(output_file, duration);
    capture.record();

    return 0;
}
*/
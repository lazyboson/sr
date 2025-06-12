// obs_rtmp_streaming.cpp - Multi-monitor OBS RTMP streaming for Windows
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
#include <atomic>
#include <mutex>
#include <condition_variable>

#ifdef _WIN32
#include <windows.h>
#include <shlwapi.h>
#include <conio.h>
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

class OBSRTMPStreamer {
private:
    std::vector<obs_source_t*> screen_captures;
    std::vector<obs_sceneitem_t*> scene_items;
    std::vector<MonitorInfo> monitors;

    obs_source_t* mic_capture = nullptr;
    obs_source_t* desktop_audio = nullptr;
    obs_scene_t* scene = nullptr;
    obs_output_t* rtmp_output = nullptr;
    obs_service_t* rtmp_service = nullptr;
    obs_encoder_t* video_encoder = nullptr;
    obs_encoder_t* audio_encoder = nullptr;

    std::string rtmp_server;
    std::string stream_key;
    std::string obs_path;
    int total_width = 0;
    int total_height = 0;
    int fps = 30;
    int video_bitrate = 5000;
    int audio_bitrate = 128;

    // Thread control
    std::atomic<bool> is_streaming{ false };
    std::atomic<bool> should_stop{ false };
    std::thread control_thread;
    std::mutex stream_mutex;
    std::condition_variable cv;

    static BOOL CALLBACK MonitorEnumProc(HMONITOR hMonitor, HDC hdcMonitor,
        LPRECT lprcMonitor, LPARAM dwData) {
        std::vector<MonitorInfo>* monitors = (std::vector<MonitorInfo>*)dwData;

        MONITORINFOEX mi;
        mi.cbSize = sizeof(mi);
        if (GetMonitorInfo(hMonitor, &mi)) {
            MonitorInfo info;
            info.hMonitor = hMonitor;
            info.index = static_cast<int>(monitors->size());
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

        int min_x = INT_MAX, min_y = INT_MAX;
        int max_x = INT_MIN, max_y = INT_MIN;

        for (const auto& monitor : monitors) {
            min_x = (std::min)(min_x, monitor.x);
            min_y = (std::min)(min_y, monitor.y);
            max_x = (std::max)(max_x, monitor.x + monitor.width);
            max_y = (std::max)(max_y, monitor.y + monitor.height);
        }

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
            "obs-x264",
            "rtmp-services"
        };

        for (const auto& module : modules) {
            std::string module_data = data_path + "/" + module;
            if (!load_module(bin_path, module_data, module)) {
                std::cerr << "Warning: Failed to load module: " << module << std::endl;
            }
        }

        return true;
    }

    void control_loop() {
        std::cout << "\n=== STREAMING CONTROLS ===" << std::endl;
        std::cout << "Press 'S' to START streaming" << std::endl;
        std::cout << "Press 'T' to STOP streaming" << std::endl;
        std::cout << "Press 'Q' to QUIT application" << std::endl;
        std::cout << "=========================" << std::endl;

        while (!should_stop) {
            if (_kbhit()) {
                char key = _getch();
                key = toupper(key);

                switch (key) {
                case 'S':
                    if (!is_streaming) {
                        std::cout << "\nStarting stream..." << std::endl;
                        start_streaming();
                    }
                    else {
                        std::cout << "\nStream is already running!" << std::endl;
                    }
                    break;

                case 'T':
                    if (is_streaming) {
                        std::cout << "\nStopping stream..." << std::endl;
                        stop_streaming();
                    }
                    else {
                        std::cout << "\nNo stream is running!" << std::endl;
                    }
                    break;

                case 'Q':
                    std::cout << "\nQuitting application..." << std::endl;
                    if (is_streaming) {
                        stop_streaming();
                    }
                    should_stop = true;
                    cv.notify_all();
                    break;

                default:
                    std::cout << "\nUnknown command. Use S/T/Q" << std::endl;
                    break;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

public:
    OBSRTMPStreamer(const std::string& server, const std::string& key, int fps_rate = 30, int vbitrate = 5000)
        : rtmp_server(server), stream_key(key), fps(fps_rate), video_bitrate(vbitrate) {
        obs_path = "C:/Program Files/obs-studio";

        if (!fs::exists(obs_path)) {
            std::cerr << "Error: OBS Studio not found at: " << obs_path << std::endl;
        }
        std::cout << "OBS Path: " << obs_path << std::endl;
    }

    ~OBSRTMPStreamer() {
        if (control_thread.joinable()) {
            should_stop = true;
            cv.notify_all();
            control_thread.join();
        }
        cleanup();
    }

    bool initialize() {
        detect_monitors();

        if (monitors.empty()) {
            std::cerr << "No monitors detected!" << std::endl;
            return false;
        }

        std::string bin_path = obs_path + "/bin/64bit";
        std::string plugin_bin_path = obs_path + "/obs-plugins/64bit";
        std::string data_path = obs_path + "/data/obs-plugins/%module%";

        obs_add_module_path(bin_path.c_str(), data_path.c_str());
        obs_add_module_path(plugin_bin_path.c_str(), data_path.c_str());

        if (!obs_startup("en-US", nullptr, nullptr)) {
            std::cerr << "Failed to initialize OBS core" << std::endl;
            return false;
        }

        std::cout << "OBS core initialized successfully" << std::endl;

        load_required_modules();
        obs_post_load_modules();

        // Setup video
        struct obs_video_info ovi = {};
        ovi.fps_num = fps;
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

        std::cout << "Video initialized: " << total_width << "x"
            << total_height << " @ " << fps << " FPS" << std::endl;

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
        scene = obs_scene_create("Multi-Monitor Scene");
        if (!scene) {
            std::cerr << "Failed to create scene" << std::endl;
            return false;
        }

        // Create screen captures for each monitor
        for (const auto& monitor : monitors) {
            std::cout << "\nSetting up capture for Monitor " << monitor.index
                << " (" << monitor.name << ")" << std::endl;

            obs_data_t* screen_settings = obs_data_create();
            obs_data_set_bool(screen_settings, "capture_cursor", true);
            obs_data_set_int(screen_settings, "monitor", monitor.index);
            obs_data_set_bool(screen_settings, "compatibility", false);
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

            obs_sceneitem_t* scene_item = obs_scene_add(scene, screen_capture);
            if (scene_item) {
                vec2 pos;
                pos.x = (float)monitor.x;
                pos.y = (float)monitor.y;
                obs_sceneitem_set_pos(scene_item, &pos);

                vec2 scale;
                scale.x = 1.0f;
                scale.y = 1.0f;
                obs_sceneitem_set_scale(scene_item, &scale);

                obs_bounds_type bounds_type = OBS_BOUNDS_NONE;
                obs_sceneitem_set_bounds_type(scene_item, bounds_type);

                obs_sceneitem_crop crop = { 0, 0, 0, 0 };
                obs_sceneitem_set_crop(scene_item, &crop);

                obs_sceneitem_set_visible(scene_item, true);

                screen_captures.push_back(screen_capture);
                scene_items.push_back(scene_item);
            }
        }

        // Create audio sources
        obs_data_t* desktop_settings = obs_data_create();
        obs_data_t* mic_settings = obs_data_create();

        desktop_audio = obs_source_create("wasapi_output_capture",
            "Desktop Audio", desktop_settings, nullptr);

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

    bool setup_rtmp_service() {
        // Create RTMP service
        obs_data_t* service_settings = obs_data_create();
        obs_data_set_string(service_settings, "service", "Custom");
        obs_data_set_string(service_settings, "server", rtmp_server.c_str());
        obs_data_set_string(service_settings, "key", stream_key.c_str());

        rtmp_service = obs_service_create("rtmp_custom", "RTMP Service", service_settings, nullptr);
        obs_data_release(service_settings);

        if (!rtmp_service) {
            std::cerr << "Failed to create RTMP service" << std::endl;
            return false;
        }

        std::cout << "RTMP service configured for: " << rtmp_server << std::endl;
        return true;
    }

    bool setup_encoding() {
        // Video encoder settings
        obs_data_t* video_settings = obs_data_create();

        // Calculate adaptive bitrate based on resolution
        double pixels_per_second = (double)total_width * total_height * fps;
        double base_pixels_per_second = 1920.0 * 1080.0 * 30.0;
        int calculated_bitrate = (int)((pixels_per_second / base_pixels_per_second) * video_bitrate);
        calculated_bitrate = (std::max)(1000, (std::min)(calculated_bitrate, 50000));

        obs_data_set_int(video_settings, "bitrate", calculated_bitrate);
        obs_data_set_int(video_settings, "keyint_sec", 2);
        obs_data_set_string(video_settings, "preset", "veryfast");
        obs_data_set_string(video_settings, "profile", "main");
        obs_data_set_string(video_settings, "tune", "zerolatency");
        obs_data_set_int(video_settings, "buffer_size", calculated_bitrate);

        const char* video_encoders[] = {
            "obs_x264",
            "ffmpeg_nvenc",
            "jim_nvenc",
            "amd_amf_h264"
        };

        for (const auto& encoder_id : video_encoders) {
            video_encoder = obs_video_encoder_create(encoder_id, "Video Encoder",
                video_settings, nullptr);
            if (video_encoder) {
                std::cout << "Video encoder: " << encoder_id
                    << " (bitrate: " << calculated_bitrate << " kbps)" << std::endl;
                break;
            }
        }

        obs_data_release(video_settings);

        if (!video_encoder) {
            std::cerr << "Failed to create video encoder" << std::endl;
            return false;
        }

        // Audio encoder settings
        obs_data_t* audio_settings = obs_data_create();
        obs_data_set_int(audio_settings, "bitrate", audio_bitrate);

        const char* audio_encoders[] = {
            "ffmpeg_aac",
            "mf_aac",
            "CoreAudio_AAC"
        };

        for (const auto& encoder_id : audio_encoders) {
            audio_encoder = obs_audio_encoder_create(encoder_id, "Audio Encoder",
                audio_settings, 0, nullptr);
            if (audio_encoder) {
                std::cout << "Audio encoder: " << encoder_id << std::endl;
                break;
            }
        }

        obs_data_release(audio_settings);

        if (!audio_encoder) {
            std::cerr << "Failed to create audio encoder" << std::endl;
            return false;
        }

        obs_encoder_set_video(video_encoder, obs_get_video());
        obs_encoder_set_audio(audio_encoder, obs_get_audio());

        return true;
    }

    bool start_streaming() {
        std::lock_guard<std::mutex> lock(stream_mutex);

        if (is_streaming) {
            std::cout << "Already streaming!" << std::endl;
            return false;
        }

        // Create RTMP output
        obs_data_t* output_settings = obs_data_create();
        obs_data_set_string(output_settings, "bind_ip", "default");
        obs_data_set_bool(output_settings, "new_socket_loop_enabled", false);
        obs_data_set_bool(output_settings, "low_latency_mode_enabled", true);

        rtmp_output = obs_output_create("rtmp_output", "RTMP Output", output_settings, nullptr);
        obs_data_release(output_settings);

        if (!rtmp_output) {
            std::cerr << "Failed to create RTMP output" << std::endl;
            return false;
        }

        obs_output_set_service(rtmp_output, rtmp_service);
        obs_output_set_video_encoder(rtmp_output, video_encoder);
        obs_output_set_audio_encoder(rtmp_output, audio_encoder, 0);

        // Set up reconnect settings
        obs_data_t* output_data = obs_output_get_settings(rtmp_output);
        obs_data_set_int(output_data, "retry_delay", 2);
        obs_data_set_int(output_data, "max_retries", 5);
        obs_output_update(rtmp_output, output_data);
        obs_data_release(output_data);

        if (!obs_output_start(rtmp_output)) {
            const char* error = obs_output_get_last_error(rtmp_output);
            std::cerr << "Failed to start RTMP output: " << (error ? error : "unknown") << std::endl;
            obs_output_release(rtmp_output);
            rtmp_output = nullptr;
            return false;
        }

        is_streaming = true;
        std::cout << "Streaming started successfully!" << std::endl;
        std::cout << "Stream URL: " << rtmp_server << "/" << stream_key << std::endl;
        return true;
    }

    bool stop_streaming() {
        std::lock_guard<std::mutex> lock(stream_mutex);

        if (!is_streaming || !rtmp_output) {
            std::cout << "Not currently streaming!" << std::endl;
            return false;
        }

        obs_output_stop(rtmp_output);

        // Wait for output to finish
        int timeout = 50; // 5 seconds timeout
        while (obs_output_active(rtmp_output) && timeout > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            timeout--;
        }

        if (timeout == 0) {
            std::cerr << "Warning: Timeout while stopping stream" << std::endl;
            obs_output_force_stop(rtmp_output);
        }

        obs_output_release(rtmp_output);
        rtmp_output = nullptr;
        is_streaming = false;

        std::cout << "Streaming stopped successfully!" << std::endl;
        return true;
    }

    void run() {
        std::cout << "Initializing OBS for multi-monitor RTMP streaming..." << std::endl;

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

        std::cout << "\nSetting up RTMP service..." << std::endl;
        if (!setup_rtmp_service()) {
            std::cerr << "Failed to setup RTMP service" << std::endl;
            cleanup();
            return;
        }

        std::cout << "\nSetting up encoders..." << std::endl;
        if (!setup_encoding()) {
            std::cerr << "Failed to setup encoding" << std::endl;
            cleanup();
            return;
        }

        // Start control thread
        control_thread = std::thread(&OBSRTMPStreamer::control_loop, this);

        // Wait for quit signal
        std::unique_lock<std::mutex> lock(stream_mutex);
        cv.wait(lock, [this] { return should_stop.load(); });

        std::cout << "\nShutting down..." << std::endl;
    }

    // Status methods
    bool get_streaming_status() const {
        return is_streaming.load();
    }

    void get_stream_stats() {
        if (!is_streaming || !rtmp_output) {
            std::cout << "Not currently streaming" << std::endl;
            return;
        }

        uint64_t total_bytes = obs_output_get_total_bytes(rtmp_output);
        int total_frames = obs_output_get_total_frames(rtmp_output);
        int dropped_frames = obs_output_get_frames_dropped(rtmp_output);
        double congestion = obs_output_get_congestion(rtmp_output);

        std::cout << "\n=== STREAM STATISTICS ===" << std::endl;
        std::cout << "Total data sent: " << (total_bytes / 1024.0 / 1024.0) << " MB" << std::endl;
        std::cout << "Total frames: " << total_frames << std::endl;
        std::cout << "Dropped frames: " << dropped_frames << std::endl;
        std::cout << "Congestion: " << (congestion * 100.0) << "%" << std::endl;
        std::cout << "========================" << std::endl;
    }

private:
    void cleanup() {
        if (is_streaming) {
            stop_streaming();
        }

        for (int i = 0; i < 6; i++) {
            obs_set_output_source(i, nullptr);
        }

        if (rtmp_service) {
            obs_service_release(rtmp_service);
            rtmp_service = nullptr;
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

// Example usage with popular streaming platforms
void print_usage() {
    std::cout << "\nUsage: obs_rtmp_streaming <rtmp_server> <stream_key> [fps] [bitrate]" << std::endl;
    std::cout << "\nExamples:" << std::endl;
    std::cout << "  YouTube:  obs_rtmp_streaming \"rtmp://a.rtmp.youtube.com/live2\" \"your-stream-key\"" << std::endl;
    std::cout << "  Twitch:   obs_rtmp_streaming \"rtmp://live.twitch.tv/app\" \"your-stream-key\"" << std::endl;
    std::cout << "  Facebook: obs_rtmp_streaming \"rtmps://live-api-s.facebook.com:443/rtmp\" \"your-stream-key\"" << std::endl;
    std::cout << "  Custom:   obs_rtmp_streaming \"rtmp://your-server.com/live\" \"stream-key\" 30 5000" << std::endl;
}


int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cout << "OBS Multi-Monitor RTMP Streamer" << std::endl;
        std::cout << "===============================" << std::endl;
        print_usage();
        return 1;
    }

    std::string rtmp_server = argv[1];
    std::string stream_key = argv[2];
    int fps = 30;
    int bitrate = 5000;

    if (argc > 3) {
        fps = std::atoi(argv[3]);
        fps = (std::max)(10, (std::min)(fps, 60)); // Clamp between 10-60
    }

    if (argc > 4) {
        bitrate = std::atoi(argv[4]);
        bitrate = (std::max)(1000, (std::min)(bitrate, 50000)); // Clamp between 1-50 Mbps
    }

    std::cout << "OBS Multi-Monitor RTMP Streamer" << std::endl;
    std::cout << "===============================" << std::endl;
    std::cout << "Server: " << rtmp_server << std::endl;
    std::cout << "Stream Key: " << (stream_key.length() > 8 ? stream_key.substr(0, 8) + "..." : stream_key) << std::endl;
    std::cout << "FPS: " << fps << std::endl;
    std::cout << "Target Bitrate: " << bitrate << " kbps" << std::endl;
    std::cout << "\nIMPORTANT: Make sure OBS Studio is installed in the default location" << std::endl;
    std::cout << "\nPress Enter to continue..." << std::endl;
    std::cin.get();

    try {
        OBSRTMPStreamer streamer(rtmp_server, stream_key, fps, bitrate);
        streamer.run();
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
*/
#include "render/measurement-export.h"
#include "render/frame-probe.h"
#include <cmath>
#include <fstream>
#include <iomanip>
#include <locale>
namespace vaporcruise::render {
namespace {
std::string JsonString(const std::string& text) {
    constexpr char hex[] = "0123456789abcdef";
    std::string result = "\"";
    for (unsigned char c : text) {
        switch (c) {
        case '"': result += "\\\""; break;
        case '\\': result += "\\\\"; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        case '\b': result += "\\b"; break;
        case '\f': result += "\\f"; break;
        default:
            if (c < 32) { result += "\\u00"; result += hex[c >> 4]; result += hex[c & 15]; }
            else result += static_cast<char>(c);
        }
    }
    return result + '"';
}
}
bool SerializeMeasurements(std::ostream& csv, std::ostream& json,
    std::span<const FrameMeasurement> rows, const MeasurementMetadata& m) {
    csv.imbue(std::locale::classic()); json.imbue(std::locale::classic());
    csv << "timestamp_ms,cpu_active_ms,present_ms,gpu_ms,grid_phase,hint_opacity,grid_r,grid_g,grid_b,travel_cell,terrain_section,terrain_rows_built,city_light_phase\n" << std::setprecision(10);
    size_t missing = 0;
    for (const auto& row : rows) {
        csv << row.timestampMs << ',' << row.cpuActiveMs << ',' << row.presentMs << ',';
        if (std::isfinite(row.gpuMs)) csv << row.gpuMs; else ++missing;
        csv << ',' << row.gridPhase << ',' << row.hintOpacity << ',' << row.gridR << ',' << row.gridG << ',' << row.gridB
            << ',' << row.travelCell << ',' << row.terrainSection << ',' << row.terrainRowsBuilt << ',' << row.cityLightPhase << '\n';
    }
    const auto optionalNumber = [](std::optional<unsigned> value) { return value ? std::to_string(*value) : "null"; };
    json << std::setprecision(10) << "{\n  \"adapter\": " << JsonString(m.adapter)
        << ",\n  \"output_width\": " << m.width << ",\n  \"output_height\": " << m.height
        << ",\n  \"internal_width\": " << m.width << ",\n  \"internal_height\": " << m.height
        << ",\n  \"render_samples\": " << m.renderSamples
        << ",\n  \"refresh_hz\": " << optionalNumber(m.display ? std::optional<unsigned>(m.display->refreshHz) : std::nullopt)
        << ",\n  \"monitor_device\": " << (m.display ? JsonString(m.display->device) : "null")
        << ",\n  \"desktop_width\": " << optionalNumber(m.display ? std::optional<unsigned>(m.display->width) : std::nullopt)
        << ",\n  \"desktop_height\": " << optionalNumber(m.display ? std::optional<unsigned>(m.display->height) : std::nullopt)
        << ",\n  \"ac_line_status\": " << optionalNumber(m.acLineStatus)
        << ",\n  \"present_interval\": " << m.presentInterval
        << ",\n  \"frame_limit_fps\": " << (m.frameLimitFps ? std::to_string(m.frameLimitFps) : "null")
        << ",\n  \"preset\": \"background and depth-tested highway terrain\""
        << ",\n  \"terrain_seed\": \"" << m.terrainSeed << "\""
        << ",\n  \"warmup_seconds\": " << m.warmupSeconds << ",\n  \"capture_seconds\": " << m.captureSeconds
        << ",\n  \"startup_first_scene_ms\": " << m.startupMs
        << ",\n  \"measurement_start_monotonic_seconds\": " << std::setprecision(17) << m.measurementStartSeconds << std::setprecision(10)
        << ",\n  \"monitor_metadata_snapshot\": \"start of visible measurement session\""
        << ",\n  \"resource_generation\": " << m.resourceGeneration
        << ",\n  \"retained_frames\": " << rows.size() << ",\n  \"dropped_records\": " << m.dropped
        << ",\n  \"buffer_capacity\": " << m.capacity << ",\n  \"buffer_bytes\": " << m.capacity * sizeof(FrameMeasurement)
        << ",\n  \"gpu_missing_samples\": " << missing << ",\n  \"query_unavailable_events\": " << m.queryUnavailable
        << ",\n  \"query_slots\": " << m.querySlots << ",\n  \"gpu_query_read\": \"delayed DONOTFLUSH; no spin; one final poll\"\n}\n";
    csv.flush(); json.flush();
    return static_cast<bool>(csv) && static_cast<bool>(json);
}
bool WriteMeasurements(const std::filesystem::path& path, std::span<const FrameMeasurement> rows,
    const MeasurementMetadata& metadata, std::wstring& error) {
    error.clear();
    std::ofstream csv(path);
    if (!csv) { error = L"VaporCruise: measurement CSV creation failed."; return false; }
    std::ofstream json(std::filesystem::path(path.wstring() + L".json"));
    if (!json) { error = L"VaporCruise: measurement metadata creation failed."; return false; }
    const bool written = SerializeMeasurements(csv, json, rows, metadata);
    csv.close(); json.close();
    if (!written || !csv || !json) {
        error = !csv ? L"VaporCruise: measurement CSV write failed." : L"VaporCruise: measurement metadata write failed.";
        return false;
    }
    return true;
}
bool SerializeGpuDetails(std::ostream& csv,std::ostream& json,
    std::span<const GpuDetailMeasurement> rows,const MeasurementMetadata& m) {
    csv.imbue(std::locale::classic()); json.imbue(std::locale::classic());
    csv << "frame_index,timestamp_ms,probe_device_generation,gpu_detail_status,gpu_total_ms,gpu_uploads_setup_ms,gpu_terrain_ms,gpu_composite_ms,gpu_resolve_ms,resolve_executed\n" << std::setprecision(10);
    const auto statusName=[](GpuDetailStatus status) {
        switch (status) {
        case GpuDetailStatus::Pending: return "pending";
        case GpuDetailStatus::Ready: return "ready";
        case GpuDetailStatus::Unavailable: return "unavailable";
        case GpuDetailStatus::Aborted: return "aborted";
        case GpuDetailStatus::NoSlot: return "no_slot";
        case GpuDetailStatus::Detached: return "detached";
        case GpuDetailStatus::FinalPending: return "final_pending";
        }
        return "unavailable";
    };
    const auto writeNumber=[&csv](double value) { if (std::isfinite(value) && value>=0) csv << value; };
    size_t missing=0;
    for (size_t index=0; index<rows.size(); ++index) {
        const auto& row=rows[index];
        csv << index << ',' << row.timestampMs << ',' << row.deviceGeneration << ',' << statusName(row.status) << ',';
        writeNumber(row.gpuTotalMs);
        for (double value : row.passMilliseconds) {
            csv << ',';
            if (row.status==GpuDetailStatus::Ready) writeNumber(value);
        }
        csv << ',' << (row.resolveExecuted?1:0) << '\n';
        if (row.status!=GpuDetailStatus::Ready) ++missing;
    }
    json << std::setprecision(10) << "{\n  \"schema_version\": 2,\n  \"join\": \"zero-based frame_index and timestamp_ms in the original measurement CSV\""
        << ",\n  \"output_width\": " << m.width << ",\n  \"output_height\": " << m.height
        << ",\n  \"render_samples\": " << m.renderSamples << ",\n  \"present_interval\": " << m.presentInterval
        << ",\n  \"frame_limit_fps\": " << (m.frameLimitFps ? std::to_string(m.frameLimitFps) : "null")
        << ",\n  \"retained_frames\": " << rows.size() << ",\n  \"dropped_records\": " << m.dropped
        << ",\n  \"buffer_capacity\": " << m.capacity << ",\n  \"buffer_bytes\": " << m.capacity*sizeof(GpuDetailMeasurement)
        << ",\n  \"gpu_detail_missing_samples\": " << missing
        << ",\n  \"query_slots\": " << kFrameQuerySlots
        << ",\n  \"query_objects\": " << kFrameQuerySlots*GpuQueryObjectsPerSlot(true)
        << ",\n  \"gpu_query_read\": \"four-attempt delayed DONOTFLUSH; no spin; one final poll; at most six reads per slot per poll\""
        << ",\n  \"probe_device_generation\": \"increments on successful device attachment; pending old-device results are retired\""
        << ",\n  \"resolve_executed\": \"per-row actual resolve command issuance; zero identifies an empty resolve timestamp interval\""
        << ",\n  \"intervals\": {"
        << "\n    \"gpu_uploads_setup_ms\": \"terrain vertex uploads and render-target/depth/viewport setup; frame constant upload excluded\","
        << "\n    \"gpu_terrain_ms\": \"terrain bindings, per-section constants and indexed draws; includes UI shader work\","
        << "\n    \"gpu_composite_ms\": \"background bindings and fullscreen composite draw; includes UI shader work\","
        << "\n    \"gpu_resolve_ms\": \"optional MSAA resolve and target unbind; empty timestamp interval at 1x\"\n  }\n}\n";
    csv.flush(); json.flush();
    return static_cast<bool>(csv) && static_cast<bool>(json);
}
bool WriteGpuDetails(const std::filesystem::path& path,std::span<const GpuDetailMeasurement> rows,
    const MeasurementMetadata& metadata,std::wstring& error) {
    error.clear();
    std::ofstream csv(std::filesystem::path(path.wstring()+L".gpu.csv"));
    if (!csv) { error=L"VaporCruise: GPU detail CSV creation failed."; return false; }
    std::ofstream json(std::filesystem::path(path.wstring()+L".gpu.json"));
    if (!json) { error=L"VaporCruise: GPU detail metadata creation failed."; return false; }
    const bool written=SerializeGpuDetails(csv,json,rows,metadata);
    csv.close(); json.close();
    if (!written || csv.fail() || json.fail()) { error=L"VaporCruise: GPU detail output write/flush failed."; return false; }
    return true;
}
}

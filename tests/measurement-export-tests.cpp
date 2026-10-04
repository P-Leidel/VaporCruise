#include "render/measurement-export.h"
#include <cmath>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <locale>
#include <sstream>
using namespace vaporcruise::render;
namespace {
int failures = 0;
void Check(bool ok, const char* name) { if (!ok) { std::cerr << "FAIL: " << name << '\n'; ++failures; } }
struct Comma : std::numpunct<char> { char do_decimal_point() const override { return ','; } };
struct FailingBuffer : std::streambuf { int overflow(int) override { return traits_type::eof(); } };
struct FlushFailure : std::stringbuf { int sync() override { return -1; } };
}
int RunTests() {
    FrameMeasurement rows[2]{};
    rows[0].timestampMs = 1.25; rows[0].cpuActiveMs = 3; rows[0].presentMs = 5;
    rows[0].travelCell = 36028797018963971; rows[0].terrainSection = 1125899906842624;
    rows[0].terrainRowsBuilt = 4; rows[1].gpuMs = 2.5;
    MeasurementMetadata meta;
    meta.adapter = "GPU \xc3\xa9\"\\\n\t\x01";
    meta.terrainSeed = 6215337262362608466ULL;
    meta.capacity = 3; meta.dropped = 7; meta.queryUnavailable = 9; meta.querySlots = 16;
    meta.measurementStartSeconds = 123.12345678901234;
    std::ostringstream csv, json;
    csv.imbue(std::locale(std::locale::classic(), new Comma));
    json.imbue(std::locale(std::locale::classic(), new Comma));
    Check(SerializeMeasurements(csv, json, rows, meta), "stream export succeeds");
    const std::string header = "timestamp_ms,cpu_active_ms,present_ms,gpu_ms,grid_phase,hint_opacity,grid_r,grid_g,grid_b,travel_cell,terrain_section,terrain_rows_built,city_light_phase\n";
    Check(csv.str() == header + "1.25,3,5,,0,0,0,0,0,36028797018963971,1125899906842624,4,0\n0,0,0,2.5,0,0,0,0,0,0,0,0,0\n", "exact CSV order, units, blanks and distant integers under comma locale");
    Check(json.str().find("\"frame_limit_fps\": null") != std::string::npos,
          "default metadata records absence of an additional frame cap");
    for (auto key : {"adapter", "output_width", "output_height", "internal_width", "internal_height", "render_samples", "refresh_hz", "monitor_device", "desktop_width", "desktop_height", "ac_line_status", "present_interval", "preset", "terrain_seed", "warmup_seconds", "capture_seconds", "startup_first_scene_ms", "measurement_start_monotonic_seconds", "monitor_metadata_snapshot", "resource_generation", "retained_frames", "dropped_records", "buffer_capacity", "buffer_bytes", "gpu_missing_samples", "query_unavailable_events", "query_slots", "gpu_query_read"})
        Check(json.str().find(std::string("\"") + key + "\":") != std::string::npos, key);
    for (auto token : {"\"refresh_hz\": null", "\"monitor_device\": null", "\"desktop_width\": null", "\"desktop_height\": null", "\"ac_line_status\": null", "\"terrain_seed\": \"6215337262362608466\"", "\"gpu_missing_samples\": 1", "\"dropped_records\": 7", "\"query_unavailable_events\": 9", "\"query_slots\": 16", "123.12345678901234", "GPU \xc3\xa9\\\"\\\\\\n\\t\\u0001", "background and depth-tested highway terrain", "start of visible measurement session", "delayed DONOTFLUSH; no spin; one final poll"})
        Check(json.str().find(token) != std::string::npos, token);
    Check(json.str().find("\"buffer_bytes\": " + std::to_string(3 * sizeof(FrameMeasurement))) != std::string::npos, "buffer byte accounting");
    GpuDetailMeasurement details[2]{};
    details[0].timestampMs=1.25; details[0].deviceGeneration=2; details[0].status=GpuDetailStatus::Ready;
    details[0].gpuTotalMs=10; details[0].passMilliseconds={1,2,3,4};
    details[0].resolveExecuted=true;
    details[1].timestampMs=2.5; details[1].status=GpuDetailStatus::Aborted;
    details[1].passMilliseconds={1,2,3,4};
    std::ostringstream detailCsv,detailJson; detailCsv.imbue(std::locale(std::locale::classic(),new Comma));
    Check(SerializeGpuDetails(detailCsv,detailJson,details,meta),"separate opt-in detail export succeeds");
    Check(detailCsv.str()=="frame_index,timestamp_ms,probe_device_generation,gpu_detail_status,gpu_total_ms,gpu_uploads_setup_ms,gpu_terrain_ms,gpu_composite_ms,gpu_resolve_ms,resolve_executed\n0,1.25,2,ready,10,1,2,3,4,1\n1,2.5,0,aborted,,,,,,0\n",
        "detail sidecar retains actual per-row resolve independent of final 1x metadata, joins and unavailable blanks");
    Check(detailJson.str().find("\"schema_version\": 2")!=std::string::npos
        && detailJson.str().find("\"gpu_detail_missing_samples\": 1")!=std::string::npos
        && detailJson.str().find("\"query_objects\": 96")!=std::string::npos,
        "interior-free detail schema and coverage are independent of unchanged metadata");
    meta.frameLimitFps = 60;
    std::ostringstream cappedCsv,cappedJson,cappedDetailCsv,cappedDetailJson;
    SerializeMeasurements(cappedCsv,cappedJson,rows,meta);
    SerializeGpuDetails(cappedDetailCsv,cappedDetailJson,details,meta);
    Check(cappedCsv.str() == csv.str() && cappedDetailCsv.str() == detailCsv.str(),
          "cap metadata preserves both original and GPU detail CSV byte contracts");
    Check(cappedJson.str().find("\"frame_limit_fps\": 60") != std::string::npos &&
          cappedDetailJson.str().find("\"frame_limit_fps\": 60") != std::string::npos,
          "locked cap choice is reproducible from either metadata file");
    meta.display = DisplayMeasurementContext{"Monitor \xc3\xb6", 144, 1920, 1080}; meta.acLineStatus = 1;
    std::ostringstream csv2, json2; SerializeMeasurements(csv2,json2,rows,meta);
    Check(json2.str().find("\"refresh_hz\": 144") != std::string::npos && json2.str().find("\"ac_line_status\": 1") != std::string::npos, "available metadata numbers");
    FailingBuffer bad; std::ostream broken(&bad); std::ostringstream good;
    Check(!SerializeGpuDetails(broken,good,details,meta),"detail CSV stream failure terminal");
    Check(!SerializeMeasurements(broken, good, rows, meta), "CSV stream error fails");
    std::ostringstream other;
    Check(!SerializeMeasurements(other, broken, rows, meta), "JSON stream error fails");
    FlushFailure flush; std::ostream flushStream(&flush); std::ostringstream next;
    Check(!SerializeMeasurements(flushStream, next, rows, meta), "flush failure fails");
    FlushFailure detailFlush; std::ostream detailFlushStream(&detailFlush); std::ostringstream detailNext;
    Check(!SerializeGpuDetails(detailFlushStream,detailNext,details,meta),"detail flush failure terminal");
    const auto root = std::filesystem::temp_directory_path() / ("measurement-export-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(root);
    std::wstring error;
    Check(!WriteMeasurements(root / "missing" / "run.csv", rows, meta, error) && error.find(L"CSV") != std::wstring::npos, "missing parent fails with CSV error");
    Check(!WriteGpuDetails(root / "missing" / "run.csv",details,meta,error) && error.find(L"GPU detail CSV")!=std::wstring::npos,"detail missing parent retains specific CSV diagnostic");
    Check(!WriteMeasurements(root, rows, meta, error), "CSV directory fails");
    std::filesystem::create_directory(root / "run.csv.json");
    Check(!WriteMeasurements(root / "run.csv", rows, meta, error) && error.find(L"metadata") != std::wstring::npos, "metadata directory fails");
    Check(WriteMeasurements(root / "good.csv", rows, meta, error), "real file export succeeds");
    std::filesystem::create_directory(root / "blocked.csv.gpu.json");
    Check(!WriteGpuDetails(root / "blocked.csv",details,meta,error) && error.find(L"GPU detail metadata")!=std::wstring::npos,"detail metadata path failure terminal and diagnostic");
    Check(WriteGpuDetails(root / "good.csv",details,meta,error),"real separate sidecar export succeeds");
    std::ifstream detailedActual(root / "good.csv.gpu.csv"); std::stringstream detailedContents; detailedContents<<detailedActual.rdbuf();
    Check(detailedContents.str()==detailCsv.str(),"detail file and stream outputs agree"); detailedActual.close();
    std::ifstream actual(root / "good.csv"); std::stringstream contents; contents << actual.rdbuf();
    Check(contents.str() == csv2.str(), "file and stream outputs agree");
    actual.close();
    std::filesystem::remove_all(root);
    return failures ? 1 : 0;
}
int main() {
    try { return RunTests(); }
    catch (const std::exception& e) { std::cerr << "FAIL: export test exception: " << e.what() << '\n'; return 1; }
}

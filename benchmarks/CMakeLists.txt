# Benchmarks and M0 probes. The brush latency benchmark lives in the desktop app (S1-3.7); S1-6 adds the
# probes below and bench_main (S1-6.8). The aggregate target builds them all.
add_custom_target(benchmarks)

# A minimal PNG writer shared by the probes (the product codec arrives in io/formats, S1-8).
add_library(archmage_probe_png STATIC probes/ProbePng.cpp)
target_link_libraries(archmage_probe_png PUBLIC PNG::PNG)
archmage_target_warnings(archmage_probe_png)

# M0-E4: bounded PSD/PSB parse, unsupported-feature records, CPU render of flat layers.
add_executable(psd_probe probes/psd_probe.cpp)
target_link_libraries(psd_probe PRIVATE archmage_core archmage_probe_png)
archmage_target_warnings(psd_probe)
add_dependencies(benchmarks psd_probe)

# M0-E5: LibRaw opens a RAW or DNG file, reports camera and levels, demosaics into a float32 tile map.
add_executable(raw_probe probes/raw_probe.cpp)
target_link_libraries(raw_probe PRIVATE archmage_core archmage_probe_png PkgConfig::LibRaw)
archmage_target_warnings(raw_probe)
add_dependencies(benchmarks raw_probe)

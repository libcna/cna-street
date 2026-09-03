# cna-street's own tests. Deliberately plain executables registered with CTest
# rather than a test framework dependency: the logic under test is small,
# self-contained and easier to keep honest than to describe in fixtures.
function(cna_street_test name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE CnaStreet::Core CnaStreet::Warnings)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

cna_street_test(traffic_signal_tests TrafficSignalTests.cpp)
cna_street_test(traffic_system_tests TrafficSystemTests.cpp)
cna_street_test(pedestrian_tests PedestrianTests.cpp)
cna_street_test(geometry_tests GeometryTests.cpp)
cna_street_test(layout_tests LayoutTests.cpp)
cna_street_test(settings_tests SettingsTests.cpp)
cna_street_test(camera_tests CameraTests.cpp)
cna_street_test(content_pipeline_tests ContentPipelineTests.cpp)
cna_street_test(appearance_tests AppearanceTests.cpp)

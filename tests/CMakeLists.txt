# cna-street's own tests. Deliberately plain executables registered with CTest
# rather than a test framework dependency: the logic under test is small,
# self-contained and easier to keep honest than to describe in fixtures.
function(cna_street_test name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE CnaStreet::Core CnaStreet::Warnings)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

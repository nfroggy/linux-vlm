add_library(vlm_stub STATIC stub.c)
target_link_libraries(vlm_stub PUBLIC vlm_options)
target_compile_options(vlm_stub PRIVATE -Wno-unused-label -Wno-unused-function)

find_program(CLISP_EXECUTABLE NAMES clisp REQUIRED)

set(stub_names
    ifunhead idispat ifuncom1 ifuncom2 ifungene ifunfcal ifunloop
    ifunlist ifuninst ifunmath ifunarra ifunmove ifunpred ifunsubp
    ifunfext ifunlexi ifunbits ifunblok ifunbind ifunfull ifunbnum
    ifuntrap ihalt idouble ifunjosh ifuntran)
set(generated_stubs)
set(assembly_sources)
foreach(name IN LISTS stub_names)
    list(APPEND generated_stubs "${CMAKE_CURRENT_BINARY_DIR}/${name}.c")
    list(APPEND assembly_sources "${PROJECT_SOURCE_DIR}/alpha-emulator/${name}.as")
endforeach()
set(macro_sources)
foreach(name aistat alphamac stacklis memoryem imaclist fcallmac imacbits
        imacblok imaclexi imacgene imacinst imacialu imacloop imacmath
        imacbind imacjosh imacarra imacpred imacsubp imactrap)
    list(APPEND macro_sources "${PROJECT_SOURCE_DIR}/alpha-emulator/${name}.lisp")
endforeach()

add_custom_command(OUTPUT ${generated_stubs}
    COMMAND "${CMAKE_COMMAND}" -E env
        "VLM_STUB_OUTPUT_DIRECTORY=${CMAKE_CURRENT_BINARY_DIR}/"
        "${CLISP_EXECUTABLE}" -norc "${CMAKE_CURRENT_SOURCE_DIR}/process.lisp"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    DEPENDS ${assembly_sources} ${macro_sources}
        process.lisp clisp-support.lisp intrpmac.lisp
        "${PROJECT_SOURCE_DIR}/emulator/aihead.lisp"
        "${PROJECT_SOURCE_DIR}/emulator/errortbl.lisp"
        "${PROJECT_SOURCE_DIR}/emulator/traps.lisp"
    COMMENT "Generating interpreter sources with CLISP"
    VERBATIM)
add_custom_target(generate-stubs DEPENDS ${generated_stubs})
add_dependencies(vlm_stub generate-stubs)
target_include_directories(vlm_stub PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

if(BUILD_TESTING)
    add_executable(float-test float_test.c)
    target_link_libraries(float-test PRIVATE vlm_options)
    target_compile_options(float-test PRIVATE -Wno-unused-function)
    add_test(NAME float-conversion COMMAND float-test)
endif()

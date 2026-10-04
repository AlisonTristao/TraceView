# tracelab_code: blocks of user C code -- a plant, a filter, a controller --
# compiled in memory by libtcc (the Tiny C Compiler) when they are saved, and
# run tick by tick (PLANO_BLOCOS_DE_CODIGO.md, lab/README.md "Code blocks").
#
# libtcc is LGPL-2.1, so it is built as a shared library of its own
# (tracelab_tcc) next to the executable, never linked statically into the
# MIT-licensed TraceView.

# libtcc and the fault guard (codecall.c) are C.
enable_language(C)

include(FetchContent)
FetchContent_Declare(
    tracelab_tinycc
    GIT_REPOSITORY https://github.com/TinyCC/tinycc.git
    # mob, 2026-10: tcc_list_symbols(), tcc_relocate(s) without a buffer,
    # _tcc_setjmp() + tcc_set_backtrace_func() to catch faults in run code.
    GIT_TAG 43c7708b85681a2fd4451c8a541af4494a8919b2
    # No CMakeLists.txt there: only the sources are fetched.
    SOURCE_SUBDIR no-cmake-build
)
FetchContent_MakeAvailable(tracelab_tinycc)

# The target tcc compiles FOR -- this machine: the code runs in process.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(AMD64|x86_64|amd64)$")
    set(_tl_tcc_cpu TCC_TARGET_X86_64)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$")
    set(_tl_tcc_cpu TCC_TARGET_ARM64)
else()
    message(FATAL_ERROR "TraceLab code blocks: libtcc has no backend for ${CMAKE_SYSTEM_PROCESSOR}")
endif()
set(_tl_tcc_defs ${_tl_tcc_cpu})
if(WIN32)
    list(APPEND _tl_tcc_defs TCC_TARGET_PE)
elseif(APPLE)
    list(APPEND _tl_tcc_defs TCC_TARGET_MACHO)
endif()

# A copy of the sources with one change to tccrun.c: tcc's fault handler
# (installed process-wide once code compiled with -bt is relocated) only
# handles a fault whose instruction lies in code tcc compiled, inside a
# tcc_setjmp() guard. Unpatched, every other fault and exception of the
# process -- Qt's, a C++ throw -- would reach it, and it ends the process
# with exit(). Done by string replacement, so re-configuring is harmless.
set(_tl_tcc_src ${CMAKE_CURRENT_BINARY_DIR}/tinycc-src)
file(COPY ${tracelab_tinycc_SOURCE_DIR}/ DESTINATION ${_tl_tcc_src}
     PATTERN ".git" EXCLUDE PATTERN "tests" EXCLUDE PATTERN "examples" EXCLUDE)
file(READ ${_tl_tcc_src}/tccrun.c _tl_tccrun)
macro(_tl_patch_tccrun from to)
    string(FIND "${_tl_tccrun}" "${from}" _tl_at)
    if(_tl_at LESS 0)
        message(FATAL_ERROR "TraceLab code blocks: tccrun.c changed, the fault-handler patch no longer applies")
    endif()
    string(REPLACE "${from}" "${to}" _tl_tccrun "${_tl_tccrun}")
endmacro()
if(NOT _tl_tccrun MATCHES "rt_tracelab_guarded")
    # rt_exit()'s definition (not its forward declaration) is the anchor.
    set(_tl_anchor "static void rt_exit(rt_frame *f, int code)
{
    TCCState *s;")
    _tl_patch_tccrun("${_tl_anchor}" "/* tracelab: is this fault on an instruction of code a state compiled, while
   that state is inside a tcc_setjmp() guard? Anything else belongs to the
   host application and is left alone. */
static int rt_tracelab_guarded(rt_frame *f)
{
    TCCState *s;
    for (s = g_s1; s; s = s->next)
        if (s->run_lj && f->ip >= (addr_t)s->run_ptr
         && f->ip < (addr_t)s->run_ptr + s->run_size)
            return 1;
    return 0;
}

${_tl_anchor}")
    _tl_patch_tccrun("    rt_getcontext(puc, &f);
"
        "    rt_getcontext(puc, &f);
    if (!rt_tracelab_guarded(&f)) {
        signal(signum, SIG_DFL);
        return;
    }
")
    _tl_patch_tccrun("    rt_getcontext(ex_info->ContextRecord, &f);
"
        "    rt_getcontext(ex_info->ContextRecord, &f);
    if (!rt_tracelab_guarded(&f))
        return EXCEPTION_CONTINUE_SEARCH;
")
    file(WRITE ${_tl_tcc_src}/tccrun.c "${_tl_tccrun}")
endif()
# Only libtcc.h is public: tcc's source folder also holds a file named
# "version", which would hide C++'s <version> header from anything that
# had the whole folder on its include path.
configure_file(${_tl_tcc_src}/libtcc.h ${CMAKE_CURRENT_BINARY_DIR}/tcc-include/libtcc.h COPYONLY)
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/tcc-config/config.h
     "#define TCC_VERSION \"0.9.28rc\"\n#define CONFIG_TCC_PREDEFS 0\n")

add_library(tracelab_tcc SHARED ${_tl_tcc_src}/libtcc.c)
set_target_properties(tracelab_tcc PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    C_VISIBILITY_PRESET default
    # Next to TraceView's executable, where Windows looks for it.
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
target_compile_definitions(tracelab_tcc PRIVATE ${_tl_tcc_defs} ONE_SOURCE=1
                           $<$<BOOL:${WIN32}>:LIBTCC_AS_DLL>)
target_include_directories(tracelab_tcc
    PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/tcc-config ${_tl_tcc_src}
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/tcc-include>)
if(MSVC)
    target_compile_options(tracelab_tcc PRIVATE /w)
else()
    target_compile_options(tracelab_tcc PRIVATE -w -O2)
endif()
if(UNIX)
    target_link_libraries(tracelab_tcc PRIVATE ${CMAKE_DL_LIBS} m pthread)
endif()
# Packaged like TraceView itself (root CMakeLists.txt): next to the .exe on
# Windows, bin/ + lib/ elsewhere.
if(WIN32)
    install(TARGETS tracelab_tcc RUNTIME DESTINATION .)
else()
    install(TARGETS tracelab_tcc RUNTIME DESTINATION bin LIBRARY DESTINATION lib)
endif()

# Copies libtcc next to `target` (a test, a tool) so it starts from its own
# build folder.
function(tracelab_copy_tcc_runtime target)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:tracelab_tcc>
                $<TARGET_FILE_DIR:${target}>)
endfunction()

# What the compiled code sees: tcc's own freestanding headers (stddef.h,
# stdarg.h, tccdefs.h...), the runtime helpers it may call (libtcc1.c, and on
# Windows __chkstk/alloca), and TraceLab's headers (tracelab.h and the libc
# subset). CodeRuntime writes them to a folder on first use.
set(_tl_rt ${CMAKE_CURRENT_BINARY_DIR}/tcc-runtime)
file(MAKE_DIRECTORY ${_tl_rt})
set(_tl_rt_files)
foreach(_tl_header float.h stdalign.h stdarg.h stdbool.h stddef.h stdnoreturn.h tccdefs.h varargs.h)
    configure_file(${_tl_tcc_src}/include/${_tl_header} ${_tl_rt}/${_tl_header} COPYONLY)
    list(APPEND _tl_rt_files ${_tl_rt}/${_tl_header})
endforeach()
configure_file(${_tl_tcc_src}/lib/libtcc1.c ${_tl_rt}/libtcc1.c COPYONLY)
configure_file(${_tl_tcc_src}/lib/alloca.S ${_tl_rt}/alloca.S COPYONLY)
list(APPEND _tl_rt_files ${_tl_rt}/libtcc1.c ${_tl_rt}/alloca.S)
if(WIN32)
    configure_file(${_tl_tcc_src}/win32/lib/chkstk.S ${_tl_rt}/chkstk.S COPYONLY)
    list(APPEND _tl_rt_files ${_tl_rt}/chkstk.S)
endif()

add_library(tracelab_code STATIC
    codecall.c
    codecall.h
    codefiles.cpp
    codefiles.h
    codehostapi.cpp
    codehostapi.h
    codemodule.cpp
    codemodule.h
    coderuntime.cpp
    coderuntime.h
)
target_include_directories(tracelab_code PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(tracelab_code PUBLIC Qt6::Core PRIVATE tracelab_tcc)
qt_add_resources(tracelab_code tracelab_code_runtime
    PREFIX "/tracelab/code/runtime"
    BASE ${_tl_rt}
    FILES ${_tl_rt_files}
)
qt_add_resources(tracelab_code tracelab_code_headers
    PREFIX "/tracelab/code/runtime"
    BASE include
    FILES
        include/math.h
        include/stdint.h
        include/stdio.h
        include/stdlib.h
        include/string.h
        include/tracelab.h
)
qt_add_resources(tracelab_code tracelab_code_skeletons
    PREFIX "/tracelab/code/skeletons"
    BASE skeletons
    FILES
        skeletons/controller.c
        skeletons/filter.c
        skeletons/plant.c
)

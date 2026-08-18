set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)
set(XWIN "$ENV{HOME}/winsdk")
set(CMAKE_C_COMPILER   clang-cl)
set(CMAKE_CXX_COMPILER clang-cl)
set(CMAKE_LINKER       lld-link)
set(CMAKE_RC_COMPILER  llvm-rc)
set(CMAKE_MT           llvm-mt)
# xwin ships only the RELEASE CRT (no msvcrtd.lib), so pin the release runtime everywhere
set(CMAKE_TRY_COMPILE_CONFIGURATION Release)
set(CMAKE_POLICY_DEFAULT_CMP0091 NEW)
set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
set(_XI "/imsvc${XWIN}/crt/include /imsvc${XWIN}/sdk/include/ucrt /imsvc${XWIN}/sdk/include/um /imsvc${XWIN}/sdk/include/shared /imsvc${XWIN}/sdk/include/winrt /imsvc${XWIN}/sdk/include/cppwinrt")
set(CMAKE_C_FLAGS_INIT   "--target=x86_64-pc-windows-msvc /X ${_XI}")
set(CMAKE_CXX_FLAGS_INIT "--target=x86_64-pc-windows-msvc /X ${_XI} /clang:-ferror-limit=0")
# llvm-rc does not see the /imsvc paths, so give the resource compiler its own includes
set(CMAKE_RC_FLAGS_INIT "-I${XWIN}/sdk/include/um -I${XWIN}/sdk/include/shared -I${XWIN}/crt/include")

set(CMAKE_C_FLAGS_DEBUG_INIT   "/MD /Od")
set(CMAKE_CXX_FLAGS_DEBUG_INIT "/MD /Od")
set(CMAKE_C_FLAGS_RELEASE_INIT   "/MD /O2 /DNDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE_INIT "/MD /O2 /DNDEBUG")
set(_XL "/libpath:${XWIN}/crt/lib/x86_64 /libpath:${XWIN}/sdk/lib/um/x86_64 /libpath:${XWIN}/sdk/lib/ucrt/x86_64")
set(CMAKE_EXE_LINKER_FLAGS_INIT    "${_XL}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${_XL}")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${_XL}")

# Cross-compile isolation: without this, find_package() picks up HOST libraries (OpenXR's
# loader found the system jsoncpp) and drags /usr/include into a Windows build.
set(CMAKE_FIND_ROOT_PATH "${XWIN}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

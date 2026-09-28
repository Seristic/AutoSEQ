set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)

# Pin dependencies to the VS 2022 (v143) toolset. Without this, vcpkg picks the newest
# Visual Studio installed, and MSVC 14.50+ (VS 18 / v145) removed stdext::checked_array_iterator,
# which fmt 9.1.0 (pulled in by CommonLibSSE-NG) still uses.
set(VCPKG_PLATFORM_TOOLSET v143)

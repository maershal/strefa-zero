#pragma once
#include <filesystem>

// RenderDoc in-application API for StrefaQA (docs/adr/0002-narzedzia-profilowania.md): F11 captures the next frame.
// RenderDoc is used when it is already in the process (the program was launched from RenderDoc) or when
// STREFA_RENDERDOC names the library to load ("1" = renderdoc.dll / librenderdoc.so from the search path).
namespace strefa::tools {
// Must run before the GL context exists: RenderDoc hooks only the OpenGL contexts created after it is loaded.
void attach_renderdoc();
// Registers the F11 watch once SDL events work; captures go to <profile>/renderdoc. False without RenderDoc.
bool watch_renderdoc_hotkey(const std::filesystem::path& profile);
} // namespace strefa::tools

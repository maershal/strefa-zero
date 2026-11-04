#pragma once
// Tracy CPU zones (docs/adr/0002-narzedzia-profilowania.md). Without STREFA_TRACY (the default) every macro expands to
// nothing, so a default build has no Tracy symbol; GPU zones are in platform/gpu_zone.hpp.
#ifdef STREFA_TRACY
#include <tracy/Tracy.hpp>
// At most one zone per scope: the macro declares a fixed variable name.
#define STREFA_ZONE(name) ZoneScopedN(name)
#define STREFA_FRAME_MARK() FrameMark
#define STREFA_PLOT(name, value) TracyPlot(name, static_cast<double>(value))
#else
#define STREFA_ZONE(name) static_cast<void>(0)
#define STREFA_FRAME_MARK() static_cast<void>(0)
#define STREFA_PLOT(name, value) static_cast<void>(sizeof(value))
#endif

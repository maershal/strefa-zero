#pragma once
#include "gpu/gl_api.hpp"

#include <cstddef>
#include <span>
#include <vector>

namespace strefa::gpu::detail {
// Diagnostic read of a buffer the simulation or presentation keeps using: copy the range into a fresh GL_STREAM_READ
// buffer and read that copy. glGetBufferSubData on the live SSBO lets the driver move it to host memory (NVIDIA
// debug message 131186) and back on the next dispatch, a cost the following benchmark segment would pay. Synchronous:
// the read waits for the copy. Leaves GL_COPY_READ_BUFFER and GL_COPY_WRITE_BUFFER bound to deleted names, so callers
// invalidate their binding caches as after any other read.
inline void read_staged(const Api& gl, GLuint source, GLintptr offset, std::span<std::byte> out) {
    if (out.empty()) return;
    const auto size = static_cast<GLsizeiptr>(out.size());
    GLuint staging{};
    gl.MemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
    gl.GenBuffers(1, &staging);
    gl.BindBuffer(GL_COPY_WRITE_BUFFER, staging);
    gl.BufferData(GL_COPY_WRITE_BUFFER, size, nullptr, GL_STREAM_READ);
    gl.BindBuffer(GL_COPY_READ_BUFFER, source);
    gl.CopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, offset, 0, size);
    gl.BindBuffer(GL_COPY_READ_BUFFER, staging);
    gl.GetBufferSubData(GL_COPY_READ_BUFFER, 0, size, out.data());
    gl.DeleteBuffers(1, &staging);
}
template<class T, std::size_t N> void read_staged(const Api& gl, GLuint source, GLintptr offset, std::span<T, N> out) {
    read_staged(gl, source, offset, std::span<std::byte>{std::as_writable_bytes(out)});
}
// Appends the whole content of `source` to `out` through a staging copy.
inline void append_staged(const Api& gl, GLuint source, std::vector<std::byte>& out) {
    gl.BindBuffer(GL_COPY_READ_BUFFER, source);
    GLint size{};
    gl.GetBufferParameteriv(GL_COPY_READ_BUFFER, GL_BUFFER_SIZE, &size);
    const auto at = out.size();
    out.resize(at + static_cast<std::size_t>(size));
    read_staged(gl, source, 0, std::span{out}.subspan(at));
}
} // namespace strefa::gpu::detail

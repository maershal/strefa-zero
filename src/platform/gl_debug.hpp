#pragma once
#include <filesystem>
#include <memory>
namespace strefa::platform {
// Bounded callback storage; writing the report happens outside driver callbacks.
class GlDebugLog {
public:
    static std::unique_ptr<GlDebugLog> create(const std::filesystem::path& output);
    ~GlDebugLog();
    GlDebugLog(const GlDebugLog&)=delete;
    GlDebugLog& operator=(const GlDebugLog&)=delete;
private:
    struct Impl;
    explicit GlDebugLog(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
}

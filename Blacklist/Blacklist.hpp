#pragma once
#include <string>
#include <vector>
#include <filesystem>

namespace Juke {
    struct BlacklistEntry {
        std::string Name;
        std::string PolicyHash;
        std::string SharedHash;
        std::string Message;
    };

    bool CheckBlacklist(const std::filesystem::path& policyPath, const std::filesystem::path& sharedPath);
}

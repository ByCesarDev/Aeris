#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace aeris {

enum class SignatureStatus {
    Verified,              // Exactly 1 unique match found in valid executable memory
    NotFound,              // 0 matches found in target memory range
    Ambiguous,             // Multiple matches found (unsafe / collision risk)
    InvalidMemoryRegion,   // Target address is outside executable range or lacks execution permissions
    IncompatibleVersion,   // Game/loader version does not match target version
    DisabledBySafety       // Detour/inspection bypassed due to active safe mode
};

constexpr std::string_view signatureStatusToString(SignatureStatus status) {
    switch (status) {
    case SignatureStatus::Verified:
        return "Verified";
    case SignatureStatus::NotFound:
        return "NotFound";
    case SignatureStatus::Ambiguous:
        return "Ambiguous";
    case SignatureStatus::InvalidMemoryRegion:
        return "InvalidMemoryRegion";
    case SignatureStatus::IncompatibleVersion:
        return "IncompatibleVersion";
    case SignatureStatus::DisabledBySafety:
        return "DisabledBySafety";
    default:
        return "Unknown";
    }
}

struct MemoryRegionInfo {
    uintptr_t baseAddress  = 0;
    size_t    size         = 0;
    bool      isCommitted  = true;
    bool      isExecutable = true;
};

struct PatternByte {
    uint8_t byte       = 0;
    bool    isWildcard = false;
};

struct PatternMatchResult {
    SignatureStatus  status       = SignatureStatus::NotFound;
    uintptr_t        address      = 0;
    size_t           matchCount   = 0;
    std::string_view matchedPattern;
    std::string      details;
};

class SignatureVerifier {
public:
    static std::vector<PatternByte> parsePattern(std::string_view patternStr) {
        std::vector<PatternByte> result;
        size_t i = 0;
        size_t const len = patternStr.length();

        while (i < len) {
            while (i < len && (patternStr[i] == ' ' || patternStr[i] == '\t' || patternStr[i] == '\r' || patternStr[i] == '\n')) {
                ++i;
            }
            if (i >= len) break;

            if (patternStr[i] == '?') {
                result.push_back({0, true});
                ++i;
                if (i < len && patternStr[i] == '?') {
                    ++i;
                }
            } else {
                if (i + 1 < len) {
                    char c1 = patternStr[i];
                    char c2 = patternStr[i + 1];
                    auto hexVal = [](char c) -> int {
                        if (c >= '0' && c <= '9') return c - '0';
                        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                        return -1;
                    };
                    int v1 = hexVal(c1);
                    int v2 = hexVal(c2);
                    if (v1 >= 0 && v2 >= 0) {
                        result.push_back({static_cast<uint8_t>((v1 << 4) | v2), false});
                        i += 2;
                        continue;
                    }
                }
                ++i;
            }
        }
        return result;
    }

    static std::vector<uintptr_t> scanSpan(
        std::span<uint8_t const> buffer,
        std::vector<PatternByte> const& pattern,
        uintptr_t baseAddress = 0,
        size_t maxMatches = 16
    ) {
        std::vector<uintptr_t> matches;
        if (pattern.empty() || buffer.size() < pattern.size()) {
            return matches;
        }

        size_t const limit = buffer.size() - pattern.size();
        size_t const patSize = pattern.size();

        for (size_t i = 0; i <= limit; ++i) {
            bool matched = true;
            for (size_t j = 0; j < patSize; ++j) {
                if (!pattern[j].isWildcard && buffer[i + j] != pattern[j].byte) {
                    matched = false;
                    break;
                }
            }
            if (matched) {
                matches.push_back(baseAddress + i);
                if (matches.size() >= maxMatches) {
                    break;
                }
            }
        }

        return matches;
    }

    static SignatureStatus verifyMemoryRegion(uintptr_t address, MemoryRegionInfo const& region) {
        if (region.size == 0) {
            return SignatureStatus::InvalidMemoryRegion;
        }
        if (address < region.baseAddress || address >= (region.baseAddress + region.size)) {
            return SignatureStatus::InvalidMemoryRegion;
        }
        if (!region.isCommitted || !region.isExecutable) {
            return SignatureStatus::InvalidMemoryRegion;
        }
        return SignatureStatus::Verified;
    }

    static PatternMatchResult evaluate(
        size_t matchCount,
        uintptr_t firstMatchAddress,
        std::string_view pattern,
        MemoryRegionInfo const& region,
        bool isVersionCompatible
    ) {
        PatternMatchResult result;
        result.matchCount     = matchCount;
        result.address        = firstMatchAddress;
        result.matchedPattern = pattern;

        if (!isVersionCompatible) {
            result.status  = SignatureStatus::IncompatibleVersion;
            result.details = "Version incompatible with signature profile";
            return result;
        }

        if (matchCount == 0) {
            result.status  = SignatureStatus::NotFound;
            result.details = "Pattern not found in target memory";
            return result;
        }

        if (matchCount > 1) {
            result.status  = SignatureStatus::Ambiguous;
            result.details = "Ambiguous pattern: " + std::to_string(matchCount) + " matches detected";
            return result;
        }

        SignatureStatus regionStatus = verifyMemoryRegion(firstMatchAddress, region);
        if (regionStatus != SignatureStatus::Verified) {
            result.status  = SignatureStatus::InvalidMemoryRegion;
            result.details = "Match address is outside module code boundaries or lacks execute permission";
            return result;
        }

        result.status  = SignatureStatus::Verified;
        result.details = "Unique signature verified in executable module section";
        return result;
    }
};

} // namespace aeris

#include "metrics/process.h"

const char *wt_integrity_level_name(WT_IntegrityLevel level)
{
    switch (level) {
    case WT_INTEGRITY_UNTRUSTED:    return "untrusted";
    case WT_INTEGRITY_LOW:          return "low";
    case WT_INTEGRITY_MEDIUM:       return "medium";
    case WT_INTEGRITY_MEDIUM_PLUS:  return "medium_plus";
    case WT_INTEGRITY_HIGH:         return "high";
    case WT_INTEGRITY_SYSTEM:       return "system";
    case WT_INTEGRITY_PROTECTED:    return "protected";
    case WT_INTEGRITY_UNKNOWN:
    default:                        return "unknown";
    }
}

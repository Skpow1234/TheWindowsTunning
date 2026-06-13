#include "cli/exit_codes.h"

int wt_exit_code_from_result(WT_Result result)
{
    switch (result) {
    case WT_OK:
        return WT_EXIT_OK;
    case WT_ERR_INVALID_ARGUMENT:
        return WT_EXIT_USAGE;
    case WT_ERR_CANCELLED:
        return WT_EXIT_CANCELLED;
    case WT_ERR_ACCESS_DENIED:
        return WT_EXIT_ACCESS_DENIED;
    case WT_ERR_NOT_FOUND:
        return WT_EXIT_NOT_FOUND;
    case WT_ERR_NOT_SUPPORTED:
        return WT_EXIT_NOT_SUPPORTED;
    case WT_ERR_TIMEOUT:
        return WT_EXIT_TIMEOUT;
    case WT_ERR_UNKNOWN:
    case WT_ERR_OUT_OF_MEMORY:
    case WT_ERR_WIN32:
    case WT_ERR_PDH:
    case WT_ERR_BUFFER_TOO_SMALL:
    default:
        return WT_EXIT_ERROR;
    }
}

const char *wt_result_code_name(WT_Result result)
{
    switch (result) {
    case WT_OK:                   return "ok";
    case WT_ERR_INVALID_ARGUMENT: return "invalid_argument";
    case WT_ERR_OUT_OF_MEMORY:    return "out_of_memory";
    case WT_ERR_WIN32:            return "win32_error";
    case WT_ERR_PDH:              return "pdh_error";
    case WT_ERR_ACCESS_DENIED:    return "access_denied";
    case WT_ERR_NOT_SUPPORTED:    return "not_supported";
    case WT_ERR_NOT_FOUND:        return "not_found";
    case WT_ERR_BUFFER_TOO_SMALL: return "buffer_too_small";
    case WT_ERR_TIMEOUT:          return "timeout";
    case WT_ERR_CANCELLED:        return "cancelled";
    case WT_ERR_UNKNOWN:
    default:                      return "unknown_error";
    }
}

const char *wt_exit_code_name(int exit_code)
{
    switch (exit_code) {
    case WT_EXIT_OK:               return "ok";
    case WT_EXIT_USAGE:            return "usage";
    case WT_EXIT_CANCELLED:        return "cancelled";
    case WT_EXIT_ACCESS_DENIED:    return "access_denied";
    case WT_EXIT_NOT_FOUND:        return "not_found";
    case WT_EXIT_NOT_SUPPORTED:    return "not_supported";
    case WT_EXIT_TIMEOUT:          return "timeout";
    case WT_EXIT_ERROR:            return "error";
    case WT_EXIT_NOT_IMPLEMENTED:  return "not_implemented";
    default:                       return "error";
    }
}

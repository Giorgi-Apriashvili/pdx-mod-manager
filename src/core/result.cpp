#include "core/result.hpp"

namespace pmm {

std::string_view errorCodeName(ErrorCode code) noexcept {
    switch (code) {
    case ErrorCode::InvalidArgument:
        return "invalid_argument";
    case ErrorCode::NotFound:
        return "not_found";
    case ErrorCode::AlreadyExists:
        return "already_exists";
    case ErrorCode::IoError:
        return "io_error";
    case ErrorCode::PermissionDenied:
        return "permission_denied";
    case ErrorCode::ParseError:
        return "parse_error";
    case ErrorCode::ValidationFailed:
        return "validation_failed";
    case ErrorCode::UnsupportedVersion:
        return "unsupported_version";
    case ErrorCode::LimitExceeded:
        return "limit_exceeded";
    case ErrorCode::IntegrityCheckFailed:
        return "integrity_check_failed";
    case ErrorCode::WrongGame:
        return "wrong_game";
    case ErrorCode::UnsupportedGame:
        return "unsupported_game";
    case ErrorCode::DatabaseError:
        return "database_error";
    case ErrorCode::MigrationFailed:
        return "migration_failed";
    case ErrorCode::ProviderUnavailable:
        return "provider_unavailable";
    case ErrorCode::Offline:
        return "offline";
    case ErrorCode::ItemUnavailable:
        return "item_unavailable";
    case ErrorCode::AccessDenied:
        return "access_denied";
    case ErrorCode::Busy:
        return "busy";
    case ErrorCode::Timeout:
        return "timeout";
    case ErrorCode::Cancelled:
        return "cancelled";
    case ErrorCode::ProcessStartFailed:
        return "process_start_failed";
    case ErrorCode::ConflictingProcess:
        return "conflicting_process";
    case ErrorCode::Interrupted:
        return "interrupted";
    case ErrorCode::NotImplemented:
        return "not_implemented";
    case ErrorCode::Internal:
        return "internal";
    }
    return "internal";
}

} // namespace pmm

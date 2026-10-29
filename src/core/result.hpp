#pragma once

// Structured error handling shared by every layer. Expected failures (bad input,
// missing files, provider errors) are reported through Result<T>; exceptions are
// reserved for programming errors and are not thrown across module boundaries.

#include <cassert>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace pmm {

enum class ErrorCode {
    InvalidArgument,
    NotFound,
    AlreadyExists,
    IoError,
    PermissionDenied,
    ParseError,
    ValidationFailed,
    UnsupportedVersion,
    LimitExceeded,
    IntegrityCheckFailed,
    WrongGame,
    UnsupportedGame,
    DatabaseError,
    MigrationFailed,
    ProviderUnavailable,
    Offline,
    ItemUnavailable,
    AccessDenied,
    Busy,
    Timeout,
    Cancelled,
    ProcessStartFailed,
    ConflictingProcess,
    Interrupted,
    NotImplemented,
    Internal,
};

// Stable, machine-readable identifier such as "validation_failed".
std::string_view errorCodeName(ErrorCode code) noexcept;

struct Error {
    ErrorCode code = ErrorCode::Internal;
    // Actionable, user-facing explanation. Must not contain secrets.
    std::string message;
    // Optional technical context (file paths, OS error text). May contain
    // personal paths: pass through redact() before exporting diagnostics.
    std::string detail;

    Error() = default;
    Error(ErrorCode c, std::string msg, std::string det = {})
        : code(c), message(std::move(msg)), detail(std::move(det)) {}
};

template <class T> class [[nodiscard]] Result {
public:
    Result(T value) : m_data(std::in_place_index<0>, std::move(value)) {}
    Result(Error error) : m_data(std::in_place_index<1>, std::move(error)) {}

    bool ok() const noexcept { return m_data.index() == 0; }
    explicit operator bool() const noexcept { return ok(); }

    T& value() & {
        assert(ok());
        return std::get<0>(m_data);
    }
    const T& value() const& {
        assert(ok());
        return std::get<0>(m_data);
    }
    T&& value() && {
        assert(ok());
        return std::get<0>(std::move(m_data));
    }
    T* operator->() { return &value(); }
    const T* operator->() const { return &value(); }
    T& operator*() & { return value(); }
    const T& operator*() const& { return value(); }

    const Error& error() const& {
        assert(!ok());
        return std::get<1>(m_data);
    }
    Error&& error() && {
        assert(!ok());
        return std::get<1>(std::move(m_data));
    }

    template <class U> T valueOr(U&& fallback) const& {
        return ok() ? value() : static_cast<T>(std::forward<U>(fallback));
    }

private:
    std::variant<T, Error> m_data;
};

template <> class [[nodiscard]] Result<void> {
public:
    Result() = default;
    Result(Error error) : m_error(std::move(error)) {}

    bool ok() const noexcept { return !m_error.has_value(); }
    explicit operator bool() const noexcept { return ok(); }

    const Error& error() const& {
        assert(!ok());
        return *m_error;
    }
    Error&& error() && {
        assert(!ok());
        return std::move(*m_error);
    }

private:
    std::optional<Error> m_error;
};

using Status = Result<void>;

inline Error makeError(ErrorCode code, std::string message, std::string detail = {}) {
    return Error{code, std::move(message), std::move(detail)};
}

} // namespace pmm

// Propagate an error from an expression returning Result<...>. Every expansion
// names its variable uniquely, so a PMM_TRY inside a lambda passed to another
// PMM_TRY does not shadow the outer one (GCC -Wshadow). Clang >= 22 reports
// __COUNTER__ as an extension under -Wpedantic and does not warn about this
// shadowing, so it uses the line number instead.
#if defined(__clang__)
#define PMM_DETAIL_TRY_ID __LINE__
#else
#define PMM_DETAIL_TRY_ID __COUNTER__
#endif
#define PMM_DETAIL_CONCAT_IMPL(a, b) a##b
#define PMM_DETAIL_CONCAT(a, b) PMM_DETAIL_CONCAT_IMPL(a, b)
#define PMM_DETAIL_TRY(expr, var)                                                                  \
    do {                                                                                           \
        auto&& var = (expr);                                                                       \
        if (!var.ok())                                                                             \
            return std::move(var).error();                                                         \
    } while (false)
#define PMM_TRY(expr) PMM_DETAIL_TRY(expr, PMM_DETAIL_CONCAT(pmm_try_result_, PMM_DETAIL_TRY_ID))

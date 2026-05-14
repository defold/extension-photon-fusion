// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

#include "Error.h"
#include "StringType.h"

#include <functional>
#include <optional>
#include "PhotonAssert.h"
#include <type_traits>
#include <utility>
#include <variant>

namespace PhotonCommon {
    template<typename T = void, typename CodeT = ErrorCode>
    class Result;

    namespace detail {
        template<typename T>
        struct is_result : std::false_type {};

        template<typename T, typename CodeT>
        struct is_result<Result<T, CodeT>> : std::true_type {};

        template<typename T>
        inline constexpr bool is_result_v = is_result<T>::value;
    } // namespace detail

    template<typename T, typename CodeT>
    class Result {
    public:
        using ValueType = T;
        using CodeType = CodeT;

        static Result Ok(T value) {
            return Result(std::move(value));
        }

        static Result Err(CodeT code, PhotonCommon::StringType message = {}) {
            return Result(Error<CodeT>{code, std::move(message)});
        }

        static Result Err(Error<CodeT> error) {
            return Result(std::move(error));
        }

        bool IsOk() const noexcept { return data.index() == 0; }

        bool IsErr() const noexcept { return data.index() == 1; }

        explicit operator bool() const noexcept { return IsOk(); }

        const T& GetValue() const& {
            PHOTON_ASSERT(!IsErr(), "Result::GetValue() called on error");
            return std::get<0>(data);
        }

        T& GetValue() & {
            PHOTON_ASSERT(!IsErr(), "Result::GetValue() called on error");
            return std::get<0>(data);
        }

        T&& GetValue() && {
            PHOTON_ASSERT(!IsErr(), "Result::GetValue() called on error");
            return std::move(std::get<0>(data));
        }

        const T* operator->() const {
            return &GetValue();
        }

        const Error<CodeT>& GetError() const& {
            PHOTON_ASSERT(!IsOk(), "Result::GetError() called on ok value");
            return std::get<1>(data);
        }

        Error<CodeT>&& GetError() && {
            PHOTON_ASSERT(!IsOk(), "Result::GetError() called on ok value");
            return std::move(std::get<1>(data));
        }

        CodeT GetErrorCode() const {
            return IsErr() ? std::get<1>(data).code : CodeT::Ok;
        }

        template<typename F>
        auto Transform(F&& f) const& -> Result<std::invoke_result_t<F, const T&>, CodeT> {
            using U = std::invoke_result_t<F, const T&>;
            if (IsErr()) return Result<U, CodeT>::Err(GetError());
            if constexpr (std::is_void_v<U>) {
                std::invoke(std::forward<F>(f), GetValue());
                return Result<U, CodeT>::Ok();
            } else {
                return Result<U, CodeT>::Ok(std::invoke(std::forward<F>(f), GetValue()));
            }
        }

        template<typename F>
        auto Transform(F&& f) & -> Result<std::invoke_result_t<F, T&>, CodeT> {
            using U = std::invoke_result_t<F, T&>;
            if (IsErr()) return Result<U, CodeT>::Err(GetError());
            if constexpr (std::is_void_v<U>) {
                std::invoke(std::forward<F>(f), GetValue());
                return Result<U, CodeT>::Ok();
            } else {
                return Result<U, CodeT>::Ok(std::invoke(std::forward<F>(f), GetValue()));
            }
        }

        template<typename F>
        auto Transform(F&& f) && -> Result<std::invoke_result_t<F, T&&>, CodeT> {
            using U = std::invoke_result_t<F, T&&>;
            if (IsErr()) return Result<U, CodeT>::Err(std::move(*this).GetError());
            if constexpr (std::is_void_v<U>) {
                std::invoke(std::forward<F>(f), std::move(*this).GetValue());
                return Result<U, CodeT>::Ok();
            } else {
                return Result<U, CodeT>::Ok(std::invoke(std::forward<F>(f), std::move(*this).GetValue()));
            }
        }

        template<typename F>
        auto AndThen(F&& f) const& -> std::invoke_result_t<F, const T&> {
            using RetType = std::invoke_result_t<F, const T&>;
            static_assert(detail::is_result_v<RetType>,
                "AndThen: callable must return a Result<U>");
            if (IsOk()) return std::invoke(std::forward<F>(f), GetValue());
            return RetType::Err(GetError());
        }

        template<typename F>
        auto AndThen(F&& f) & -> std::invoke_result_t<F, T&> {
            using RetType = std::invoke_result_t<F, T&>;
            static_assert(detail::is_result_v<RetType>,
                "AndThen: callable must return a Result<U>");
            if (IsOk()) return std::invoke(std::forward<F>(f), GetValue());
            return RetType::Err(GetError());
        }

        template<typename F>
        auto AndThen(F&& f) && -> std::invoke_result_t<F, T&&> {
            using RetType = std::invoke_result_t<F, T&&>;
            static_assert(detail::is_result_v<RetType>,
                "AndThen: callable must return a Result<U>");
            if (IsOk()) return std::invoke(std::forward<F>(f), std::move(*this).GetValue());
            return RetType::Err(std::move(*this).GetError());
        }

        template<typename F>
        auto OrElse(F&& f) const& -> std::invoke_result_t<F, const Error<CodeT>&> {
            using RetType = std::invoke_result_t<F, const Error<CodeT>&>;
            static_assert(std::is_same_v<RetType, Result>,
                "OrElse: callable must return Result<T> (same T)");
            if (IsOk()) return Result::Ok(GetValue());
            return std::invoke(std::forward<F>(f), GetError());
        }

        template<typename F>
        auto OrElse(F&& f) & -> std::invoke_result_t<F, const Error<CodeT>&> {
            using RetType = std::invoke_result_t<F, const Error<CodeT>&>;
            static_assert(std::is_same_v<RetType, Result>,
                "OrElse: callable must return Result<T> (same T)");
            if (IsOk()) return Result::Ok(GetValue());
            return std::invoke(std::forward<F>(f), GetError());
        }

        template<typename F>
        auto OrElse(F&& f) && -> std::invoke_result_t<F, Error<CodeT>&&> {
            using RetType = std::invoke_result_t<F, Error<CodeT>&&>;
            static_assert(std::is_same_v<RetType, Result>,
                "OrElse: callable must return Result<T> (same T)");
            if (IsOk()) return Result::Ok(std::move(*this).GetValue());
            return std::invoke(std::forward<F>(f), std::move(*this).GetError());
        }

        template<typename F>
        auto TransformError(F&& f) const& -> Result {
            static_assert(std::is_same_v<std::invoke_result_t<F, const Error<CodeT>&>, Error<CodeT>>,
                "TransformError: callable must return Error");
            if (IsOk()) return Result::Ok(GetValue());
            return Result::Err(std::invoke(std::forward<F>(f), GetError()));
        }

        template<typename F>
        auto TransformError(F&& f) & -> Result {
            static_assert(std::is_same_v<std::invoke_result_t<F, const Error<CodeT>&>, Error<CodeT>>,
                "TransformError: callable must return Error");
            if (IsOk()) return Result::Ok(GetValue());
            return Result::Err(std::invoke(std::forward<F>(f), GetError()));
        }

        template<typename F>
        auto TransformError(F&& f) && -> Result {
            static_assert(std::is_same_v<std::invoke_result_t<F, Error<CodeT>&&>, Error<CodeT>>,
                "TransformError: callable must return Error");
            if (IsOk()) return Result::Ok(std::move(*this).GetValue());
            return Result::Err(std::invoke(std::forward<F>(f), std::move(*this).GetError()));
        }

        T ValueOr(T default_value) const& {
            if (IsOk()) return GetValue();
            return std::move(default_value);
        }

        T ValueOr(T default_value) && {
            if (IsOk()) return std::move(*this).GetValue();
            return std::move(default_value);
        }

    private:
        explicit Result(T value) : data(std::in_place_index<0>, std::move(value)) {}
        explicit Result(Error<CodeT> error) : data(std::in_place_index<1>, std::move(error)) {}

        std::variant<T, Error<CodeT>> data;
    };

    template<typename CodeT>
    class Result<void, CodeT> {
    public:
        using ValueType = void;
        using CodeType = CodeT;

        static Result Ok() { return Result(); }

        static Result Err(CodeT code, PhotonCommon::StringType message = {}) {
            return Result(Error<CodeT>{code, std::move(message)});
        }

        static Result Err(Error<CodeT> error) {
            return Result(std::move(error));
        }

        bool IsOk() const noexcept { return !errorOpt.has_value(); }
        bool IsErr() const noexcept { return errorOpt.has_value(); }
        explicit operator bool() const noexcept { return IsOk(); }

        const Error<CodeT>& GetError() const& {
            PHOTON_ASSERT(!IsOk(), "Result::GetError() called on ok value");
            return *errorOpt;
        }

        Error<CodeT>&& GetError() && {
            PHOTON_ASSERT(!IsOk(), "Result::GetError() called on ok value");
            return std::move(*errorOpt);
        }

        CodeT GetErrorCode() const {
            return IsErr() ? errorOpt->code : CodeT::Ok;
        }

        template<typename F>
        auto Transform(F&& f) const& -> Result<std::invoke_result_t<F>, CodeT> {
            using U = std::invoke_result_t<F>;
            if (IsErr()) return Result<U, CodeT>::Err(GetError());
            if constexpr (std::is_void_v<U>) {
                std::invoke(std::forward<F>(f));
                return Result<U, CodeT>::Ok();
            } else {
                return Result<U, CodeT>::Ok(std::invoke(std::forward<F>(f)));
            }
        }

        template<typename F>
        auto Transform(F&& f) && -> Result<std::invoke_result_t<F>, CodeT> {
            using U = std::invoke_result_t<F>;
            if (IsErr()) return Result<U, CodeT>::Err(std::move(*this).GetError());
            if constexpr (std::is_void_v<U>) {
                std::invoke(std::forward<F>(f));
                return Result<U, CodeT>::Ok();
            } else {
                return Result<U, CodeT>::Ok(std::invoke(std::forward<F>(f)));
            }
        }

        template<typename F>
        auto AndThen(F&& f) const& -> std::invoke_result_t<F> {
            using RetType = std::invoke_result_t<F>;
            static_assert(detail::is_result_v<RetType>,
                "AndThen: callable must return a Result<U>");
            if (IsOk()) return std::invoke(std::forward<F>(f));
            return RetType::Err(GetError());
        }

        template<typename F>
        auto AndThen(F&& f) && -> std::invoke_result_t<F> {
            using RetType = std::invoke_result_t<F>;
            static_assert(detail::is_result_v<RetType>,
                "AndThen: callable must return a Result<U>");
            if (IsOk()) return std::invoke(std::forward<F>(f));
            return RetType::Err(std::move(*this).GetError());
        }

        template<typename F>
        auto OrElse(F&& f) const& -> std::invoke_result_t<F, const Error<CodeT>&> {
            using RetType = std::invoke_result_t<F, const Error<CodeT>&>;
            static_assert(std::is_same_v<RetType, Result>,
                "OrElse: callable must return Result<void>");
            if (IsOk()) return Result::Ok();
            return std::invoke(std::forward<F>(f), GetError());
        }

        template<typename F>
        auto OrElse(F&& f) && -> std::invoke_result_t<F, Error<CodeT>&&> {
            using RetType = std::invoke_result_t<F, Error<CodeT>&&>;
            static_assert(std::is_same_v<RetType, Result>,
                "OrElse: callable must return Result<void>");
            if (IsOk()) return Result::Ok();
            return std::invoke(std::forward<F>(f), std::move(*this).GetError());
        }

        template<typename F>
        auto TransformError(F&& f) const& -> Result {
            static_assert(std::is_same_v<std::invoke_result_t<F, const Error<CodeT>&>, Error<CodeT>>,
                "TransformError: callable must return Error");
            if (IsOk()) return Result::Ok();
            return Result::Err(std::invoke(std::forward<F>(f), GetError()));
        }

        template<typename F>
        auto TransformError(F&& f) && -> Result {
            static_assert(std::is_same_v<std::invoke_result_t<F, Error<CodeT>&&>, Error<CodeT>>,
                "TransformError: callable must return Error");
            if (IsOk()) return Result::Ok();
            return Result::Err(std::invoke(std::forward<F>(f), std::move(*this).GetError()));
        }

    private:
        Result() = default;
        explicit Result(Error<CodeT> error) : errorOpt(std::move(error)) {}

        std::optional<Error<CodeT>> errorOpt;
    };
} // namespace PhotonCommon

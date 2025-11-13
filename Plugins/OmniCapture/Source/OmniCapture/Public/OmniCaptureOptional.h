#pragma once

#if defined(__has_include)
#if __has_include("Misc/Optional.h")
#include "Misc/Optional.h"
#define OMNICAPTURE_HAS_TOPTIONAL 1
#elif __has_include("Templates/Optional.h")
#include "Templates/Optional.h"
#define OMNICAPTURE_HAS_TOPTIONAL 1
#elif __has_include("Containers/Optional.h")
#include "Containers/Optional.h"
#define OMNICAPTURE_HAS_TOPTIONAL 1
#endif
#endif

#ifndef OMNICAPTURE_HAS_TOPTIONAL
#include <optional>

/**
 * Minimal fallback implementation of Unreal's TOptional backed by std::optional.
 * This keeps the plugin compatible with engine versions where the engine version of
 * TOptional is not accessible from public headers.
 */
template <typename ValueType>
class TOptional : public std::optional<ValueType>
{
public:
    using std::optional<ValueType>::optional;

    bool IsSet() const
    {
        return this->has_value();
    }

    void Reset()
    {
        this->reset();
    }

    ValueType& GetValue()
    {
        return this->value();
    }

    const ValueType& GetValue() const
    {
        return this->value();
    }
};
#endif

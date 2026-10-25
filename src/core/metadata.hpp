#pragma once

#include <functional>
#include <string_view>
#include <tuple>
#include <utility>

namespace strefa::meta {

template<class Owner, class Value>
struct Field {
    std::string_view key;
    std::string_view label;
    Value Owner::* member;
    Value minimum{};
    Value maximum{};
};

// The same typed schema drives persistence, validation and settings widgets.
template<class Owner, class Schema, class Visitor>
constexpr void for_each_field(Owner& object, const Schema& schema, Visitor&& visitor) {
    std::apply([&](const auto&... field) {
        (std::invoke(visitor, field, object.*(field.member)), ...);
    }, schema);
}

} // namespace strefa::meta

// ============================================================================
//  poorijson.hpp — C++23 JSON Foundation
//  Developed by: Pooria Yousefi
//  License: Apache 2.0
//
//  DESCRIPTION
//  -----------
//  A self-contained, header-only JSON library for C++23. It provides:
//
//    * JSON            — a std::variant-backed value type covering all six
//                         JSON types (null, bool, number, string, array,
//                         object) with an STL-flavoured API.
//    * JSONParser      — a strict, single-pass recursive-descent parser that
//                         reports failures through std::expected<JSON,
//                         JSONError> instead of exceptions.
//    * parse_lenient() — tolerant parsing that strips Markdown code fences
//                         and leading prose, and repairs unclosed containers
//                         (useful for LLM-generated output).
//
//  Comments follow Doxygen conventions: `///` documentation blocks,
//  `///<` trailing member comments, and the standard @-tags.
// ============================================================================

#pragma once

// ---- Standard library dependencies ----------------------------------------
#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <concepts>
#include <cstdint>
#include <expected>
#include <format>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <ranges>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <variant>
#include <vector>

/// @brief Root namespace of the library.
namespace pooriayousefi::json
{
    /// @brief Every error condition this library can report.
    ///
    /// Fallible operations return std::expected<T, JSONError> rather than
    /// throwing. A few enumerators are not currently produced by the library
    /// itself and are marked "(reserved)" for callers or future use.
    enum class JSONError
    {
        NONE,                            ///< Success — no error occurred.
        INVALID_SYNTAX,                  ///< Structurally malformed JSON (stray token, missing separator, ...).
        UNEXPECTED_END,                  ///< Input ended in the middle of a value.
        INVALID_NUMBER,                  ///< Numeric literal violates the JSON grammar.
        INVALID_STRING_ESCAPE,           ///< Unknown character after a backslash inside a string.
        INVALID_UNICODE,                 ///< Malformed \uXXXX escape or unpaired UTF-16 surrogate.
        INVALID_LITERAL,                 ///< "true" / "false" / "null" misspelled or truncated.
        TYPE_MISMATCH,                   ///< Checked access (as_bool / as_number / as_string) used on the wrong type.
        KEY_NOT_FOUND,                   ///< (Reserved) requested object key does not exist.
        INDEX_OUT_OF_RANGE,              ///< (Reserved) requested array index is out of bounds.
        NOT_AN_OBJECT,                   ///< (Reserved) operation requires an object value.
        NOT_AN_ARRAY,                    ///< (Reserved) operation requires an array value.
        PARSE_ERROR,                     ///< (Reserved) generic parse failure.
        UNESCAPED_CONTROL_CHARACTER      ///< Raw control byte (below 0x20) inside a string literal.
    };

    class JSON; // Forward declaration — the container aliases below are recursive.

    /// A JSON text value, stored unescaped as UTF-8.
    using JSONString = std::string;

    /// A JSON number. All numbers are IEEE-754 doubles (like JavaScript),
    /// so integers beyond 2^53 lose precision.
    using JSONNumber = double;

    /// A JSON boolean value (true / false).
    using JSONBool = bool;

    /// A JSON array: an ordered, index-addressable sequence of values.
    using JSONArray = std::vector<JSON>;

    /// @brief Transparent hasher for JSONString keys.
    ///
    /// Allows std::unordered_map lookups with std::string_view without
    /// allocating a temporary std::string, significantly improving
    /// performance for high-frequency object member access.
    struct JSONStringHash
    {
        using is_transparent = void;

        [[nodiscard]] std::size_t operator()(std::string_view sv) const noexcept
        {
            return std::hash<std::string_view>{}(sv);
        }
    };

    /// @brief Transparent equality comparator for JSONString keys.
    ///
    /// Works in tandem with JSONStringHash to enable zero-allocation
    /// lookups by std::string_view.
    struct JSONStringEqual
    {
        using is_transparent = void;

        [[nodiscard]] bool operator()(std::string_view a, std::string_view b) const noexcept
        {
            return a == b;
        }
    };

    /// A JSON object: an unordered map from string keys to values.
    /// @note Iteration order of the entries is unspecified.
    ///       Uses transparent hash/eq for zero-alloc string_view lookups.
    using JSONObject = std::unordered_map<JSONString, JSON, JSONStringHash, JSONStringEqual>;

    /// @brief A single JSON value — one of null, boolean, number, string,
    ///        array or object, held in a std::variant.
    ///
    /// The type has full value semantics: copies deep-copy the whole value
    /// tree, moves are cheap. Values are typically created by parsing (see
    /// JSONParser, parse, parse_lenient), by brace initialisation, or by
    /// growing a structure through operator[] / push_back, which
    /// "auto-vivify" null values into objects/arrays as needed.
    ///
    /// Example:
    /// @code
    ///     JSON j;                        // null
    ///     j["user"]["name"] = "Ada";     // becomes {"user":{"name":"Ada"}}
    ///     j["scores"].push_back(99);     // adds an array under "scores"
    ///     std::string text = j.dump(4);  // pretty-printed JSON text
    /// @endcode
    class JSON
    {
    public:
        /// @brief The JSON data type a JSON instance currently holds.
        /// @note The enumerator order intentionally matches the alternative
        ///       order of StorageType, so the variant's index can be cast
        ///       directly to a Type (see get_type()).
        enum class Type
        {
            NULL_TYPE, ///< The "null" literal; also the default-constructed state.
            BOOLEAN,   ///< The "true" / "false" literals.
            NUMBER,    ///< A numeric literal, stored as a double.
            STRING,    ///< A double-quoted text literal (UTF-8, unescaped internally).
            ARRAY,     ///< An ordered list: [ v0, v1, ... ]
            OBJECT     ///< An unordered set of key/value pairs: { "key": v, ... }
        };

    private:
        /// Type-erased storage for the held value. The alternative index is
        /// numerically equal to the corresponding Type enumerator:
        /// 0 = monostate (null), 1 = bool, 2 = double, 3 = string,
        /// 4 = array, 5 = object.
        using StorageType = std::variant<std::monostate, JSONBool, JSONNumber, JSONString, JSONArray, JSONObject>;

        // ---- Compile-time invariant: Type indices match StorageType ----
        static_assert(static_cast<std::size_t>(Type::NULL_TYPE) == 0, "Type::NULL_TYPE must match StorageType index 0");
        static_assert(static_cast<std::size_t>(Type::BOOLEAN) == 1, "Type::BOOLEAN must match StorageType index 1");
        static_assert(static_cast<std::size_t>(Type::NUMBER) == 2, "Type::NUMBER must match StorageType index 2");
        static_assert(static_cast<std::size_t>(Type::STRING) == 3, "Type::STRING must match StorageType index 3");
        static_assert(static_cast<std::size_t>(Type::ARRAY) == 4, "Type::ARRAY must match StorageType index 4");
        static_assert(static_cast<std::size_t>(Type::OBJECT) == 5, "Type::OBJECT must match StorageType index 5");

        StorageType data_; ///< The wrapped value; default-constructed to null.

    public:
        // --------------------------------------------------------------------
        //  Constructors
        // --------------------------------------------------------------------

        /// @brief Default constructor — creates a null value.
        constexpr JSON() noexcept = default;

        /// @brief Constructs a null value (enables `JSON j = nullptr;`).
        constexpr JSON(std::nullptr_t) noexcept : data_{} {}

        /// @brief Constructs a boolean value (enables `JSON j = true;`).
        constexpr JSON(bool value) noexcept : data_{std::in_place_index<1>, value} {}

        /// @brief Constructs a number from any floating-point type.
        template <std::floating_point T>
        constexpr JSON(T value) noexcept : data_{std::in_place_index<2>, static_cast<JSONNumber>(value)} {}

        /// @brief Constructs a number from any integral type
        ///        (narrowed to double, like all JSON numbers here).
        template <std::integral T>
        constexpr JSON(T value) noexcept : data_{std::in_place_index<2>, static_cast<JSONNumber>(value)} {}

        /// @brief Constructs a string value by copying @p value.
        constexpr JSON(const JSONString& value) : data_{std::in_place_index<3>, value} {}

        /// @brief Constructs a string value by moving @p value in (no copy).
        constexpr JSON(JSONString&& value) noexcept : data_{std::in_place_index<3>, std::move(value)} {}

        /// @brief Constructs a string value from a NUL-terminated C string.
        /// @note If @p value is null, an empty string is stored (no UB).
        constexpr JSON(const char* value) : data_{std::in_place_index<3>, value ? JSONString(value) : JSONString()} {}

        /// @brief Constructs a string value from a std::string_view.
        constexpr JSON(std::string_view value) : data_{std::in_place_index<3>, JSONString(value)} {}

        /// @brief Constructs an array value by moving @p array in.
        constexpr JSON(JSONArray array) noexcept : data_{std::in_place_index<4>, std::move(array)} {}

        /// @brief Constructs an object value by moving @p object in.
        JSON(JSONObject object) noexcept : data_{std::in_place_index<5>, std::move(object)} {}

        /// @brief Constructs a value from a braced list, inferring the
        ///        container kind from the shape of the list.
        ///
        /// If every element is itself a two-element array whose first
        /// element is a string, the list is interpreted as key/value pairs
        /// and an object is built; otherwise a plain array is built. An
        /// empty braced list always yields an empty array (there is no way
        /// to spell an empty object this way).
        ///
        /// Example:
        /// @code
        ///     JSON o = { {"a", 1}, {"b", 2} };  // object: {"a":1,"b":2}
        ///     JSON a = { 1, 2, 3 };             // array:  [1,2,3]
        /// @endcode
        JSON(std::initializer_list<JSON> init)
        {
            bool is_object_init = true;

            // Detect the { {"key", value}, ... } pattern.
            for (const auto& val : init)
            {
                if (!val.is_array() || val.size() != 2 || !val[0].is_string())
                {
                    is_object_init = false;
                    break;
                }
            }

            if (is_object_init && init.size() > 0)
            {
                // Interpret each element as a {"key", value} pair.
                data_.emplace<JSONObject>();
                auto& obj = std::get<JSONObject>(data_);
                for (const auto& val : init)
                {
                    obj[val[0].get_string()] = val[1];
                }
            }
            else
            {
                // Not key/value shaped — build a plain array.
                data_.emplace<JSONArray>(init);
            }
        }

        /// @brief Copy constructor — deep-copies the whole value tree.
        JSON(const JSON&) = default;

        /// @brief Move constructor — cheap; the source is left in a valid
        ///        but unspecified state.
        JSON(JSON&&) noexcept = default;

        /// @brief Copy assignment — deep-copies the whole value tree.
        JSON& operator=(const JSON&) = default;

        /// @brief Move assignment — cheap, never throws.
        JSON& operator=(JSON&&) noexcept = default;

        // --------------------------------------------------------------------
        //  Type inspection
        // --------------------------------------------------------------------

        /// @brief Returns the JSON type currently held.
        /// @note O(1) — a direct cast of the variant's alternative index.
        [[nodiscard]] constexpr Type get_type() const noexcept
        {
            return static_cast<Type>(data_.index());
        }

        /// @brief True if the value is null.
        [[nodiscard]] constexpr bool is_null() const noexcept
        {
            return get_type() == Type::NULL_TYPE;
        }

        /// @brief True if the value is a boolean.
        [[nodiscard]] constexpr bool is_boolean() const noexcept
        {
            return get_type() == Type::BOOLEAN;
        }

        /// @brief True if the value is a number.
        [[nodiscard]] constexpr bool is_number() const noexcept
        {
            return get_type() == Type::NUMBER;
        }

        /// @brief True if the value is a string.
        [[nodiscard]] constexpr bool is_string() const noexcept
        {
            return get_type() == Type::STRING;
        }

        /// @brief True if the value is an array.
        [[nodiscard]] constexpr bool is_array() const noexcept
        {
            return get_type() == Type::ARRAY;
        }

        /// @brief True if the value is an object.
        [[nodiscard]] constexpr bool is_object() const noexcept
        {
            return get_type() == Type::OBJECT;
        }

        // --------------------------------------------------------------------
        //  Value access
        // --------------------------------------------------------------------

        /// @brief Checked access to the boolean value.
        /// @return the boolean, or JSONError::TYPE_MISMATCH if the value is not a boolean.
        [[nodiscard]] std::expected<JSONBool, JSONError> as_bool() const noexcept
        {
            std::expected<JSONBool, JSONError> result;
            if (is_boolean())
            {
                result = std::get<JSONBool>(data_);
            }
            else
            {
                result = std::unexpected(JSONError::TYPE_MISMATCH);
            }
            return result;
        }

        /// @brief Checked access to the numeric value.
        /// @return the number, or JSONError::TYPE_MISMATCH if the value is not a number.
        [[nodiscard]] std::expected<JSONNumber, JSONError> as_number() const noexcept
        {
            std::expected<JSONNumber, JSONError> result;
            if (is_number())
            {
                result = std::get<JSONNumber>(data_);
            }
            else
            {
                result = std::unexpected(JSONError::TYPE_MISMATCH);
            }
            return result;
        }

        /// @brief Checked access to the string value (returned by value).
        /// @return the string, or JSONError::TYPE_MISMATCH if the value is not a string.
        [[nodiscard]] std::expected<JSONString, JSONError> as_string() const
        {
            std::expected<JSONString, JSONError> result;
            if (is_string())
            {
                result = std::get<JSONString>(data_);
            }
            else
            {
                result = std::unexpected(JSONError::TYPE_MISMATCH);
            }
            return result;
        }

        /// @brief Unchecked boolean access.
        /// @throws std::bad_expected_access if the value is not a boolean.
        [[nodiscard]] JSONBool get_bool() const
        {
            return as_bool().value();
        }

        /// @brief Unchecked numeric access.
        /// @throws std::bad_expected_access if the value is not a number.
        [[nodiscard]] JSONNumber get_number() const
        {
            return as_number().value();
        }

        /// @brief Unchecked string access (returns a copy).
        /// @throws std::bad_expected_access if the value is not a string.
        [[nodiscard]] JSONString get_string() const
        {
            return as_string().value();
        }

        /// @brief Unchecked read-only access to the underlying array.
        /// @throws std::bad_variant_access if the value is not an array.
        [[nodiscard]] const JSONArray& get_array() const
        {
            return std::get<JSONArray>(data_);
        }

        /// @brief Unchecked mutable access to the underlying array.
        /// @throws std::bad_variant_access if the value is not an array.
        [[nodiscard]] JSONArray& get_array()
        {
            return std::get<JSONArray>(data_);
        }

        /// @brief Unchecked read-only access to the underlying object.
        /// @throws std::bad_variant_access if the value is not an object.
        [[nodiscard]] const JSONObject& get_object() const
        {
            return std::get<JSONObject>(data_);
        }

        /// @brief Unchecked mutable access to the underlying object.
        /// @throws std::bad_variant_access if the value is not an object.
        [[nodiscard]] JSONObject& get_object()
        {
            return std::get<JSONObject>(data_);
        }

        // --------------------------------------------------------------------
        //  Checked element access (at)
        // --------------------------------------------------------------------

        /// @brief Checked object member access — const.
        /// @param key  member name.
        /// @return a reference wrapper to the member, or a JSONError
        ///         (NOT_AN_OBJECT / KEY_NOT_FOUND) if access fails.
        [[nodiscard]] std::expected<std::reference_wrapper<const JSON>, JSONError> at(std::string_view key) const
        {
            std::expected<std::reference_wrapper<const JSON>, JSONError> result{std::unexpected(JSONError::NOT_AN_OBJECT)};
            if (is_object())
            {
                const auto& obj = std::get<JSONObject>(data_);
                auto it = obj.find(key); // transparent find — no allocation.
                if (it == obj.end())
                {
                    result = std::unexpected(JSONError::KEY_NOT_FOUND);
                }
                else
                {
                    result = std::cref(it->second);
                }
            }
            return result;
        }

        /// @brief Checked object member access — mutable.
        /// @param key  member name.
        /// @return a reference wrapper to the member, or a JSONError
        ///         (NOT_AN_OBJECT / KEY_NOT_FOUND) if access fails.
        [[nodiscard]] std::expected<std::reference_wrapper<JSON>, JSONError> at(std::string_view key)
        {
            std::expected<std::reference_wrapper<JSON>, JSONError> result{std::unexpected(JSONError::NOT_AN_OBJECT)};
            if (is_object())
            {
                auto& obj = std::get<JSONObject>(data_);
                auto it = obj.find(key); // transparent find — no allocation.
                if (it == obj.end())
                {
                    result = std::unexpected(JSONError::KEY_NOT_FOUND);
                }
                else
                {
                    result = std::ref(it->second);
                }
            }
            return result;
        }

        /// @brief Checked array element access — const.
        /// @param index  zero-based element position.
        /// @return a reference wrapper to the element, or a JSONError
        ///         (NOT_AN_ARRAY / INDEX_OUT_OF_RANGE) if access fails.
        [[nodiscard]] std::expected<std::reference_wrapper<const JSON>, JSONError> at(std::size_t index) const
        {
            std::expected<std::reference_wrapper<const JSON>, JSONError> result{std::unexpected(JSONError::NOT_AN_ARRAY)};
            if (is_array())
            {
                const auto& arr = std::get<JSONArray>(data_);
                if (index >= arr.size())
                {
                    result = std::unexpected(JSONError::INDEX_OUT_OF_RANGE);
                }
                else
                {
                    result = std::cref(arr[index]);
                }
            }
            return result;
        }

        /// @brief Checked array element access — mutable.
        /// @param index  zero-based element position.
        /// @return a reference wrapper to the element, or a JSONError
        ///         (NOT_AN_ARRAY / INDEX_OUT_OF_RANGE) if access fails.
        [[nodiscard]] std::expected<std::reference_wrapper<JSON>, JSONError> at(std::size_t index)
        {
            std::expected<std::reference_wrapper<JSON>, JSONError> result{std::unexpected(JSONError::NOT_AN_ARRAY)};
            if (is_array())
            {
                auto& arr = std::get<JSONArray>(data_);
                if (index >= arr.size())
                {
                    result = std::unexpected(JSONError::INDEX_OUT_OF_RANGE);
                }
                else
                {
                    result = std::ref(arr[index]);
                }
            }
            return result;
        }

        // --------------------------------------------------------------------
        //  Object member access
        // --------------------------------------------------------------------

        /// @brief Mutable object member access with auto-vivification.
        ///
        /// A null value is first converted into an empty object, and a
        /// missing key is inserted holding null, so chained calls build
        /// structure on the fly: j["a"]["b"] = 1;
        ///
        /// @param key  member name.
        /// @return mutable reference to the member's value.
        /// @throws std::bad_variant_access if the value is neither null nor an object.
        [[nodiscard]] JSON& operator[](std::string_view key)
        {
            if (is_null())
            {
                data_.emplace<JSONObject>();
            }
            auto& obj = std::get<JSONObject>(data_);
            auto it = obj.find(key); // transparent find — no allocation for existing keys.
            if (it == obj.end())
            {
                // Key not found — allocate only when inserting a new key.
                it = obj.try_emplace(JSONString(key)).first;
            }
            return it->second;
        }

        /// @brief Read-only object member access — never throws, never inserts.
        ///
        /// @param key  member name.
        /// @return the member's value, or a reference to a shared static
        ///         null value when the key is missing or this is not an object.
        [[nodiscard]] const JSON& operator[](std::string_view key) const
        {
            static const JSON null_value;
            std::reference_wrapper<const JSON> result = std::cref(null_value);
            if (is_object())
            {
                const auto& obj = std::get<JSONObject>(data_);
                auto it = obj.find(key); // transparent find — no allocation.
                if (it != obj.end())
                {
                    result = std::cref(it->second);
                }
            }
            return result.get();
        }

        /// @brief Tests object membership.
        /// @param key  member name.
        /// @return true only when this is an object that contains @p key.
        [[nodiscard]] bool contains(std::string_view key) const noexcept
        {
            bool result = false;
            if (is_object())
            {
                result = std::get<JSONObject>(data_).contains(key); // transparent contains — no allocation.
            }
            return result;
        }

        // --------------------------------------------------------------------
        //  Array element access
        // --------------------------------------------------------------------

        /// @brief Mutable array element access with auto-vivification.
        ///
        /// A null value is first converted into an empty array; if @p index
        /// is past the end, the array is grown with null elements to fit.
        ///
        /// @param index  zero-based element position.
        /// @return mutable reference to the element.
        /// @throws std::bad_variant_access if the value is neither null nor an array.
        [[nodiscard]] JSON& operator[](std::size_t index)
        {
            if (is_null())
            {
                data_.emplace<JSONArray>();
            }
            auto& arr = std::get<JSONArray>(data_);
            if (index >= arr.size())
            {
                arr.resize(index + 1);
            }
            return arr[index];
        }

        /// @brief Read-only array element access — never throws.
        ///
        /// @param index  zero-based element position.
        /// @return the element, or a reference to a shared static null value
        ///         when the value is not an array or @p index is out of range.
        [[nodiscard]] const JSON& operator[](std::size_t index) const
        {
            static const JSON null_value;
            std::reference_wrapper<const JSON> result = std::cref(null_value);
            if (is_array() && index < std::get<JSONArray>(data_).size())
            {
                result = std::cref(std::get<JSONArray>(data_)[index]);
            }
            return result.get();
        }

        // --------------------------------------------------------------------
        //  Container size / mutation
        // --------------------------------------------------------------------

        /// @brief Element count.
        /// @return the array length, the object entry count, or 0 for null
        ///         and scalar values.
        [[nodiscard]] std::size_t size() const noexcept
        {
            std::size_t result = 0;
            if (is_array())
            {
                result = std::get<JSONArray>(data_).size();
            }
            else if (is_object())
            {
                result = std::get<JSONObject>(data_).size();
            }
            return result;
        }

        /// @brief True when the container is empty (also true for null and scalars).
        [[nodiscard]] bool empty() const noexcept
        {
            return size() == 0;
        }

        /// @brief Appends @p value to the end of the array.
        ///
        /// A null value is first converted into an empty array.
        ///
        /// @param value  the element to append (moved in).
        /// @return reference to the newly appended element.
        /// @throws std::bad_variant_access if the value is neither null nor an array.
        JSON& push_back(JSON value)
        {
            if (is_null())
            {
                data_.emplace<JSONArray>();
            }
            auto& arr = std::get<JSONArray>(data_);
            arr.push_back(std::move(value));
            return arr.back();
        }

        /// @brief Removes the entry @p key from the object, if present.
        ///
        /// Silently does nothing when this is not an object or the key is
        /// absent — allows erase() to be called unconditionally.
        ///
        /// @param key  member name to erase.
        void erase(std::string_view key)
        {
            if (is_object())
            {
                auto& obj = std::get<JSONObject>(data_);
                auto it = obj.find(key); // transparent find — no allocation.
                if (it != obj.end())
                {
                    obj.erase(it);
                }
            }
        }

        /// @brief Deep structural equality (compiler-generated).
        ///
        /// Two values are equal when types and contents match. Arrays
        /// compare order-sensitively; objects compare order-insensitively
        /// (the underlying unordered maps are compared directly).
        [[nodiscard]] bool operator==(const JSON& other) const = default;

        // --------------------------------------------------------------------
        //  Serialisation
        // --------------------------------------------------------------------

        /// @brief Serialises the value into a JSON text string.
        /// @param indent  spaces per nesting level for pretty-printing;
        ///                a negative value selects compact single-line output.
        /// @return the serialised JSON text (UTF-8).
        /// @note NaN serialises as null and infinities as ±1e309, since JSON
        ///       has no literals for them.
        [[nodiscard]] std::string dump(int indent = -1) const
        {
            std::string out;
            out.reserve(256);
            dump_value(out, indent, 0);
            return out;
        }

        /// @brief Streams the serialised form into @p os.
        /// @param os             destination stream.
        /// @param indent         spaces per nesting level (negative = compact).
        /// @param current_indent internal recursion parameter; leave at the default.
        void dump(std::ostream& os, int indent = -1, int current_indent = 0) const
        {
            std::string out;
            out.reserve(256);
            dump_value(out, indent, current_indent);
            os.write(out.data(), static_cast<std::streamsize>(out.size()));
        }

        // --------------------------------------------------------------------
        //  Iteration (array elements)
        // --------------------------------------------------------------------

        /// @brief Mutable iterator to the first array element.
        /// @note A null value is converted into an empty array first, so
        ///       range-for loops work on freshly created values.
        /// @throws std::bad_variant_access if the value is not null and not an array.
        [[nodiscard]] auto begin()
        {
            if (is_null())
            {
                data_.emplace<JSONArray>();
            }
            return std::get<JSONArray>(data_).begin();
        }

        /// @brief Mutable end sentinel — see begin().
        [[nodiscard]] auto end()
        {
            if (is_null())
            {
                data_.emplace<JSONArray>();
            }
            return std::get<JSONArray>(data_).end();
        }

        /// @brief Const iterator to the first array element.
        ///
        /// For a non-array value, value-initialised iterators are returned
        /// (they compare equal to end()), so a const range-for simply
        /// iterates over nothing.
        [[nodiscard]] auto begin() const
        {
            return is_array() ? std::get<JSONArray>(data_).begin() : JSONArray::const_iterator{};
        }

        /// @brief Const end sentinel — see begin() const.
        [[nodiscard]] auto end() const
        {
            return is_array() ? std::get<JSONArray>(data_).end() : JSONArray::const_iterator{};
        }

    private:
        /// @brief Core recursive serialiser; dispatches on the active
        ///        variant alternative and writes the JSON text.
        /// @param out            destination string (appended to).
        /// @param indent         pretty-print step (negative = compact output).
        /// @param current_indent indentation already applied at this depth.
        void dump_value(std::string& out, int indent, int current_indent) const
        {
            std::visit(
                [&](const auto& val)
                {
                    using T = std::decay_t<decltype(val)>;

                    // ---- null ----
                    if constexpr (std::is_same_v<T, std::monostate>)
                    {
                        out += "null";
                    }
                    // ---- true / false ----
                    else if constexpr (std::is_same_v<T, JSONBool>)
                    {
                        out += (val ? "true" : "false");
                    }
                    // ---- number (JSON has no NaN/Infinity literals) ----
                    else if constexpr (std::is_same_v<T, JSONNumber>)
                    {
                        if (std::isnan(val))
                        {
                            out += "null"; // NaN is not representable in JSON.
                        }
                        else if (std::isinf(val))
                        {
                            // 1e309 overflows to infinity in every common
                            // double parser, so the value round-trips.
                            out += (val > 0 ? "1e309" : "-1e309");
                        }
                        else
                        {
                            // Shortest round-trippable form, locale-independent.
                            std::array<char, 32> buf{};
                            auto [ptr, ec] = std::to_chars(buf.data(), buf.data() + buf.size(), val);
                            if (ec == std::errc())
                            {
                                out.append(buf.data(), static_cast<std::size_t>(ptr - buf.data()));
                            }
                            else
                            {
                                out += "null"; // Conversion failed — degrade gracefully.
                            }
                        }
                    }
                    // ---- string ----
                    else if constexpr (std::is_same_v<T, JSONString>)
                    {
                        dump_string(out, val);
                    }
                    // ---- array: '[' elem (',' elem)* ']' ----
                    else if constexpr (std::is_same_v<T, JSONArray>)
                    {
                        out += '[';
                        if (indent >= 0 && !val.empty())
                        {
                            // Newline + indentation before the first element.
                            out += '\n';
                            out.append(static_cast<std::size_t>(current_indent + indent), ' ');
                        }
                        bool first = true;
                        for (const auto& elem : val)
                        {
                            if (!first)
                            {
                                out += ',';
                                if (indent >= 0)
                                {
                                    // Newline + indentation between elements.
                                    out += '\n';
                                    out.append(static_cast<std::size_t>(current_indent + indent), ' ');
                                }
                            }
                            first = false;
                            elem.dump_value(out, indent, current_indent + indent); // Recurse one level deeper.
                        }
                        if (indent >= 0 && !val.empty())
                        {
                            // Newline + indentation before the closing bracket.
                            out += '\n';
                            out.append(static_cast<std::size_t>(current_indent), ' ');
                        }
                        out += ']';
                    }
                    // ---- object: '{' "key" ':' value (',')* '}' ----
                    else if constexpr (std::is_same_v<T, JSONObject>)
                    {
                        out += '{';
                        if (indent >= 0 && !val.empty())
                        {
                            out += '\n';
                            out.append(static_cast<std::size_t>(current_indent + indent), ' ');
                        }
                        bool first = true;
                        for (const auto& [k, v] : val)
                        {
                            if (!first)
                            {
                                out += ',';
                                if (indent >= 0)
                                {
                                    out += '\n';
                                    out.append(static_cast<std::size_t>(current_indent + indent), ' ');
                                }
                            }
                            first = false;
                            dump_string(out, k); // Keys are re-escaped on output.
                            out += ':';
                            if (indent >= 0)
                            {
                                out += ' '; // '"key": value' spacing when pretty-printing.
                            }
                            v.dump_value(out, indent, current_indent + indent); // Recurse one level deeper.
                        }
                        if (indent >= 0 && !val.empty())
                        {
                            out += '\n';
                            out.append(static_cast<std::size_t>(current_indent), ' ');
                        }
                        out += '}';
                    }
                },
                data_
            );
        }

        /// @brief Writes @p str as a quoted, fully escaped JSON string literal.
        ///
        /// Escapes the two mandatory characters ('"' and '\'), the short
        /// escapes (\b \f \n \r \t), and emits all remaining control
        /// characters below 0x20 as \u00XX. Bytes 0x20–0xFF pass through
        /// verbatim, so existing UTF-8 sequences are left unchanged.
        ///
        /// @param out  destination string (appended to).
        /// @param str  the raw (unescaped) string content.
        void dump_string(std::string& out, const std::string& str) const
        {
            static constexpr const char hex_digits[] = "0123456789abcdef";

            out += '"';
            for (unsigned char c : str)
            {
                switch (c)
                {
                case '"':
                    out += "\\\"";
                    break;
                case '\\':
                    out += "\\\\";
                    break;
                case '\b':
                    out += "\\b";
                    break;
                case '\f':
                    out += "\\f";
                    break;
                case '\n':
                    out += "\\n";
                    break;
                case '\r':
                    out += "\\r";
                    break;
                case '\t':
                    out += "\\t";
                    break;
                default:
                    if (c < 0x20)
                    {
                        // Remaining control chars — manual hex conversion (faster than std::format).
                        out += '\\';
                        out += 'u';
                        out += '0';
                        out += '0';
                        out += hex_digits[(c >> 4) & 0x0F];
                        out += hex_digits[c & 0x0F];
                    }
                    else
                    {
                        out += static_cast<char>(c); // Printable ASCII / UTF-8 bytes.
                    }
                    break;
                }
            }
            out += '"';
        }
    }; // end class JSON
        /// @brief A strict, single-pass recursive-descent JSON parser.
    ///
    /// Validates the full JSON grammar (RFC 8259), decodes \uXXXX escapes
    /// including UTF-16 surrogate pairs (re-encoded as UTF-8), and returns
    /// either the fully built JSON value or a JSONError. It never throws
    /// for malformed input.
    class JSONParser
    {
    public:
        /// @brief Constructs a parser over the given text.
        /// @param input  the JSON text to parse; the view (and the buffer it
        ///               views) must outlive the parser instance.
        explicit JSONParser(std::string_view input) : input_(input), pos_(0) {}

        /// @brief Parses the entire input as a single JSON document.
        ///
        /// Leading/trailing whitespace is allowed; any trailing
        /// non-whitespace content after the value is rejected.
        ///
        /// @return the parsed value, or a JSONError describing the failure.
        [[nodiscard]] std::expected<JSON, JSONError> parse()
        {
            skip_whitespace();
            auto result = parse_value();
            if (result)
            {
                // A valid value must be followed only by whitespace.
                skip_whitespace();
                if (pos_ != input_.size())
                {
                    result = std::unexpected(JSONError::INVALID_SYNTAX);
                }
            }
            return result;
        }

    private:
        std::string_view input_; ///< The full text being parsed.
        std::size_t pos_;        ///< Current read position (0-based index into input_).

        /// @brief Returns the character at the cursor without consuming it.
        /// @return the character, or '\0' once the end of input is reached.
        [[nodiscard]] char peek() const noexcept
        {
            return pos_ < input_.size() ? input_[pos_] : '\0';
        }

        /// @brief Returns the character at the cursor and advances past it.
        /// @return the character, or '\0' (without moving) at end of input.
        [[nodiscard]] char get() noexcept
        {
            return pos_ < input_.size() ? input_[pos_++] : '\0';
        }

        /// @brief Advances the cursor by one position, if input remains.
        void advance() noexcept
        {
            if (pos_ < input_.size())
            {
                ++pos_;
            }
        }

        /// @brief Skips any run of whitespace at the cursor
        ///        (space, \t, \n, \v, \f, \r).
        void skip_whitespace() noexcept
        {
            while (pos_ < input_.size() && std::isspace(static_cast<unsigned char>(input_[pos_])))
            {
                ++pos_;
            }
        }

        /// @brief If the character at the cursor equals @p c, consumes it.
        /// @param c  the expected character.
        /// @return true when the character matched and was consumed.
        [[nodiscard]] bool consume(char c) noexcept
        {
            bool result = false;
            if (peek() == c)
            {
                ++pos_;
                result = true;
            }
            return result;
        }

        /// @brief Parses exactly one JSON value, dispatching on its first
        ///        character. Leading whitespace is skipped first.
        ///
        /// @return the parsed value, or the error reported by the delegated
        ///         sub-parser (UNEXPECTED_END if the input is exhausted).
        [[nodiscard]] std::expected<JSON, JSONError> parse_value()
        {
            std::expected<JSON, JSONError> result;
            skip_whitespace();
            char c = peek();

            if (c == '\0')
            {
                result = std::unexpected(JSONError::UNEXPECTED_END);
            }
            else
            {
                switch (c)
                {
                case '"':
                    result = parse_string();
                    break;
                case '{':
                    result = parse_object();
                    break;
                case '[':
                    result = parse_array();
                    break;
                case 't':
                case 'f':
                    result = parse_bool();
                    break;
                case 'n':
                    result = parse_null();
                    break;
                case '-':
                case '0':
                case '1':
                case '2':
                case '3':
                case '4':
                case '5':
                case '6':
                case '7':
                case '8':
                case '9':
                    result = parse_number();
                    break;
                default:
                    result = std::unexpected(JSONError::INVALID_SYNTAX);
                    break;
                }
            }
            return result;
        }

        /// @brief Parses a double-quoted string literal with escape handling.
        ///
        /// Handles the standard short escapes (\" \\ \/ \b \f \n \r \t),
        /// \uXXXX escapes — including UTF-16 surrogate pairs, which are
        /// combined and re-encoded as UTF-8 — and rejects raw control
        /// characters below 0x20 appearing outside escapes.
        ///
        /// @return the decoded string as a JSON value, or one of:
        ///         - UNEXPECTED_END              — input ended mid-string
        ///         - INVALID_STRING_ESCAPE       — unknown escape character
        ///         - INVALID_UNICODE             — bad hex digits / lone surrogate
        ///         - UNESCAPED_CONTROL_CHARACTER — raw control byte
        [[nodiscard]] std::expected<JSON, JSONError> parse_string()
        {
            std::expected<JSON, JSONError> result;

            if (!consume('"'))
            {
                result = std::unexpected(JSONError::INVALID_SYNTAX);
            }
            else
            {
                std::string str_result;
                bool parsing = true;

                while (parsing)
                {
                    char c = get();

                    if (c == '\0')
                    {
                        result = std::unexpected(JSONError::UNEXPECTED_END);
                        parsing = false;
                    }
                    else if (c == '"')
                    {
                        // Closing quote — done.
                        result = JSON(std::move(str_result));
                        parsing = false;
                    }
                    else if (c == '\\')
                    {
                        // Escape sequence: dispatch on the character after '\'.
                        char esc = get();
                        if (esc == '\0')
                        {
                            result = std::unexpected(JSONError::UNEXPECTED_END);
                            parsing = false;
                        }
                        else
                        {
                            switch (esc)
                            {
                            case '"':
                                str_result += '"';
                                break;
                            case '\\':
                                str_result += '\\';
                                break;
                            case '/':
                                str_result += '/';
                                break;
                            case 'b':
                                str_result += '\b';
                                break;
                            case 'f':
                                str_result += '\f';
                                break;
                            case 'n':
                                str_result += '\n';
                                break;
                            case 'r':
                                str_result += '\r';
                                break;
                            case 't':
                                str_result += '\t';
                                break;
                            case 'u':
                            {
                                // Read the four hexadecimal digits of the \uXXXX escape.
                                std::string hex;
                                bool hex_complete = true;
                                for (int i = 0; i < 4; ++i)
                                {
                                    char h = get();
                                    if (h == '\0')
                                    {
                                        result = std::unexpected(JSONError::UNEXPECTED_END);
                                        parsing = false;
                                        hex_complete = false;
                                        break;
                                    }
                                    hex += h;
                                }
                                if (!hex_complete)
                                {
                                    break; // Break out of switch — outer loop will exit.
                                }

                                int codepoint = 0;
                                auto [ptr, ec] = std::from_chars(hex.data(), hex.data() + hex.size(), codepoint, 16);
                                if (ec != std::errc())
                                {
                                    result = std::unexpected(JSONError::INVALID_UNICODE);
                                    parsing = false;
                                    break;
                                }

                                // A value in 0xD800..0xDBFF is a high surrogate; it
                                // must be followed by a \uXXXX low surrogate
                                // (0xDC00..0xDFFF). The pair combines into a code
                                // point above 0xFFFF.
                                if (codepoint >= 0xD800 && codepoint <= 0xDBFF)
                                {
                                    // Check for '\' followed by 'u' of the low-surrogate escape.
                                    if (peek() != '\\')
                                    {
                                        result = std::unexpected(JSONError::INVALID_UNICODE);
                                        parsing = false;
                                        break;
                                    }
                                    advance(); // Consume '\'.
                                    if (get() != 'u')
                                    {
                                        result = std::unexpected(JSONError::INVALID_UNICODE);
                                        parsing = false;
                                        break;
                                    }

                                    // Read the four hexadecimal digits of the low surrogate.
                                    std::string hex2;
                                    bool hex2_complete = true;
                                    for (int i = 0; i < 4; ++i)
                                    {
                                        char h = get();
                                        if (h == '\0')
                                        {
                                            result = std::unexpected(JSONError::UNEXPECTED_END);
                                            parsing = false;
                                            hex2_complete = false;
                                            break;
                                        }
                                        hex2 += h;
                                    }
                                    if (!hex2_complete)
                                    {
                                        break;
                                    }

                                    int codepoint2 = 0;
                                    auto [ptr2, ec2] = std::from_chars(hex2.data(), hex2.data() + hex2.size(), codepoint2, 16);
                                    if (ec2 != std::errc() || codepoint2 < 0xDC00 || codepoint2 > 0xDFFF)
                                    {
                                        result = std::unexpected(JSONError::INVALID_UNICODE);
                                        parsing = false;
                                        break;
                                    }

                                    // Surrogate-pair → real code point formula.
                                    codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + (codepoint2 - 0xDC00);
                                }

                                // Encode the code point as UTF-8 (1–4 bytes by range).
                                if (codepoint < 0x80)
                                {
                                    str_result += static_cast<char>(codepoint); // 1 byte: ASCII
                                }
                                else if (codepoint < 0x800)
                                {
                                    str_result += static_cast<char>(0xC0 | (codepoint >> 6));         // 2 bytes
                                    str_result += static_cast<char>(0x80 | (codepoint & 0x3F));
                                }
                                else if (codepoint < 0x10000)
                                {
                                    str_result += static_cast<char>(0xE0 | (codepoint >> 12));        // 3 bytes
                                    str_result += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
                                    str_result += static_cast<char>(0x80 | (codepoint & 0x3F));
                                }
                                else
                                {
                                    str_result += static_cast<char>(0xF0 | (codepoint >> 18));        // 4 bytes
                                    str_result += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
                                    str_result += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
                                    str_result += static_cast<char>(0x80 | (codepoint & 0x3F));
                                }
                                break;
                            }
                            default:
                                result = std::unexpected(JSONError::INVALID_STRING_ESCAPE);
                                parsing = false;
                                break;
                            }
                        }
                    }
                    else if (static_cast<unsigned char>(c) < 0x20)
                    {
                        result = std::unexpected(JSONError::UNESCAPED_CONTROL_CHARACTER);
                        parsing = false;
                    }
                    else
                    {
                        str_result += c; // Ordinary byte (may be part of UTF-8 sequence).
                    }
                }
            }
            return result;
        }

        /// @brief Parses a numeric literal following the JSON grammar:
        ///        [-] int [frac] [exp], where
        ///        int  = '0' | [1-9][0-9]*
        ///        frac = '.' [0-9]+
        ///        exp  = ('e'|'E') ['+'|'-'] [0-9]+
        ///
        /// The matched text is converted with std::from_chars
        /// (locale-independent). Leading zeros like "01" and a trailing
        /// partial exponent are rejected.
        ///
        /// @return the number as a JSON value, or INVALID_NUMBER if the text
        ///         does not match the grammar or fails to convert.
        [[nodiscard]] std::expected<JSON, JSONError> parse_number()
        {
            std::expected<JSON, JSONError> result;
            std::size_t start = pos_;
            bool has_digits = false;
            bool error = false;

            if (consume('-'))
            {
                // Optional leading minus — consume() already advanced.
            }

            // Integer part: either a single '0' or a nonzero-leading digit run.
            if (consume('0'))
            {
                has_digits = true;
            }
            else if (peek() >= '1' && peek() <= '9')
            {
                has_digits = true;
                while (peek() >= '0' && peek() <= '9')
                {
                    advance();
                }
            }

            if (!has_digits)
            {
                result = std::unexpected(JSONError::INVALID_NUMBER);
                error = true;
            }

            if (!error)
            {
                // Fractional part: '.' followed by at least one digit.
                if (consume('.'))
                {
                    bool frac = false;
                    while (peek() >= '0' && peek() <= '9')
                    {
                        advance();
                        frac = true;
                    }
                    if (!frac)
                    {
                        result = std::unexpected(JSONError::INVALID_NUMBER);
                        error = true;
                    }
                }
            }

            if (!error)
            {
                // Exponent part: 'e'/'E', optional sign, at least one digit.
                if (peek() == 'e' || peek() == 'E')
                {
                    advance();
                    if (peek() == '+' || peek() == '-')
                    {
                        advance();
                    }
                    bool exp = false;
                    while (peek() >= '0' && peek() <= '9')
                    {
                        advance();
                        exp = true;
                    }
                    if (!exp)
                    {
                        result = std::unexpected(JSONError::INVALID_NUMBER);
                        error = true;
                    }
                }
            }

            if (!error)
            {
                // Convert the exact matched text; every consumed char must be used.
                std::string_view num_str = input_.substr(start, pos_ - start);
                double val = 0.0;
                auto [ptr, ec] = std::from_chars(num_str.data(), num_str.data() + num_str.size(), val);
                if (ec != std::errc() || ptr != num_str.data() + num_str.size())
                {
                    result = std::unexpected(JSONError::INVALID_NUMBER);
                }
                else
                {
                    result = JSON(val);
                }
            }

            return result;
        }

        /// @brief Parses the "true" / "false" literals.
        /// @return the boolean value, or INVALID_LITERAL for anything else.
        [[nodiscard]] std::expected<JSON, JSONError> parse_bool()
        {
            std::expected<JSON, JSONError> result;
            if (input_.substr(pos_, 4) == "true")
            {
                pos_ += 4;
                result = JSON(true);
            }
            else if (input_.substr(pos_, 5) == "false")
            {
                pos_ += 5;
                result = JSON(false);
            }
            else
            {
                result = std::unexpected(JSONError::INVALID_LITERAL);
            }
            return result;
        }

        /// @brief Parses the "null" literal.
        /// @return a null value, or INVALID_LITERAL for anything else.
        [[nodiscard]] std::expected<JSON, JSONError> parse_null()
        {
            std::expected<JSON, JSONError> result;
            if (input_.substr(pos_, 4) == "null")
            {
                pos_ += 4;
                result = JSON(nullptr);
            }
            else
            {
                result = std::unexpected(JSONError::INVALID_LITERAL);
            }
            return result;
        }

        /// @brief Parses an array literal: '[' value (',' value)* ']' or '[]'.
        ///
        /// @return the array as a JSON value, or the first error encountered
        ///         (INVALID_SYNTAX for structural problems, or an error
        ///         propagated from an element's parser).
        [[nodiscard]] std::expected<JSON, JSONError> parse_array()
        {
            std::expected<JSON, JSONError> result;

            if (!consume('['))
            {
                result = std::unexpected(JSONError::INVALID_SYNTAX);
            }
            else
            {
                JSONArray arr;
                skip_whitespace();
                if (consume(']'))
                {
                    result = JSON(std::move(arr)); // Empty array: [].
                }
                else
                {
                    bool parsing = true;
                    while (parsing)
                    {
                        skip_whitespace();
                        auto val = parse_value(); // Parse one element.
                        if (!val)
                        {
                            result = std::unexpected(val.error()); // Propagate element error.
                            parsing = false;
                        }
                        else
                        {
                            arr.push_back(std::move(*val));
                            skip_whitespace();
                            if (consume(']'))
                            {
                                result = JSON(std::move(arr)); // Closing bracket — done.
                                parsing = false;
                            }
                            else if (!consume(','))
                            {
                                result = std::unexpected(JSONError::INVALID_SYNTAX); // Missing separator.
                                parsing = false;
                            }
                        }
                    }
                }
            }
            return result;
        }

        /// @brief Parses an object literal:
        ///        '{' string ':' value (',' string ':' value)* '}' or '{}'.
        ///
        /// Keys must be string literals; if the same key appears more than
        /// once, the last value wins (unordered_map assignment semantics).
        ///
        /// @return the object as a JSON value, or the first error encountered
        ///         (INVALID_SYNTAX for structural problems, or an error
        ///         propagated from a key/value parser).
        [[nodiscard]] std::expected<JSON, JSONError> parse_object()
        {
            std::expected<JSON, JSONError> result;

            if (!consume('{'))
            {
                result = std::unexpected(JSONError::INVALID_SYNTAX);
            }
            else
            {
                JSONObject obj;
                skip_whitespace();
                if (consume('}'))
                {
                    result = JSON(std::move(obj)); // Empty object: {}.
                }
                else
                {
                    bool parsing = true;
                    while (parsing)
                    {
                        skip_whitespace();
                        auto key_result = parse_string(); // Parse the key literal.
                        if (!key_result)
                        {
                            result = key_result; // Propagate key parse error.
                            parsing = false;
                        }
                        else
                        {
                            // parse_string() only returns strings or errors — no need to check type.
                            std::string key = key_result->get_string();
                            skip_whitespace();
                            if (!consume(':'))
                            {
                                result = std::unexpected(JSONError::INVALID_SYNTAX); // Missing ':'.
                                parsing = false;
                            }
                            else
                            {
                                skip_whitespace();
                                auto val = parse_value(); // Parse the member's value.
                                if (!val)
                                {
                                    result = val; // Propagate value error.
                                    parsing = false;
                                }
                                else
                                {
                                    obj[std::move(key)] = std::move(*val);
                                    skip_whitespace();
                                    if (consume('}'))
                                    {
                                        result = JSON(std::move(obj)); // Closing brace — done.
                                        parsing = false;
                                    }
                                    else if (!consume(','))
                                    {
                                        result = std::unexpected(JSONError::INVALID_SYNTAX); // Missing separator.
                                        parsing = false;
                                    }
                                }
                            }
                        }
                    }
                }
            }
            return result;
        }
    }; // end class JSONParser

    /// @brief Parses @p json_text as one strict JSON document.
    ///
    /// Thin convenience wrapper around JSONParser.
    ///
    /// @param json_text  the complete JSON text.
    /// @return the parsed value, or a JSONError on any parse failure.
    /// @see parse_lenient() for tolerant parsing.
    [[nodiscard]] inline std::expected<JSON, JSONError> parse(std::string_view json_text)
    {
        JSONParser parser(json_text);
        return parser.parse();
    }

    /// @brief Parses JSON from "dirty" text such as LLM or Markdown output.
    ///
    /// Before running the strict parser, the text is repaired:
    ///   1. A leading ```json / ``` fence and trailing ``` are removed.
    ///   2. Surrounding whitespace is trimmed; whitespace-only input yields null.
    ///   3. Any prose preceding the first '{' or '[' is discarded.
    ///   4. Unbalanced braces / brackets (counted while respecting string
    ///      literals and backslash escapes) are closed by appending the
    ///      missing '}' / ']' characters in the correct nesting order
    ///      (tracked via a stack so closures match the opening order).
    ///
    /// @param json_text  the raw, possibly decorated text.
    /// @return the parsed JSON value, or the JSONError reported by the
    ///         strict parser on the repaired text.
    [[nodiscard]] inline std::expected<JSON, JSONError> parse_lenient(std::string_view json_text)
    {
        std::string s(json_text);

        // 1) Strip Markdown code fences: ```json ... ``` or ``` ... ```.
        if (s.starts_with("```json"))
        {
            s.erase(0, 7);
        }
        else if (s.starts_with("```"))
        {
            s.erase(0, 3);
        }
        if (s.ends_with("```"))
        {
            s.erase(s.length() - 3, 3);
        }

        // 2) Trim leading/trailing whitespace; whitespace-only input → null.
        auto start = s.find_first_not_of(" \t\n\r");

        // Default result for whitespace-only input.
        std::expected<JSON, JSONError> result = JSON(nullptr);

        if (start != std::string::npos)
        {
            s = s.substr(start, s.find_last_not_of(" \t\n\r") - start + 1);

            // 3) Discard any prose that appears before the first '{' or '['.
            std::size_t first_brace = s.find_first_of("{[");
            if (first_brace != std::string::npos && first_brace > 0)
            {
                s = s.substr(first_brace);
            }

            // 4) Track unmatched containers using a stack, ignoring braces
            //    and brackets that appear inside string literals or
            //    immediately after a backslash escape.
            std::string open_containers;
            bool in_string = false;
            bool escape = false;

            for (char c : s)
            {
                if (escape)
                {
                    escape = false; // Previous char was '\': skip this one.
                    continue;
                }
                if (c == '\\')
                {
                    escape = true; // Next char is escaped.
                    continue;
                }
                if (c == '"')
                {
                    in_string = !in_string; // Toggle string state.
                    continue;
                }
                if (in_string)
                {
                    continue; // Ignore everything inside a string.
                }
                if (c == '{' || c == '[')
                {
                    open_containers.push_back(c);
                }
                else if (c == '}')
                {
                    if (!open_containers.empty() && open_containers.back() == '{')
                    {
                        open_containers.pop_back();
                    }
                }
                else if (c == ']')
                {
                    if (!open_containers.empty() && open_containers.back() == '[')
                    {
                        open_containers.pop_back();
                    }
                }
            }

            // 5) Close any containers the author left open, in reverse
            //    nesting order so the output is structurally valid.
            for (auto it = open_containers.rbegin(); it != open_containers.rend(); ++it)
            {
                if (*it == '{')
                {
                    s += '}';
                }
                else
                {
                    s += ']';
                }
            }

            // 6) Run the strict parser over the repaired text.
            JSONParser parser(s);
            result = parser.parse();
        }

        return result;
    }
}

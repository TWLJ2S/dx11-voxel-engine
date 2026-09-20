#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>
#include <stdexcept>

namespace ac {

    template<typename T, size_t size>
    struct vec {
        T _data[size];

        // Default constructor - zero initialize
        vec() {
            std::fill(_data, _data + size, T{});
        }

        // Constructor from array reference
        vec(const T(&other)[size]) {
            std::copy(other, other + size, _data);
        }

        // Constructor from std::array
        vec(const std::array<T, size>& other) {
            std::copy(other.begin(), other.end(), _data);
        }

        // Constructor from initializer list
        vec(std::initializer_list<T> list) {
            if (list.size() != size) [[unlikely]] {
                throw std::runtime_error("Initializer list size does not match vector size");
            }
            std::copy(list.begin(), list.end(), _data);
        }

        template<typename... Args>
        vec(Args... args) {
            static_assert(sizeof...(Args) <= size, "Too many arguments for fixed-size vector");

            if constexpr (sizeof...(Args) == size)
                fill_array(std::index_sequence_for<Args...>{}, std::forward<Args>(args)...);
            else 
                initialize_with_default(std::index_sequence_for<Args...>{}, std::forward<Args>(args)...);
        }

        // For size mismatch at runtime
        template<typename... Args>
        vec(size_t runtime_size, Args... args) {
            if (runtime_size != size) {
                throw std::runtime_error("Initializer list size does not match vector size");
            }
            // Fill the array...
        }

        // Copy constructor
        vec(const vec& other) {
            std::copy(other._data, other._data + size, _data);
        }

        // Move constructor
        vec(vec&& other) noexcept {
            std::move(other._data, other._data + size, _data);
        }

        // Copy assignment
        vec& operator=(const vec& other) {
            if (this != &other) {
                std::copy(other._data, other._data + size, _data);
            }
            return *this;
        }

        // Move assignment
        vec& operator=(vec&& other) noexcept {
            if (this != &other) {
                std::move(other._data, other._data + size, _data);
            }
            return *this;
        }

        // Array subscript operators
        T& operator[](size_t index) {
            return _data[index];
        }

        const T& operator[](size_t index) const {
            return _data[index];
        }

        // Size getter
        constexpr size_t getSize() const { return size; }

        // Data access
        T* data() { return _data; }
        const T* data() const { return _data; }

        // Begin/End iterators
        T* begin() { return _data; }
        const T* begin() const { return _data; }
        T* end() { return _data + size; }
        const T* end() const { return _data + size; }

        // Comparison operators
        bool operator==(const vec& other) const {
            for (size_t i = 0; i < size; ++i) {
                if (_data[i] != other._data[i]) return false;
            }
            return true;
        }

        bool operator!=(const vec& other) const {
            return !(*this == other);
        }

        // Arithmetic operators (if T supports them)
        vec operator+(const vec& other) const {
            vec result;
            for (size_t i = 0; i < size; ++i) {
                result._data[i] = _data[i] + other._data[i];
            }
            return result;
        }

        vec operator-(const vec& other) const {
            vec result;
            for (size_t i = 0; i < size; ++i) {
                result._data[i] = _data[i] - other._data[i];
            }
            return result;
        }

        vec operator*(T scalar) const {
            vec result;
            for (size_t i = 0; i < size; ++i) {
                result._data[i] = _data[i] * scalar;
            }
            return result;
        }

        vec operator/(T scalar) const {
            if (scalar == T{}) {
                throw std::runtime_error("Division by zero");
            }
            vec result;
            for (size_t i = 0; i < size; ++i) {
                result._data[i] = _data[i] / scalar;
            }
            return result;
        }

        // In-place arithmetic
        vec& operator+=(const vec& other) {
            for (size_t i = 0; i < size; ++i) {
                _data[i] += other._data[i];
            }
            return *this;
        }

        vec& operator-=(const vec& other) {
            for (size_t i = 0; i < size; ++i) {
                _data[i] -= other._data[i];
            }
            return *this;
        }

        vec& operator*=(T scalar) {
            for (size_t i = 0; i < size; ++i) {
                _data[i] *= scalar;
            }
            return *this;
        }

        vec& operator/=(T scalar) {
            if (scalar == T{}) {
                throw std::runtime_error("Division by zero");
            }
            for (size_t i = 0; i < size; ++i) {
                _data[i] /= scalar;
            }
            return *this;
        }

        // Dot product
        T dot(const vec& other) const {
            T result = T{};
            for (size_t i = 0; i < size; ++i) {
                result += _data[i] * other._data[i];
            }
            return result;
        }

        // Magnitude (length)
        T magnitude() const {
            T sum = T{};
            for (size_t i = 0; i < size; ++i) {
                sum += _data[i] * _data[i];
            }
            return std::sqrt(sum);
        }

        // Normalize
        vec normalized() const {
            T mag = magnitude();
            if (mag == T{}) {
                throw std::runtime_error("Cannot normalize zero vector");
            }
            return *this / mag;
        }

        // Cross product (only for 3D vectors)
        vec cross(const vec& other) const {
            //static_assert(size == 3, "Cross product only valid for 3D vectors");
            vec result;
            result._data[0] = _data[1] * other._data[2] - _data[2] * other._data[1];
            result._data[1] = _data[2] * other._data[0] - _data[0] * other._data[2];
            result._data[2] = _data[0] * other._data[1] - _data[1] * other._data[0];
            return result;
        }

    private:
        template<size_t... Is, typename... Args>
        void fill_array(std::index_sequence<Is...>, Args&&... args) {
            ((_data[Is] = std::forward<Args>(args)), ...);
        }

        template<size_t... Is, typename... Args>
        void initialize_with_default(std::index_sequence<Is...>, Args&&... args) {
            // Fill provided arguments
            size_t i = 0;
            ((_data[i++] = std::forward<Args>(args)), ...);
            // Fill remaining with default-constructed T
            for (; i < size; ++i) {
                _data[i] = T{};
            }
        }
    };

    // Type aliases for common vector types
    using vec2f = vec<float, 2>;
    using vec3f = vec<float, 3>;
    using vec4f = vec<float, 4>;
    using vec2d = vec<double, 2>;
    using vec3d = vec<double, 3>;
    using vec4d = vec<double, 4>;
    using vec2i = vec<int, 2>;
    using vec3i = vec<int, 3>;
    using vec4i = vec<int, 4>;

} // namespace ac
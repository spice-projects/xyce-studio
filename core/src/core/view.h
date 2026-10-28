#pragma once

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

template <typename T>
class View
{
public:
    class Iterator
    {
    public:
        using iterator_concept = std::forward_iterator_tag;
        using iterator_category = std::forward_iterator_tag;
        using value_type = T;
        using difference_type = std::ptrdiff_t;
        using pointer = const T*;
        using reference = const T&;

        Iterator() :
            m_ptr(nullptr), m_stride(1) {}

        Iterator(const T* ptr, size_t stride) :
            m_ptr(ptr), m_stride(stride) {}

        reference operator*() const { return *m_ptr; }

        pointer operator->() const { return m_ptr; }

        Iterator& operator++() {
            m_ptr += m_stride;
            return *this;
        }

        Iterator operator++(int) {
            Iterator tmp = *this;
            m_ptr += m_stride;
            return tmp;
        }

        friend bool operator==(const Iterator& lhs, const Iterator& rhs) { return lhs.m_ptr == rhs.m_ptr; }

        friend bool operator!=(const Iterator& lhs, const Iterator& rhs) { return lhs.m_ptr != rhs.m_ptr; }

    private:
        const T* m_ptr;
        size_t m_stride;
    };

    View() = default;

    // prevent copying, allow moving
    View(const View&) = delete;

    View(View&& other) noexcept :
        m_pointer(std::exchange(other.m_pointer, nullptr)), m_size(std::exchange(other.m_size, 0)), m_stride(std::exchange(other.m_stride, 1)), m_data(std::move(other.m_data)), m_data_owner(std::move(other.m_data_owner)) {}

    View(const T* pointer, const size_t size, const size_t stride = 1, std::shared_ptr<View> data_owner = nullptr) :
        m_pointer(const_cast<T*>(pointer)), m_size(size), m_stride(stride), m_data({}), m_data_owner(data_owner) {}

    explicit View(std::vector<T>&& vector) :
        m_stride(1), m_data(std::move(vector)), m_data_owner(nullptr) {
        // pointer to vector data
        m_pointer = m_data.data();
        m_size = m_data.size();
    }

    ~View() = default;

    // prevent copying, allow moving
    View& operator=(const View&) = delete;

    View& operator=(View&& other) noexcept {
        // check for self-assignment
        if (this != &other) {
            // move data from other to this
            m_pointer = std::exchange(other.m_pointer, nullptr);
            m_size = std::exchange(other.m_size, 0);
            m_stride = std::exchange(other.m_stride, 1);
            m_data = std::move(other.m_data);
            m_data_owner = std::move(other.m_data_owner);
        }
        return *this;
    }

    const T& operator[](const size_t index) const { return m_pointer[index * m_stride]; }

    [[nodiscard]] size_t size() const { return m_size; }

    [[nodiscard]] const T* data() const { return m_pointer; }

    [[nodiscard]] size_t stride() const { return m_stride; }

    [[nodiscard]] bool empty() const { return m_size == 0; }

    Iterator begin() const { return Iterator(m_pointer, m_stride); }

    Iterator end() const { return Iterator(m_pointer + (m_size * m_stride), m_stride); }

private:
    template <typename U>
    friend class Expression;

    T* m_pointer = nullptr;
    size_t m_size = 0;
    size_t m_stride = 1;

    std::vector<T> m_data;

    std::shared_ptr<View> m_data_owner;
};

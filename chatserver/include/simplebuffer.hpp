#ifndef NEXUSIM_SIMPLEBUFFER_H
#define NEXUSIM_SIMPLEBUFFER_H

#include <cstddef>
#include <string>

namespace nexusim
{

class SimpleBuffer
{
public:
    void append(const char *data, std::size_t size)
    {
        data_.append(data, size);
    }

    void append(const std::string &data)
    {
        data_.append(data);
    }

    std::size_t readableBytes() const noexcept
    {
        return data_.size() - readIndex_;
    }

    const char *peek() const noexcept
    {
        return data_.data() + readIndex_;
    }

    void retrieve(std::size_t size)
    {
        readIndex_ += size;
        compactIfNeeded();
    }

    std::string retrieveAsString(std::size_t size)
    {
        std::string value(data_.data() + readIndex_, size);
        retrieve(size);
        return value;
    }

private:
    void compactIfNeeded()
    {
        if (readIndex_ == data_.size())
        {
            data_.clear();
            readIndex_ = 0;
        }
        else if (readIndex_ > 4096 && readIndex_ * 2 > data_.size())
        {
            data_.erase(0, readIndex_);
            readIndex_ = 0;
        }
    }

    std::string data_;
    std::size_t readIndex_{0};
};

} // namespace nexusim

#endif


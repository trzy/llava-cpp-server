#ifndef INCLUDED_LLAVA_REQUEST_HPP
#define INCLUDED_LLAVA_REQUEST_HPP

#include <string>
#include <memory>

struct llava_request
{
    std::string prompt;
    std::shared_ptr<uint8_t[]> image;
    size_t image_buffer_size;
};

#endif  // INCLUDED_LLAVA_REQUEST_HPP
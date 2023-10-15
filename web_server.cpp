//
//  upload.cc
//
//  Copyright (c) 2019 Yuji Hirose. All rights reserved.
//  MIT License
//

#include "llava_request.hpp"

#include "cpp-httplib/httplib.h"

#include <fstream>
#include <iostream>
#include <memory>
using namespace httplib;

const char *html = R"(
<form id="formElem">
  <span>Prompt: </span><input type="text" name="prompt" accept="text/*"><br>
  <input type="file" name="image_file" accept="image/*"><br>
  <input type="submit">
</form>
<script>
  formElem.onsubmit = async (e) => {
    e.preventDefault();
    let res = await fetch('/post', {
      method: 'POST',
      body: new FormData(formElem)
    });
    console.log(await res.text());
  };
</script>
)";

std::string dump_headers(const Headers &headers)
{
    std::string s;
    char buf[BUFSIZ];

    for (auto it = headers.begin(); it != headers.end(); ++it)
    {
        const auto &x = *it;
        snprintf(buf, sizeof(buf), "%s: %s\n", x.first.c_str(), x.second.c_str());
        s += buf;
    }

    return s;
}

std::string log(const Request &req, const Response &res)
{
    std::string s;
    char buf[BUFSIZ];

    s += "================================\n";

    snprintf(buf, sizeof(buf), "%s %s %s", req.method.c_str(),
             req.version.c_str(), req.path.c_str());
    s += buf;

    std::string query;
    for (auto it = req.params.begin(); it != req.params.end(); ++it)
    {
        const auto &x = *it;
        snprintf(buf, sizeof(buf), "%c%s=%s",
                 (it == req.params.begin()) ? '?' : '&', x.first.c_str(),
                 x.second.c_str());
        query += buf;
    }
    snprintf(buf, sizeof(buf), "%s\n", query.c_str());
    s += buf;

    s += dump_headers(req.headers);

    s += "--------------------------------\n";

    snprintf(buf, sizeof(buf), "%d %s\n", res.status, res.version.c_str());
    s += buf;
    s += dump_headers(res.headers);
    s += "\n";

    if (!res.body.empty())
    {
        s += res.body;
    }

    s += "\n";

    return s;
}

void run_web_server(const std::string host, int port, std::function<void(const llava_request &)> hand_off_request)
{
    Server svr;

    svr.Get("/", [](const Request & /*req*/, Response &res) { res.set_content(html, "text/html"); });

    svr.Post("/post", [&hand_off_request](const Request &req, Response &res)
    {
        MultipartFormData prompt = req.get_file_value("prompt");
        MultipartFormData img_data = req.get_file_value("image_file");
        
        res.set_content("done", "text/plain");

        // Hand off to main thread
        size_t image_buffer_size = img_data.content.length();
        auto image_buffer = std::make_unique<uint8_t[]>(image_buffer_size);
        memcpy(image_buffer.get(), img_data.content.c_str(), image_buffer_size);
        llava_request request = {
            .image = std::move(image_buffer),
            .image_buffer_size = image_buffer_size,
            .prompt = prompt.content
        };
        hand_off_request(request);
    });

    svr.set_logger([](const Request &req, const Response &res)
                   { printf("%s", log(req, res).c_str()); });

    svr.listen(host, port);
}

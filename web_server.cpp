//
//  upload.cc
//
//  Copyright (c) 2019 Yuji Hirose. All rights reserved.
//  MIT License
//

#include "cpp-httplib/httplib.h"

#include <fstream>
#include <iostream>
using namespace httplib;

const char *html = R"(
<form id="formElem">
  <input type="file" name="image_file" accept="image/*">
  <input type="text" name="text_file" accept="text/*">
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

std::string dump_headers(const Headers &headers) {
  std::string s;
  char buf[BUFSIZ];

  for (auto it = headers.begin(); it != headers.end(); ++it) {
    const auto &x = *it;
    snprintf(buf, sizeof(buf), "%s: %s\n", x.first.c_str(), x.second.c_str());
    s += buf;
  }

  return s;
}

std::string log(const Request &req, const Response &res) {
  std::string s;
  char buf[BUFSIZ];

  s += "================================\n";

  snprintf(buf, sizeof(buf), "%s %s %s", req.method.c_str(),
           req.version.c_str(), req.path.c_str());
  s += buf;

  std::string query;
  for (auto it = req.params.begin(); it != req.params.end(); ++it) {
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

  if (!res.body.empty()) { s += res.body; }

  s += "\n";

  return s;
}

void run_web_server(const std::string host, int port) {
  Server svr;

  svr.Get("/", [](const Request & /*req*/, Response &res) {
    res.set_content(html, "text/html");
  });

  svr.Post("/post", [](const Request &req, Response &res) {
    auto text_file = req.get_header_value("text_file");

    std::cout << "text length: " << text_file.length() << std::endl
         << "text: " << text_file << std::endl
         << "is_multipart_form: " << req.is_multipart_form_data() << std::endl;

    MultipartFormData data = req.get_file_value("text_file");
    std::cout << "name: " << data.name << std::endl
         << "content: " << data.content << std::endl;

    MultipartFormData img_data = req.get_file_value("image_file");
    std::cout << "name: " << img_data.name << std::endl
         << "size: " << img_data.content.length() << std::endl;

    FILE *fp = fopen("test.jpg", "wb");
    const uint8_t *image_bytes = reinterpret_cast<const uint8_t *>(img_data.content.c_str());
    fwrite(image_bytes, sizeof(uint8_t), img_data.content.length(), fp);
    fclose(fp);

    res.set_content("done", "text/plain");
  });

  svr.set_logger([](const Request &req, const Response &res) {
    printf("%s", log(req, res).c_str());
  });

  svr.listen(host, port);
}

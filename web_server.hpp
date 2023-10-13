#ifndef INCLUDED_WEB_SERVER_HPP
#define INCLUDED_WEB_SERVER_HPP

#include "llava_request.hpp"

#include <string>

void run_web_server(const std::string host, int port, std::function<void(const llava_request &)> hand_off_request);

#endif  // INCLUDED_WEB_SERVER_HPP
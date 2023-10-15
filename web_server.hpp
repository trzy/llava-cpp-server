/*
 * web_server.hpp
 * Bart Trzynadlowski, 2023
 * 
 * Header file for web server.
 */

#pragma once
#ifndef INCLUDED_WEB_SERVER_HPP
#define INCLUDED_WEB_SERVER_HPP

#include "llava_request.hpp"
#include "cpp-httplib/httplib.h"
#include <string>

std::string escape_json(const std::string &s);
void run_web_server(
    const std::string host,
    int port,
    bool enable_logging,
    std::function<void(const llava_request &, httplib::Response &)> hand_off_request
);

#endif  // INCLUDED_WEB_SERVER_HPP
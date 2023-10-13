#pragma once
#ifndef INCLUDED_UTIL_COMMAND_LINE_HPP
#define INCLUDED_UTIL_COMMAND_LINE_HPP

#include "util/config.hpp"
#include <cstdint>

namespace util
{
    namespace command_line
    {
        struct parser_state
        {
            bool exit = false;
            bool parse_error = false;
        };

        struct parser_result
        {
            util::config::Node config;
            parser_state state;

            parser_result()
                : config("CommandLine")
            {
            }
        };

        struct option_definition;

        typedef uint8_t flags_t;

        enum Flags : flags_t
        {
            None = 0x00,
            Required = 0x01
        };

        namespace detail
        {
            struct parameter_definition
            {
                const std::string name;

                virtual bool validate(const std::string &/*option_name*/, const std::string &/*value*/, size_t /*parameter_num*/) const
                {
                    return false; // no error
                }

                parameter_definition(const std::string &parameter_name)
                    : name(parameter_name)
                {
                }
            };

            struct action
            {
                virtual void perform_action(
                    util::config::Node */*config*/,
                    const option_definition &/*option*/,
                    const std::string &/*values*/,
                    const std::vector<std::string> &/*value_list*/) const
                {
                }
            };

            // Don't use this directly! Always use a factory function.
            struct raw_option_definition
            {
                // std::string mode; // this option activates a mode
                // std::vector<std::string> valid_for_modes; // this option is valid only for these modes (if it is a mode activator, may only contain 'mode')
                // std::vector<std::string> required_for_modes;  // this option is *required* in the given modes

                std::vector<std::string> long_names;
                std::vector<std::string> short_names;
                std::vector<std::shared_ptr<parameter_definition>> parameters;
                char parameter_delimiter = ',';
                std::shared_ptr<action> if_found;
                std::shared_ptr<action> if_not_found;

                std::string config_key;
                std::string description;
                std::string default_values_description;

                flags_t flags;

                bool is_required() const
                {
                    return (flags & Required) != 0;
                }
            };
        } // detail

        using parameter_definition = detail::parameter_definition;

        struct option_definition : public detail::raw_option_definition
        {
            option_definition(const detail::raw_option_definition &definition)
                : detail::raw_option_definition(definition)
            {
            }
        };

        std::shared_ptr<parameter_definition> string(const std::string &name = "value");
        std::shared_ptr<parameter_definition> boolean(const std::string &name = "value");
        std::shared_ptr<parameter_definition> integer(const std::string &name = "value");
        std::shared_ptr<parameter_definition> integer(int64_t lower, int64_t upper);
        std::shared_ptr<parameter_definition> integer(const std::string &name, int64_t lower, int64_t upper);

        option_definition switch_option(
            const std::string &long_name,
            const std::string &config_key,
            const std::string &description,
            flags_t flags = None);

        option_definition switch_option(
            const std::vector<std::string> &long_names,
            const std::vector<std::string> &short_names,
            const std::string &config_key,
            const std::string &description,
            flags_t flags = None);

        // Intended to complement an existing switch_option; therefore has *no*
        // default. Use only with an appropriate switch_option also defined.
        option_definition complement_switch_option(
            const std::string &long_name,
            const std::string &config_key,
            const std::string &description,
            flags_t flags = None);

        option_definition valued_option(
            const std::string &long_name,
            const std::shared_ptr<parameter_definition> &parameter,
            const std::string &config_key,
            const std::string &description,
            flags_t flags = None);

        option_definition default_valued_option(
            const std::string &long_name,
            const std::shared_ptr<parameter_definition> &parameter,
            const std::string &default_value,
            const std::string &config_key,
            const std::string &description,
            flags_t flags = None);

        option_definition multivalued_option(
            const std::string &long_name,
            const std::vector<std::shared_ptr<parameter_definition>> &parameters,
            const std::string &config_key,
            const std::string &description,
            flags_t flags = None);

        option_definition default_multivalued_option(
            const std::string &long_name,
            const std::vector<std::shared_ptr<parameter_definition>> &parameters,
            const std::string &default_values,
            const std::string &config_key,
            const std::string &description,
            flags_t flags = None);

        parser_result parse_command_line(const std::vector<option_definition> &options, int argc, char **argv);
        parser_state parse_command_line(util::config::Node *config, const std::vector<option_definition> &options, int argc, char **argv);
        void show_help(const std::vector<option_definition> &options, char **argv);

    } // command_line
} // util

#endif // INCLUDED_UTIL_COMMAND_LINE_HPP
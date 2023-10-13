/*
 * Command Line Parser
 * ===================
 *
 * Behavioral conventions:
 *  - Valued options do not provide defaults unless explicitly requested.
 *  - Switch options (a special case of single-valued boolean options) *do*
 *    provide a default: they will be set to false if not present.
 */

#include "util/command_line.hpp"
#include "util/logging.hpp"
#include "util/format.hpp"
#include "util/config.hpp"

#include <limits>
#include <memory>
#include <vector>
#include <map>
#include <set>
#include <sstream>
#include <algorithm>
#include <filesystem>

namespace util
{
    namespace command_line
    {
        //
        // Parameter type definitions
        //

        struct string_parameter_definition : public detail::parameter_definition
        {
            string_parameter_definition(const std::string &parameter_name)
                : detail::parameter_definition(parameter_name)
            {
            }
        };

        struct boolean_parameter_definition : public detail::parameter_definition
        {
            // Test whether supplied parameter definition is of this type
            static bool is_boolean(const std::shared_ptr<parameter_definition> &parameter_definition)
            {
                return dynamic_cast<boolean_parameter_definition *>(parameter_definition.get()) != nullptr;
            }

            virtual bool validate(const std::string &option_name, const std::string &value, size_t parameter_num) const override
            {
                // Check for boolean strings compatible with the config system
                std::string s = util::to_lower(value);
                bool valid = (s == "true") || (s == "false") || (s == "yes") || (s == "no") || (s == "on") || (s == "off") || (s == "1") || (s == "0");
                if (!valid)
                {
                    LOG_ERROR("Argument " << parameter_num << " to '" << option_name << "' must be a boolean value ('true' or 'false').");
                }
                return !valid;
            }

            boolean_parameter_definition(const std::string &parameter_name)
                : detail::parameter_definition(parameter_name)
            {
            }
        };

        struct integer_parameter_definition : public detail::parameter_definition
        {
            bool validate(const std::string &option_name, const std::string &value, size_t parameter_num) const override
            {
                int64_t v;
                bool not_an_integer = false;
                std::istringstream is(value);
                not_an_integer = !(is >> v);
                bool out_of_bounds = bounds_check_required ? !(v >= lower_bound && v <= upper_bound) : false;
                if (out_of_bounds)
                {
                    LOG_ERROR("Argument " << parameter_num << " to '" << option_name << "' must be an integer within range [" << lower_bound << "," << upper_bound << "].");
                    return true;
                }
                else if (not_an_integer)
                {
                    LOG_ERROR("Argument " << parameter_num << " to '" << option_name << "' must be an integer.");
                    return true;
                }
                return false;
            }

            const int64_t lower_bound = std::numeric_limits<int64_t>::lowest();
            const int64_t upper_bound = std::numeric_limits<int64_t>::max();
            const bool bounds_check_required = false;

            integer_parameter_definition(const std::string &parameter_name)
                : detail::parameter_definition(parameter_name)
            {
            }

            integer_parameter_definition(const std::string &parameter_name, int64_t lower, int64_t upper)
                : detail::parameter_definition(parameter_name),
                  lower_bound(lower),
                  upper_bound(upper),
                  bounds_check_required(true)
            {
                if (lower > upper)
                {
                    int64_t tmp = lower;
                    lower = upper;
                    upper = tmp;
                }
            }
        };

        //
        // Parameter type definition emitters
        //

        std::shared_ptr<parameter_definition> string(const std::string &name)
        {
            return std::make_shared<string_parameter_definition>(name);
        }

        std::shared_ptr<parameter_definition> boolean(const std::string &name)
        {
            return std::make_shared<boolean_parameter_definition>(name);
        }

        std::shared_ptr<parameter_definition> integer(const std::string &name)
        {
            return std::make_shared<integer_parameter_definition>(name);
        }

        std::shared_ptr<parameter_definition> integer(int64_t lower, int64_t upper)
        {
            return std::make_shared<integer_parameter_definition>("value", lower, upper);
        }

        std::shared_ptr<parameter_definition> integer(const std::string &name, int64_t lower, int64_t upper)
        {
            return std::make_shared<integer_parameter_definition>(name, lower, upper);
        }

        //
        // Actions (called when options are found or not found, allowing config
        // tree to be manipulated appropriately for the option type)
        //

        struct store_values_action : public detail::action
        {
            virtual void perform_action(
                util::config::Node *config,
                const option_definition &option,
                const std::string &values,
                const std::vector<std::string> &value_list) const override
            {
                // Top-level node set to value as-is, unparsed
                config->Set(option.config_key, values);

                // Important: remove any existing child nodes (created by a default
                // action) so we don't end up just adding duplicates
                config->Get(option.config_key).RemoveChildren();

                // List of values is treated by creating sub-nodes for each parameter
                if (value_list.size() == option.parameters.size())
                {
                    for (size_t i = 0; i < value_list.size(); i++)
                    {
                        config->Get(option.config_key).Add(option.parameters[i]->name, value_list[i]);
                    }
                }
            }
        };

        struct store_constant_values_action : public detail::action
        {
            virtual void perform_action(
                util::config::Node *config,
                const option_definition &option,
                const std::string &values,
                const std::vector<std::string> &value_list) const override
            {
                std::vector<std::string> constant_value_list = util::format(m_constant_values).split(option.parameter_delimiter);
                store_values_action store_values;
                store_values.perform_action(config, option, m_constant_values, constant_value_list);
            }

            store_constant_values_action(const std::string &values)
                : m_constant_values(values)
            {
            }

        private:
            const std::string m_constant_values;
        };

        struct store_inverse_bool_action : public detail::action
        {
            virtual void perform_action(
                util::config::Node *config,
                const option_definition &option,
                const std::string &values,
                const std::vector<std::string> &value_list) const override
            {
                if (value_list.size() > 1)
                {
                    throw std::logic_error(util::format() << "store_inverse_bool_action can only be used with options taking a single parameter.");
                }

                // Store the value given on command line
                store_values_action store_values;
                std::string inverted_value;
                inverted_value = invert_value(values);
                store_values.perform_action(config, option, inverted_value, {inverted_value});
            }

        private:
            std::string invert_value(const std::string &value) const
            {
                bool inverted_bool = !util::parse_bool(value);
                return inverted_bool ? "true" : "false";
            }
        };

        //
        // Action emitters
        //

        static std::shared_ptr<detail::action> do_nothing()
        {
            return std::make_shared<detail::action>();
        }

        static std::shared_ptr<detail::action> store_constants(const std::string &values)
        {
            return std::make_shared<store_constant_values_action>(values);
        }

        static std::shared_ptr<detail::action> store_values()
        {
            return std::make_shared<store_values_action>();
        }

        static std::shared_ptr<detail::action> store_inverse_bool()
        {
            return std::make_shared<store_inverse_bool_action>();
        }

        //
        // Option definition emitters
        //

        option_definition switch_option(
            const std::string &long_name,
            const std::string &config_key,
            const std::string &description,
            flags_t flags)
        {
            return {{
                {long_name},              // long_names
                {},                       // short names
                {boolean()},              // parameters
                ',',                      // parameter_delimiter
                store_values(),           // if_found
                store_constants("false"), // if_not_found
                config_key,               // config_key
                description,              // description
                "",                       // default values description
                flags                     // flags
            }};
        }

        option_definition switch_option(
            const std::vector<std::string> &long_names,
            const std::vector<std::string> &short_names,
            const std::string &config_key,
            const std::string &description,
            flags_t flags)
        {
            return {{
                long_names,               // long_names
                short_names,              // short names
                {boolean()},              // parameters
                ',',                      // parameter_delimiter
                store_values(),           // if_found
                store_constants("false"), // if_not_found
                config_key,               // config_key
                description,              // description
                "",                       // default values description
                flags                     // flags
            }};
        }

        // Intended to complement an existing switch_option; therefore has *no*
        // default.
        option_definition complement_switch_option(
            const std::string &long_name,
            const std::string &config_key,
            const std::string &description,
            flags_t flags)
        {
            return {{
                {long_name},          // long_names
                {},                   // short names
                {boolean()},          // parameters
                ',',                  // parameter_delimiter
                store_inverse_bool(), // if_found (store inverse)
                do_nothing(),         // if_not_found
                config_key,           // config_key
                description,          // description
                "",                   // default values description
                flags                 // flags
            }};
        }

        option_definition valued_option(
            const std::string &long_name,
            const std::shared_ptr<parameter_definition> &parameter,
            const std::string &config_key,
            const std::string &description,
            flags_t flags)
        {
            return {{
                {long_name},    // long_names
                {},             // short names
                {parameter},    // parameters
                ',',            // parameter_delimiter
                store_values(), // if_found
                do_nothing(),   // if_not_found
                config_key,     // config_key
                description,    // description
                "",             // default values description
                flags           // flags
            }};
        }

        option_definition default_valued_option(
            const std::string &long_name,
            const std::shared_ptr<parameter_definition> &parameter,
            const std::string &default_value,
            const std::string &config_key,
            const std::string &description,
            flags_t flags)
        {
            return {{
                {long_name},                    // long_names
                {},                             // short names
                {parameter},                    // parameters
                ',',                            // parameter_delimiter
                store_values(),                 // if_found
                store_constants(default_value), // if_not_found
                config_key,                     // config_key
                description,                    // description
                default_value,                  // default values description
                flags                           // flags
            }};
        }

        option_definition multivalued_option(
            const std::string &long_name,
            const std::vector<std::shared_ptr<parameter_definition>> &parameters,
            const std::string &config_key,
            const std::string &description,
            flags_t flags)
        {
            return {{
                {long_name},    // long_names
                {},             // short names
                parameters,     // parameters
                ',',            // parameter_delimiter
                store_values(), // if_found
                do_nothing(),   // if_not_found
                config_key,     // config_key
                description,    // description
                "",             // default values description
                flags           // flags
            }};
        }

        option_definition default_multivalued_option(
            const std::string &long_name,
            const std::vector<std::shared_ptr<parameter_definition>> &parameters,
            const std::string &default_values,
            const std::string &config_key,
            const std::string &description,
            flags_t flags)
        {
            return {{
                {long_name},                     // long_names
                {},                              // short names
                parameters,                      // parameters
                ',',                             // parameter_delimiter
                store_values(),                  // if_found
                store_constants(default_values), // if_not_found
                config_key,                      // config_key
                description,                     // description
                default_values,                  // default values description
                flags                            // flags
            }};
        }

        //
        // Functions for validation of option definitions
        //

        static bool validate_unique_names(const std::vector<option_definition> &options)
        {
            std::map<std::string, size_t> num_times_used;
            for (auto &option : options)
            {
                for (auto &name : option.long_names)
                {
                    num_times_used[name] += 1;
                }
                for (auto &name : option.short_names)
                {
                    num_times_used[name] += 1;
                }
            }

            bool error = false;
            for (auto &v : num_times_used)
            {
                if (v.second > 1)
                {
                    error = true;
                    LOG_ERROR("Option name used multiple times: " << v.first);
                }
            }

            return error;
        }

        static size_t validate_names(bool *error, const std::vector<std::string> &names)
        {
            size_t num_names = 0;
            for (auto &name : names)
            {
                num_names += (name.length() > 0) ? 1 : 0;
                if (name.find('=') != std::string::npos)
                {
                    *error = true;
                    LOG_ERROR("Option " << name << " contains forbidden character '='.");
                }
            }
            return num_names;
        }

        static bool validate_has_name(const std::vector<option_definition> &options)
        {
            bool error = false;
            size_t idx = 0;
            for (auto option : options)
            {
                ++idx;
                size_t num_long_names = validate_names(&error, option.long_names);
                if (num_long_names == 0)
                {
                    error = true;
                    LOG_ERROR("Option " << idx << " must have at least one long name.");
                }
                validate_names(&error, option.short_names);
            }
            return error;
        }

        static void validate_definition(const std::vector<option_definition> &options)
        {
            bool parse_errors = false;
            parse_errors |= validate_unique_names(options);
            parse_errors |= validate_has_name(options);
            if (parse_errors)
            {
                throw std::logic_error(util::format() << "Ill-specified command line options. Unable to parse. Fix and recompile.");
            }
        }

        //
        // Command line parsing
        //

        static void store_defaults(util::config::Node *config, const std::vector<option_definition> &options)
        {
            for (auto &option : options)
            {
                option.if_not_found->perform_action(config, option, "", std::vector<std::string>());
            }
        }

        static void extract_name_and_values(std::string *name, bool *separator_present, std::string *values, const std::string &arg)
        {
            // Extract name and values (if any)
            size_t idx_equals = arg.find_first_of('=');
            if (idx_equals != std::string::npos)
            {
                *name = std::string(arg.begin(), arg.begin() + idx_equals);
                *values = std::string(arg.begin() + idx_equals + 1, arg.end());
                *separator_present = true;
            }
            else
            {
                *name = arg;
                values->clear();
                *separator_present = false;
            }
        }

        static bool matches(const std::string &candidate, const std::vector<std::string> &values)
        {
            for (auto &value : values)
            {
                if (value == candidate)
                {
                    return true;
                }
            }
            return false;
        }

        static bool validate_option_parameters(const option_definition &option, const std::string &name, const std::vector<std::string> &value_list)
        {
            if (option.parameters.size() != value_list.size())
            {
                if (option.parameters.size() == 1)
                {
                    LOG_ERROR("'" << name << "' expects a parameter but none was given.");
                }
                else
                {
                    std::string were_given = value_list.size() == 1 ? " was given" : " were given";
                    LOG_ERROR("'" << name << "' expects " << option.parameters.size() << " parameters but " << value_list.size() << were_given << '.');
                }
                return true;
            }

            bool error = false;
            for (size_t i = 0; i < option.parameters.size(); i++)
            {
                error |= option.parameters[i]->validate(name, value_list[i], i + 1);
            }

            return error;
        }

        static size_t count_required_options(const std::vector<option_definition> &options)
        {
            size_t n = 0;
            for (auto &option : options)
            {
                n += option.is_required() ? 1 : 0;
            }
            return n;
        }

        static bool validate_required_options_found(const std::vector<option_definition> &options, const std::set<size_t> &options_found)
        {
            bool error = false;
            for (size_t j = 0; j < options.size(); j++)
            {
                auto &option = options[j];
                if (option.is_required() && options_found.count(j) == 0)
                {
                    LOG_ERROR("Missing required option: " << option.long_names[0]);
                    error = true;
                }
            }
            return error;
        }

        // Special case: switch option. Boolean options with a single parameter can
        // be given as --option=<bool> or simply as a switch: --option. In the
        // latter case, the if_found action should set the appropriate bool value
        // (usually true, except for complement options). This requires the option
        // definition to be carefully constructed for boolean options, ensuring
        // store_value_with_default() is used.
        static bool is_switch(const option_definition &option)
        {
            return option.parameters.size() == 1 && boolean_parameter_definition::is_boolean(option.parameters[0]);
        }

        parser_result parse_command_line(const std::vector<option_definition> &options, int argc, char **argv)
        {
            parser_result result;
            result.state = parse_command_line(&result.config, options, argc, argv);
            return result;
        }

        parser_state parse_command_line(util::config::Node *config, const std::vector<option_definition> &options, int argc, char **argv)
        {
            validate_definition(options);
            if (argc == 1 && count_required_options(options) > 0)
            {
                show_help(options, argv);
                return {true, true}; // parse error because required options are missing
            }

            store_defaults(config, options);

            std::set<size_t> options_found;
            bool parse_error = false;
            for (int i = 1; i < argc; i++)
            {
                std::string name;
                std::string values;
                bool separator_present = false;
                extract_name_and_values(&name, &separator_present, &values, argv[i]);

                bool found_option = false;
                for (size_t j = 0; j < options.size(); j++)
                {
                    auto &option = options[j];

                    if (matches(name, option.long_names) || matches(name, option.short_names))
                    {
                        bool parse_error_this_option = false;

                        std::vector<std::string> value_list;

                        if (!values.empty())
                        {
                            bool expects_single_value = option.parameters.size() == 1;
                            bool expects_value_list = option.parameters.size() > 1;

                            if (expects_single_value)
                            {
                                value_list.push_back(values);
                            }
                            else if (expects_value_list)
                            {
                                value_list = util::format(values).split(option.parameter_delimiter);
                            }
                        }

                        if (values.empty() && !separator_present && is_switch(option))
                        {
                            // Skip validation, which would flag an error due to "missing"
                            // bool param. Forcibly insert a "true" value, so that --option
                            // is equivalent to --option=true.
                            values = "true";
                            value_list.push_back("true");
                        }
                        else
                        {
                            parse_error_this_option |= validate_option_parameters(option, name, value_list);
                        }

                        if (!parse_error_this_option)
                        {
                            option.if_found->perform_action(config, option, values, value_list);
                        }

                        parse_error |= parse_error_this_option;
                        found_option = true;
                        options_found.insert(j);
                        break;
                    }
                }

                if (!found_option)
                {
                    LOG_ERROR("Invalid option: " << name);
                    parse_error = true;
                }
            }

            // Print help if required and return results
            bool should_exit = parse_error;
            bool print_help = (*config)["ShowHelp"].ValueAsDefault<bool>(false);
            if (print_help)
            {
                should_exit = true;
                show_help(options, argv);
            }
            else
            {
                // If help was requested, omitting required options is not an error
                parse_error |= validate_required_options_found(options, options_found);
            }

            parser_state state = {should_exit || parse_error, parse_error};
            return state;
        }

        //
        // Print help text
        //

        static std::string program_name(char **argv)
        {
            return std::filesystem::path(argv[0]).stem().string();
        }

        static std::string syntax_description(const std::string &name, const option_definition &option)
        {
            if (option.parameters.size() == 0)
            {
                return name;
            }
            std::vector<std::string> parameter_syntax;
            for (auto &parameter : option.parameters)
            {
                parameter_syntax.push_back(util::format() << '<' << util::to_lower(parameter->name) << '>');
            }
            return util::format() << name << '=' << util::format(",").join(parameter_syntax);
        }

        static std::map<std::string, std::string> build_option_name_to_syntax_map(const std::vector<option_definition> &options, size_t tab_stop)
        {
            std::map<std::string, std::string> m;

            util::tab_expander t(tab_stop);
            std::string one_tab = "\t";
            std::string two_tabs = "\t\t";

            for (auto &option : options)
            {
                // Print complete syntax only for the primary name
                const std::string &primary_name = option.long_names[0];
                if (is_switch(option))
                {
                    // Switches described as --option rather than --option=<value>
                    m[primary_name] = t.expand(one_tab + primary_name + one_tab);
                }
                else
                {
                    m[primary_name] = t.expand(one_tab + syntax_description(primary_name, option) + one_tab);
                }

                // Omit the parameters for all other names and add an indent
                for (size_t i = 1; i < option.long_names.size(); i++)
                {
                    m[option.long_names[i]] = t.expand(two_tabs + option.long_names[i] + one_tab);
                }
                for (auto &short_name : option.short_names)
                {
                    m[short_name] = t.expand(two_tabs + short_name + one_tab);
                }
            }

            return m;
        }

        template <typename Key, typename Value>
        static Value find_longest_value(const std::map<Key, Value> &m)
        {
            size_t longest_count = 0;
            Value longest_value;
            for (auto &v : m)
            {
                if (v.second.size() > longest_count)
                {
                    longest_count = v.second.size();
                    longest_value = v.second;
                }
            }
            return longest_value;
        }

        // Gets only the primary names
        static std::vector<std::string> get_required_option_names(const std::vector<option_definition> &options)
        {
            std::vector<std::string> required_options;
            for (auto &option : options)
            {
                if (option.is_required())
                {
                    required_options.push_back(option.long_names[0]);
                }
            }
            return required_options;
        }

        static void print_usage(const std::vector<option_definition> &options, char **argv, std::map<std::string, std::string> &name_to_syntax, size_t display_columns)
        {
            // Generate usage syntax: program_name --required-option-1=<value> --required_option-2=<value> [options]
            std::vector<std::string> required_option_names = get_required_option_names(options);
            std::vector<std::string> parts;
            parts.push_back(program_name(argv));
            for (auto &name : required_option_names)
            {
                // Remember to trim the whitespace that was added by names -> syntax
                // function!
                parts.push_back(util::trim_whitespace(name_to_syntax[name]));
            }
            if (required_option_names.size() < options.size())
            {
                parts.push_back("[options]");
            }
            std::string usage_syntax = util::format(" ").join(parts);

            // Break up into as many lines as needed
            size_t column = strlen("Usage: ");
            size_t columns = display_columns - column;
            util::word_wrapper w(columns);
            std::vector<std::string> lines = w.wrap_words(usage_syntax);

            // There will always be a first line
            std::cout << "Usage: " << lines[0] << std::endl;

            // Remaining lines
            std::string padding(column, ' ');
            for (size_t i = 1; i < lines.size(); i++)
            {
                std::cout << padding << lines[i] << std::endl;
            }
        }

        void show_help(const std::vector<option_definition> &options, char **argv)
        {
            validate_definition(options);

            size_t display_columns = 80; // including newline
            size_t tab_stop = 2;
            size_t description_min_columns = (80 - 36); // minimum columns required

            // Generate syntax descriptions for options and find widest one
            auto name_to_syntax = build_option_name_to_syntax_map(options, tab_stop);
            std::string widest_syntax = find_longest_value(name_to_syntax);

            print_usage(options, argv, name_to_syntax, display_columns);
            if (options.empty())
            {
                return;
            }

            std::cout << std::endl
                      << "Options:" << std::endl;

            // Compute starting column and width for descriptions
            size_t columns_available = widest_syntax.length() > display_columns ? 0 : (display_columns - widest_syntax.length());
            size_t description_start_column = columns_available < description_min_columns ? (display_columns - description_min_columns) : widest_syntax.length();
            size_t description_columns = 80 - description_start_column;

            // Print out each option (syntax and description)
            util::word_wrapper w(description_columns);
            for (auto &option : options)
            {
                // Description including, if applicable, default value
                std::string defaults;
                if (option.default_values_description.length() > 0)
                {
                    defaults = util::format() << "[Default: " << option.default_values_description << ']';
                }

                // Break up description (without defaults) into lines
                std::vector<std::string> description_lines = w.wrap_words(option.description);

                // If last description line has space, append default values. Else, put
                // them on their own line.
                if (description_lines.size() == 0)
                {
                    description_lines.emplace_back(defaults);
                }
                else
                {
                    // Compute length of last description if we append the defaults
                    // (accounting for space and newline)
                    size_t last_idx = description_lines.size() - 1;
                    size_t length_with_defaults = description_lines[last_idx].length() + 1 + defaults.length() + 1;
                    if (length_with_defaults >= description_columns)
                    {
                        // Too long, need own line
                        description_lines.emplace_back(defaults);
                    }
                    else
                    {
                        // Okay to append
                        description_lines[last_idx] += std::string(" ") + defaults;
                    }
                }

                // Get all option names
                std::vector<std::string> names(option.long_names);
                names.insert(names.end(), option.short_names.begin(), option.short_names.end());

                // Number of lines to print
                size_t num_lines = std::max(description_lines.size(), names.size());

                // Print the lines
                for (size_t i = 0; i < num_lines; i++)
                {
                    bool print_name = i < names.size();
                    bool print_description = i < description_lines.size();
                    size_t column = 0; // keep track of column

                    // Try printing option syntax first
                    if (print_name)
                    {
                        std::string syntax = name_to_syntax[names[i]];
                        std::cout << syntax;
                        column += syntax.length();
                    }

                    // Then the description at the appropriate column
                    if (print_description)
                    {
                        if (column > description_start_column)
                        {
                            // Syntax description is too wide. Need to start new line for
                            // description.
                            std::cout << std::endl;
                            column = 0;
                        }

                        if (column < description_start_column)
                        {
                            // Need padding
                            std::string padding(description_start_column - column, ' ');
                            std::cout << padding;
                        }

                        std::cout << description_lines[i];
                    }

                    std::cout << std::endl;
                }
            }
        }
    } // command_line
} // util

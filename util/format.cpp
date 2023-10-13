#include "util/format.hpp"
#include <algorithm>
#include <cctype>
#include <cassert>

namespace util
{
  std::ostream &operator<<(std::ostream &os, const format &format)
  {
    format.write(os);
    return os;
  }

  std::string tab_expander::expand(const std::string &str) const
  {
    size_t num_tabs = std::count(str.begin(), str.end(), '\t');
    std::string e;
    e.reserve(str.length() + m_tab_stop * num_tabs);
    size_t column = 0;
    for (char c: str)
    {
      if (c == '\t')
      {
        size_t next_tab = (column + m_tab_stop) - ((column + m_tab_stop) % m_tab_stop);
        while (column < next_tab)
        {
          e += ' ';
          column++;
        }
      }
      else
      {
        e += c;
        column++;
        if (c == '\n')
        {
          column = 0;
        }
      }
    }
    return e;
  }

  word_wrapper::word_wrapper(size_t columns)
    : m_columns(columns < 2 ? 2 : columns)
  {
    assert(columns >= 2);
  }

  std::vector<std::string> word_wrapper::wrap_words(const std::string &str) const
  {
    std::vector<std::string> lines = util::format(str).split('\n');
    std::vector<std::string> out;
    for (auto &line: lines)
    {
      wrap_line(&out, line);
    }
    return out;
  }

  void word_wrapper::wrap_line(std::vector<std::string> *out, const std::string &s) const
  {
    size_t max_column = m_columns - 1;  // need to allow room for implicit \n
    size_t line_start = 0;
    size_t column = 0;
    size_t last_space = std::string::npos;
    for (size_t i = 0; i < s.length(); )
    {
      if (isspace(s[i]))
      {
        last_space = i;
      }

      if (column == max_column)
      {
        // Final allowed column reached. Trim at last space or, if none,
        // hard stop here.
        size_t line_end = last_space == std::string::npos ? i : gobble_trailing_whitespace(s, last_space);
        out->emplace_back(s, line_start, line_end - line_start);

        // Begin a new line
        line_start = gobble_leading_whitespace(s, line_end);
        i = line_start;
        column = 0;
        last_space = std::string::npos;
      }
      else
      {
        column++;
        i++;
      }
    }
    out->emplace_back(s, line_start, s.length() - line_start);
  }

  size_t word_wrapper::gobble_trailing_whitespace(const std::string &s, size_t end_idx) const
  {
    while (end_idx > 0 && isspace(s[end_idx - 1]))
    {
      end_idx--;
    }
    return end_idx;
  }

  size_t word_wrapper::gobble_leading_whitespace(const std::string &s, size_t start_idx) const
  {
    while (start_idx < s.length() && isspace(s[start_idx]))
    {
      start_idx++;
    }
    return start_idx;
  }

  std::string to_lower(const std::string &str)
  {
    std::string tmp(str);
    std::transform(tmp.begin(), tmp.end(), tmp.begin(), ::tolower);
    return tmp;
  }

  std::string trim_whitespace(const std::string &str)
  {
    if (str.empty())
      return std::string();
    size_t first = 0;
    for (; first < str.length(); ++first)
    {
      if (!isspace(str[first]))
        break;
    }
    if (first >= str.length())
      return std::string();
    size_t last = str.length() - 1;
    for (; last > first; --last)
    {
      if (!isspace(str[last]))
        break;
    }
    ++last;
    return std::string(str.c_str() + first, last - first);
  }

  bool parse_bool(const std::string &str)
  {
      if (!util::stricmp(str.c_str(), "true") || !util::stricmp(str.c_str(), "on") || !util::stricmp(str.c_str(), "yes"))
        return true;
      if (!util::stricmp(str.c_str(), "false") || !util::stricmp(str.c_str(), "off") || !util::stricmp(str.c_str(), "no"))
        return false;
      bool tmp;
      std::stringstream ss;
      ss << str;
      ss >> tmp;
      return tmp;
  }

  static const char hex_digits[] = { '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f' };

  std::string hex(uint64_t n, size_t num_digits)
  {
    util::format f;
    f << "0x";
    for (size_t b = num_digits * 4; b; )
    {
      b -= 4;
      f << hex_digits[(n >> b) & 0xf];
    }
    return f;
  }

  std::string hex(uint64_t n)
  {
    return hex(n, 16);
  }

  std::string hex(uint32_t n)
  {
    return hex(n, 8);
  }

  std::string hex(uint16_t n)
  {
    return hex(n, 4);
  }

  std::string hex(uint8_t n)
  {
    return hex(n, 2);
  }

  int stricmp(const char *s1, const char *s2)
  {
    int cmp;
    char c1;
    char c2;
    do
    {
      c1 = *s1++;
      c2 = *s2++;
      cmp = unsigned(tolower(c1)) - unsigned(tolower(c2));
    } while ((cmp == 0) && (c1 != '\0') && (c2 != '\0'));
    return cmp;
  }
} // util

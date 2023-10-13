#pragma once
#ifndef INCLUDED_FORMAT_HPP
#define INCLUDED_FORMAT_HPP

#include <string>
#include <sstream>
#include <iomanip>
#include <cstdint>
#include <vector>

namespace util
{
  class format
  {
  public:
    template <typename T>
    format &operator<<(const T &data)
    {
      m_stream << data;
      return *this;
    }

    operator std::string() const
    {
      return str();
    }

    std::string str() const
    {
      return m_stream.str();
    }

    void write(std::ostream &os) const
    {
      if (m_stream.rdbuf()->in_avail())
        os << m_stream.rdbuf();
    }

    template <typename T>
    format &join(const T &collection)
    {
      std::string separator = m_stream.str();
      clear();
      for (auto it = collection.begin(); it != collection.end(); )
      {
        m_stream << *it;
        ++it;
        if (it != collection.end())
          m_stream << separator;
      }
      return *this;
    }

    std::vector<std::string> split(char separator)
    {
      // Very inefficient: lots of intermediate string copies!
      std::string str = m_stream.str();
      const char *start = str.c_str();
      const char *end = start;
      std::vector<std::string> parts;
      do
      {
        if (*end == separator || !*end)
        {
          size_t len = end - start;
          if (len)
            parts.emplace_back(start, len);
          else
            parts.emplace_back();
          start = end + 1;
        }
        ++end;
      } while (end[-1]);
      return parts;
    }

    format(const std::string &str)
      : m_stream(str)
    {
    }

    format()
    {
    }
  private:
    //TODO: write own buffer implementation to more easily support ToLower() et
    //      al as methods of Util::Format
    std::stringstream m_stream;

    void clear()
    {
      m_stream.str(std::string());
    }
  };

  class tab_expander
  {
  public:
    tab_expander(size_t tab_stop)
      : m_tab_stop(tab_stop)
    {
    }

    std::string expand(const std::string &str) const;

  private:
    const size_t m_tab_stop;
  };

  class word_wrapper
  {
  public:
    word_wrapper(size_t columns);

    // Note: does not account for or perform tab expansion
    std::vector<std::string> wrap_words(const std::string &str) const;

  private:
    const size_t m_columns;

    void wrap_line(std::vector<std::string> *out, const std::string &s) const;
    size_t gobble_trailing_whitespace(const std::string &s, size_t end_idx) const;
    size_t gobble_leading_whitespace(const std::string &s, size_t start_idx) const;
  };

  std::ostream &operator<<(std::ostream &os, const format &format);
  std::string to_lower(const std::string &str);
  std::string trim_whitespace(const std::string &str);
  bool parse_bool(const std::string &str);
  std::string hex(uint64_t n, size_t num_digits);
  std::string hex(uint64_t n);
  std::string hex(uint32_t n);
  std::string hex(uint16_t n);
  std::string hex(uint8_t n);

  int stricmp(const char *s1, const char *s2);
} // util


#endif  // INCLUDED_FORMAT_HPP

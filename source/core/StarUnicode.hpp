#pragma once

#include "StarByteArray.hpp"
#include "StarMaybe.hpp"

namespace Star {

STAR_EXCEPTION(UnicodeException, StarException);

typedef char Utf8Type;
typedef char32_t Utf32Type;

#define STAR_UTF32_REPLACEMENT_CHAR 0x0000fffdL

// Returns false if the given string contains invalid characters.
bool isValidUtf8(Utf8Type const* utf8, size_t size = NPos);
// If passed NPos as a size, assumes modified UTF-8 and stops on NULL byte.
// Otherwise, ignores NULL.
size_t utf8Length(Utf8Type const* utf8, size_t size = NPos);
// Encode up to six utf8 bytes into a utf32 character.  If passed NPos as len,
// assumes modified UTF-8 and stops on NULL, otherwise ignores.
size_t utf8DecodeChar(Utf8Type const* utf8, Utf32Type* utf32, size_t len = NPos);
// Encode single utf32 char into up to 6 utf8 characters.
size_t utf8EncodeChar(Utf8Type* utf8, Utf32Type utf32, size_t len = 6);

Utf32Type hexStringToUtf32(std::string const& codepoint, Maybe<Utf32Type> previousCodepoint = {});
std::string hexStringFromUtf32(Utf32Type character);

bool isUtf16LeadSurrogate(Utf32Type codepoint);
bool isUtf16TrailSurrogate(Utf32Type codepoint);
inline bool isUtf16Surrogate(Utf32Type codepoint) {
  return codepoint >= 0xd800 && codepoint <= 0xdfff;
};

Utf32Type utf32FromUtf16SurrogatePair(Utf32Type lead, Utf32Type trail);
pair<Utf32Type, Maybe<Utf32Type>> utf32ToUtf16SurrogatePair(Utf32Type codepoint);

// Bidirectional iterator that can make utf8 appear as utf32
// Requires a begin and end specified (to prevent out of bounds reads with invalid utf8)
template <class BaseIterator, class U32Type = Utf32Type>
class U8ToU32Iterator {
public:
  typedef ptrdiff_t difference_type;
  typedef U32Type value_type;
  typedef U32Type* pointer;
  typedef U32Type& reference;
  typedef std::bidirectional_iterator_tag iterator_category;

  U8ToU32Iterator() : m_position(), m_value(pending_read), m_begin(), m_end() {}

  U8ToU32Iterator(BaseIterator b, BaseIterator begin, BaseIterator end) : m_position(b), m_value(pending_read), m_begin(begin), m_end(end) {}

  U8ToU32Iterator(BaseIterator b, U8ToU32Iterator base) : m_position(b), m_value(pending_read), m_begin(base.m_begin), m_end(base.m_end) {}

  BaseIterator const& base() const {
    return m_position;
  }

  U32Type const& operator*() const {
    if (m_value == pending_read)
      extract_current();
    return m_value;
  }

  U8ToU32Iterator const& operator++() {
    increment();
    return *this;
  }

  U8ToU32Iterator operator++(int) {
    U8ToU32Iterator clone(*this);
    increment();
    return clone;
  }

  U8ToU32Iterator const& operator--() {
    decrement();
    return *this;
  }

  U8ToU32Iterator operator--(int) {
    U8ToU32Iterator clone(*this);
    decrement();
    return clone;
  }

  bool operator==(U8ToU32Iterator const& that) const {
    return equal(that);
  }

  bool operator!=(U8ToU32Iterator const& that) const {
    return !equal(that);
  }

private:
  // special values for pending iterator reads:
  static U32Type const pending_read = 0xffffffffu;

  static unsigned utf8_byte_count(Utf8Type c) {
    // if the most significant bit with a zero in it is in position
    // 8-N then there are N bytes in this UTF-8 sequence:
    uint8_t mask = 0x80u;
    unsigned result = 0;
    while (c & mask) {
      ++result;
      mask >>= 1;
    }
    return (result == 0) ? 1 : ((result > 4) ? 4 : result);
  }

  static unsigned utf8_trailing_byte_count(Utf8Type c) {
    return utf8_byte_count(c) - 1;
  }

  void increment() {
    // skip high surrogate first if there is one:
    m_value = pending_read;
    unsigned c = utf8_byte_count(*m_position);
    if (c > 1) {
      // ensure the sequence is valid (invalid sequences are treated as individual characters) by decoding the entire character
      // if it's invalid, just increment once to the next character
      BaseIterator next(m_position);
      uint32_t value = *m_position;
      auto extra = c - 1;
      for (unsigned i = 0; i < extra; i++) {
        ++next;
        if (next == m_end) {
          ++m_position;
          return;
        }
        value <<= 6;
        auto entry = static_cast<uint8_t>(*next);
        if ((entry & 0xC0u) != 0x80u) {
          // this should be a continuation but isn't.
          ++m_position;
          return;
        }
        value += entry & 0x3Fu;
      }
      // we now need to remove a few of the leftmost bits, but how many depends
      // upon how many extra bytes we've extracted:
      static const uint32_t masks[4] = {
        0x7Fu, 0x7FFu, 0xFFFFu, 0x1FFFFFu
      };
      value &= masks[extra];

      static const uint32_t overlong[4] = {
        0x00u, 0x80u, 0x800u, 0x10000u
      };
      if (value > (uint32_t)0x10FFFFu || value < overlong[extra] || isUtf16Surrogate(value)) {
        // This is overlong, invalid, or a UTF16 surrogate
        ++m_position;
        return;
      }
      // if it's valid, go to the character after it (may equal m_end)
      m_position = ++next;
    } else {
      ++m_position;
    }
  }

  void decrement() {
    // Keep backtracking until we don't have a trailing character:
    unsigned count = 0;
    BaseIterator previous(m_position);
    while (((uint8_t)*--previous & 0xC0u) == 0x80u && previous != m_begin)
      ++count;
    // now check that the sequence was valid:
    if (count != utf8_trailing_byte_count(*previous)) {
      m_position--;
      m_value = pending_read;
      return;
    }
    m_value = pending_read;
    m_position = previous;
  }

  bool equal(const U8ToU32Iterator& that) const {
    return m_position == that.m_position;
  }

  void extract_current() const {
    m_value = static_cast<Utf8Type>(*m_position);
    // we must not have a continuation character:
    if (((uint8_t)m_value & 0xC0u) == 0x80u) {
      m_value = STAR_UTF32_REPLACEMENT_CHAR;
      return;
    }
    // see how many extra byts we have:
    unsigned extra = utf8_trailing_byte_count(*m_position);
    // extract the extra bits, 6 from each extra byte:
    BaseIterator next(m_position);
    for (unsigned c = 0; c < extra; ++c) {
      ++next;
      if (next == m_end) {
        m_value = STAR_UTF32_REPLACEMENT_CHAR;
        return;
      }
      m_value <<= 6;
      auto entry = static_cast<uint8_t>(*next);
      if ((entry & 0xC0u) != 0x80u) {
        // Not a continuation char
        m_value = STAR_UTF32_REPLACEMENT_CHAR;
        return;
      }
      m_value += entry & 0x3Fu;
    }
    // we now need to remove a few of the leftmost bits, but how many depends
    // upon how many extra bytes we've extracted:
    static const Utf32Type masks[4] = {
      0x7Fu, 0x7FFu, 0xFFFFu, 0x1FFFFFu
    };
    m_value &= masks[extra];
    // check the result:
    static const uint32_t overlong[4] = {
      0x00u, 0x80u, 0x800u, 0x10000u
    };
    if ((uint32_t)m_value > (uint32_t)0x10FFFFu || (uint32_t)m_value < overlong[extra]) {
      // Prevent overlong or invalid characters
      m_value = STAR_UTF32_REPLACEMENT_CHAR;
      return;
    }
  }

  BaseIterator m_position;
  mutable U32Type m_value;

  BaseIterator m_begin;
  BaseIterator m_end;
};

// Output iterator
template <class BaseIterator, class U32Type = Utf32Type>
class Utf8OutputIterator {
public:
  typedef void difference_type;
  typedef void value_type;
  typedef U32Type* pointer;
  typedef U32Type& reference;

  Utf8OutputIterator(const BaseIterator& b) : m_position(b) {}
  Utf8OutputIterator(const Utf8OutputIterator& that) : m_position(that.m_position) {}
  Utf8OutputIterator& operator=(const Utf8OutputIterator& that) {
    m_position = that.m_position;
    return *this;
  }

  const Utf8OutputIterator& operator*() const {
    return *this;
  }

  void operator=(U32Type val) const {
    push(val);
  }

  Utf8OutputIterator& operator++() {
    return *this;
  }

  Utf8OutputIterator& operator++(int) {
    return *this;
  }

private:
  void push(U32Type c) const {
    if (c > 0x10FFFFu) {
      push(STAR_UTF32_REPLACEMENT_CHAR);
      return;
    }

    if ((uint32_t)c < 0x80u) {
      *m_position++ = static_cast<Utf8Type>((uint32_t)c);
    } else if ((uint32_t)c < 0x800u) {
      *m_position++ = static_cast<Utf8Type>(0xC0u + ((uint32_t)c >> 6));
      *m_position++ = static_cast<Utf8Type>(0x80u + ((uint32_t)c & 0x3Fu));
    } else if ((uint32_t)c < 0x10000u) {
      *m_position++ = static_cast<Utf8Type>(0xE0u + ((uint32_t)c >> 12));
      *m_position++ = static_cast<Utf8Type>(0x80u + (((uint32_t)c >> 6) & 0x3Fu));
      *m_position++ = static_cast<Utf8Type>(0x80u + ((uint32_t)c & 0x3Fu));
    } else {
      *m_position++ = static_cast<Utf8Type>(0xF0u + ((uint32_t)c >> 18));
      *m_position++ = static_cast<Utf8Type>(0x80u + (((uint32_t)c >> 12) & 0x3Fu));
      *m_position++ = static_cast<Utf8Type>(0x80u + (((uint32_t)c >> 6) & 0x3Fu));
      *m_position++ = static_cast<Utf8Type>(0x80u + ((uint32_t)c & 0x3Fu));
    }
  }

  mutable BaseIterator m_position;
};

}// namespace Star

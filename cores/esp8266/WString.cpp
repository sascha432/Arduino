/*
 WString.cpp - String library for Wiring & Arduino
 ...mostly rewritten by Paul Stoffregen...
 Copyright (c) 2009-10 Hernando Barragan.  All rights reserved.
 Copyright 2011, Paul Stoffregen, paul@pjrc.com
 Modified by Ivan Grokhotkov, 2014 - esp8266 support
 Modified by Michael C. Miller, 2015 - esp8266 progmem support

 This library is free software; you can redistribute it and/or
 modify it under the terms of the GNU Lesser General Public
 License as published by the Free Software Foundation; either
 version 2.1 of the License, or (at your option) any later version.

 This library is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 Lesser General Public License for more details.

 You should have received a copy of the GNU Lesser General Public
 License along with this library; if not, write to the Free Software
 Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <Arduino.h>
#include <stdlib_noniso.h>
#include "WString.h"

#define OOM_STRING_BORDER_DISPLAY           10
#define OOM_STRING_THRESHOLD_REALLOC_WARN  128

#define __STRHELPER(x) #x
#define STR(x) __STRHELPER(x) // stringifier

/*********************************************/
/*  Constructors                             */
/*********************************************/

String::String(const char *cstr) {
    init();
    if (cstr)
        copy(cstr, strlen_P(cstr));
}

String::String(const String &value) {
    init();
    *this = value;
}

String::String(const __FlashStringHelper *pstr) {
    init();
    *this = pstr; // see operator =
}

String::String(String &&rval) noexcept {
    init();
    move(rval);
}

String::String(unsigned char value, unsigned char base) {
    init();
    char buf[1 + 8 * sizeof(unsigned char)];
    utoa(value, buf, base);
    *this = buf;
}

String::String(int value, unsigned char base) {
    init();
    if (base == 10) {
        char buf[std::numeric_limits<decltype(value)>::digits10 + 2];
        sprintf(buf, "%d", value);
        *this = buf;
    } else {
        char buf[2 + 8 * sizeof(value)];
        itoa(value, buf, base);
        *this = buf;
    }
}

String::String(unsigned int value, unsigned char base) {
    init();
    char buf[1 + 8 * sizeof(unsigned int)];
    utoa(value, buf, base);
    *this = buf;
}

String::String(long value, unsigned char base) {
    init();
    if (base == 10) {
        char buf[std::numeric_limits<decltype(value)>::digits10 + 2];
        sprintf(buf, "%ld", value);
        *this = buf;
    } else {
        char buf[2 + 8 * sizeof(value)];
        ltoa(value, buf, base);
        *this = buf;
    }
}

String::String(unsigned long value, unsigned char base) {
    init();
    char buf[1 + 8 * sizeof(unsigned long)];
    ultoa(value, buf, base);
    *this = buf;
}

String::String(long long value) {
    init();
    char buf[std::numeric_limits<decltype(value)>::digits10 + 2];
    sprintf(buf, "%lld", value);
    *this = buf;
}

String::String(unsigned long long value) {
    init();
    char buf[std::numeric_limits<decltype(value)>::digits10 + 2];
    sprintf(buf, "%llu", value);
    *this = buf;
}

String::String(long long value, unsigned char base) {
    init();
    char buf[1 + 8 * sizeof(value)];
    *this = lltoa(value, buf, sizeof(buf), base);
}

String::String(unsigned long long value, unsigned char base) {
    init();
    char buf[1 + 8 * sizeof(value)];
    *this = ulltoa(value, buf, sizeof(buf), base);
}

String::String(float value, unsigned char decimalPlaces) {
    init();
    char buf[33];
    *this = dtostrf(value, (decimalPlaces + 2), decimalPlaces, buf);
}

String::String(double value, unsigned char decimalPlaces) {
    init();
    char buf[33];
    *this = dtostrf(value, (decimalPlaces + 2), decimalPlaces, buf);
}

/*********************************************/
/*  Memory Management                        */
/*********************************************/

void String::invalidate(void) {
    if (!isSSO() && wbuffer())
        free(wbuffer());
    init();
}

bool String::reserve(unsigned int size) {
    if (buffer() && capacity() >= size)
        return true;
    if (changeBuffer(size)) {
        if (len() == 0)
            wbuffer()[0] = 0;
        return true;
    }
    return false;
}

bool String::changeBuffer(unsigned int maxStrLen) {
    // Can we use SSO here to avoid allocation?
    if (maxStrLen < sizeof(sso.buff) - 1) {
        if (isSSO() || !buffer()) {
            // Already using SSO, nothing to do
            uint16_t oldLen = len();
            setSSO(true);
            setLen(oldLen);
        } else { // if bufptr && !isSSO()
            // Using bufptr, need to shrink into sso.buff
            const char *temp = buffer();
            uint16_t oldLen = len();
            setSSO(true);
            setLen(oldLen);
            memcpy(wbuffer(), temp, maxStrLen);
            free((void *)temp);
        }
        return true;
    }
    // Fallthrough to normal allocator
    size_t newSize = (maxStrLen + 16) & (~0xf);
#ifdef DEBUG_ESP_OOM
    if (!isSSO() && capacity() >= OOM_STRING_THRESHOLD_REALLOC_WARN && maxStrLen > capacity()) {
        // warn when badly re-allocating
        DEBUGV("[offending String op %d->%d ('%." STR(OOM_STRING_BORDER_DISPLAY) "s ... %." STR(OOM_STRING_BORDER_DISPLAY) "s')]\n",
            len(), maxStrLen, c_str(),
            len() > OOM_STRING_BORDER_DISPLAY? c_str() + std::max((int)len() - OOM_STRING_BORDER_DISPLAY, OOM_STRING_BORDER_DISPLAY): "");
    }
#endif
    // Make sure we can fit newsize in the buffer
    if (newSize > CAPACITY_MAX) {
        return false;
    }
    uint16_t oldLen = len();
    char *newbuffer = (char *)realloc(isSSO() ? nullptr : wbuffer(), newSize);
    if (newbuffer) {
        size_t oldSize = capacity() + 1; // include NULL.
        if (isSSO()) {
            // Copy the SSO buffer into allocated space
            memmove_P(newbuffer, sso.buff, sizeof(sso.buff));
        }
        if (newSize > oldSize) {
            memset(newbuffer + oldSize, 0, newSize - oldSize);
        }
        setSSO(false);
        setCapacity(newSize - 1);
        setLen(oldLen); // Needed in case of SSO where len() never existed
        setBuffer(newbuffer);
        return true;
    }
    return false;
}

/*********************************************/
/*  Copy and Move                            */
/*********************************************/

String &String::copy(const char *cstr, unsigned int length) {
    if (!reserve(length)) {
        invalidate();
        return *this;
    }
    setLen(length);
    memmove_P(wbuffer(), cstr, length + 1);
    return *this;
}

String &String::copy(const __FlashStringHelper *pstr, unsigned int length) {
    if (!reserve(length)) {
        invalidate();
        return *this;
    }
    setLen(length);
    memcpy_P(wbuffer(), (PGM_P)pstr, length + 1); // We know wbuffer() cannot ever be in PROGMEM, so memcpy safe here
    return *this;
}

void String::move(String &rhs) noexcept {
    invalidate();
    sso = rhs.sso;
    rhs.init();
}

String &String::operator =(const String &rhs) {
    if (this == &rhs)
        return *this;
    if (rhs.buffer())
        copy(rhs.buffer(), rhs.len());
    else
        invalidate();
    return *this;
}

String &String::operator =(String &&rval) noexcept {
    if (this != &rval)
        move(rval);
    return *this;
}

String &String::operator =(const char *cstr) {
    if (cstr)
        copy(cstr, strlen(cstr));
    else
        invalidate();
    return *this;
}

String &String::operator =(const __FlashStringHelper *pstr) {
    if (pstr)
        copy(pstr, strlen_P((PGM_P)pstr));
    else
        invalidate();
    return *this;
}

/*********************************************/
/*  concat                                   */
/*********************************************/

bool String::concat(const String &s) {
    // Special case if we're concatting ourself (s += s;) since we may end up
    // realloc'ing the buffer and moving s.buffer in the method called
    if (&s == this) {
        unsigned int newlen = 2 * len();
        if (!s.buffer())
            return false;
        if (s.len() == 0)
            return true;
        if (!reserve(newlen))
            return false;
        memmove_P(wbuffer() + len(), buffer(), len());
        setLen(newlen);
        wbuffer()[newlen] = 0;
        return true;
    } else {
        return concat(s.buffer(), s.len());
    }
}

bool String::concat(const char *cstr, unsigned int length) {
    unsigned int newlen = len() + length;
    if (!cstr)
        return false;
    if (length == 0)
        return true;
    if (!reserve(newlen))
        return false;
    memmove_P(wbuffer() + len(), cstr, length);
    setLen(newlen);
    wbuffer()[newlen] = 0;
    return true;
}

bool String::concat(const char *cstr) {
    if (!cstr)
        return false;
    return concat(cstr, strlen(cstr));
}

bool String::concat(char c) {
    return concat(&c, 1);
}

bool String::concat(unsigned char num) {
    char buf[1 + 3 * sizeof(unsigned char)];
    return concat(buf, sprintf(buf, "%d", num));
}

bool String::concat(int num) {
    char buf[2 + 3 * sizeof(int)];
    return concat(buf, sprintf(buf, "%d", num));
}

bool String::concat(unsigned int num) {
    char buf[1 + 3 * sizeof(unsigned int)];
    utoa(num, buf, 10);
    return concat(buf, strlen(buf));
}

bool String::concat(long num) {
    char buf[2 + 3 * sizeof(long)];
    return concat(buf, sprintf(buf, "%ld", num));
}

bool String::concat(unsigned long num) {
    char buf[1 + 3 * sizeof(unsigned long)];
    ultoa(num, buf, 10);
    return concat(buf, strlen(buf));
}

bool String::concat(long long num) {
    char buf[2 + 3 * sizeof(long long)];
    return concat(buf, sprintf(buf, "%lld", num));
}

bool String::concat(unsigned long long num) {
    char buf[1 + 3 * sizeof(unsigned long long)];
    return concat(buf, sprintf(buf, "%llu", num));
}

bool String::concat(float num) {
    char buf[20];
    char *string = dtostrf(num, 4, 2, buf);
    return concat(string, strlen(string));
}

bool String::concat(double num) {
    char buf[20];
    char *string = dtostrf(num, 4, 2, buf);
    return concat(string, strlen(string));
}

bool String::concat(const __FlashStringHelper *str) {
    if (!str)
        return false;
    int length = strlen_P((PGM_P)str);
    if (length == 0)
        return true;
    unsigned int newlen = len() + length;
    if (!reserve(newlen))
        return false;
    memcpy_P(wbuffer() + len(), (PGM_P)str, length + 1);
    setLen(newlen);
    return true;
}

/*********************************************/
/*  Insert                                   */
/*********************************************/

String &String::insert(size_t position, const char *other, size_t other_length) {
    if (position > length())
        return *this;

    auto len = length();
    auto total = len + other_length;
    if (!reserve(total))
        return *this;

    auto left = len - position;
    setLen(total);

    auto *start = wbuffer() + position;
    memmove(start + other_length, start, left);
    memmove_P(start, other, other_length);
    wbuffer()[total] = '\0';

    return *this;
}

String &String::insert(size_t position, const __FlashStringHelper *other) {
    auto *p = reinterpret_cast<const char*>(other);
    return insert(position, p, strlen_P(p));
}

String &String::insert(size_t position, char other) {
    char tmp[2] { other, '\0' };
    return insert(position, tmp, 1);
}

String &String::insert(size_t position, const char *other) {
    return insert(position, other, strlen(other));
}

String &String::insert(size_t position, const String &other) {
    return insert(position, other.c_str(), other.length());
}

String operator +(const String &lhs, String &&rhs) {
    String res;
    auto total = lhs.length() + rhs.length();
    if (rhs.capacity() > total) {
        rhs.insert(0, lhs);
        res = std::move(rhs);
    } else {
        res.reserve(total);
        res += lhs;
        res += rhs;
        rhs.invalidate();
    }

    return res;
}

String operator +(String &&lhs, String &&rhs) {
    String res;
    auto total = lhs.length() + rhs.length();
    if ((total > lhs.capacity()) && (total < rhs.capacity())) {
        rhs.insert(0, lhs);
        res = std::move(rhs);
    } else {
        lhs += rhs;
        rhs.invalidate();
        res = std::move(lhs);
    }

    return res;
}

String operator +(char lhs, const String &rhs) {
    String res;
    res.reserve(rhs.length() + 1);
    res += lhs;
    res += rhs;
    return res;
}

String operator +(const char *lhs, const String &rhs) {
    String res;
    res.reserve(strlen_P(lhs) + rhs.length());
    res += lhs;
    res += rhs;
    return res;
}

/*********************************************/
/*  Comparison                               */
/*********************************************/

int String::compareTo(const String &s) const {
    if (!buffer() || !s.buffer()) {
        if (s.buffer() && s.len() > 0)
            return 0 - *(unsigned char *)s.buffer();
        if (buffer() && len() > 0)
            return *(unsigned char *)buffer();
        return 0;
    }
    return strcmp(buffer(), s.buffer());
}

bool String::operator<(const String &rhs) const {
    return compareTo(rhs) < 0;
}

bool String::operator>(const String &rhs) const {
    return compareTo(rhs) > 0;
}

bool String::operator<=(const String &rhs) const {
    return compareTo(rhs) <= 0;
}

bool String::operator>=(const String &rhs) const {
    return compareTo(rhs) >= 0;
}

unsigned char String::equalsConstantTime(const String &s2) const {
    // To avoid possible time-based attacks present function
    // compares given strings in a constant time.
    if (len() != s2.len())
        return 0;
    //at this point lengths are the same
    if (len() == 0)
        return 1;
    //at this point lengths are the same and non-zero
    const char *p1 = buffer();
    const char *p2 = s2.buffer();
    unsigned int equalchars = 0;
    unsigned int diffchars = 0;
    while (*p1) {
        if (*p1 == *p2)
            ++equalchars;
        else
            ++diffchars;
        ++p1;
        ++p2;
    }
    //the following should force a constant time eval of the condition without a compiler "logical shortcut"
    unsigned char equalcond = (equalchars == len());
    unsigned char diffcond = (diffchars == 0);
    return (equalcond & diffcond); //bitwise AND
}

/*********************************************/
/*  Character Access                         */
/*********************************************/

void String::setCharAt(unsigned int loc, char c) {
    if (loc < len())
        wbuffer()[loc] = c;
}

char &String::operator[](unsigned int index) {
    static char dummy_writable_char;
    if (index >= len() || !buffer()) {
        dummy_writable_char = 0;
        return dummy_writable_char;
    }
    return wbuffer()[index];
}

char String::operator[](unsigned int index) const {
    if (index >= len() || !buffer())
        return '\0';
    return buffer()[index];
}

void String::getBytes(unsigned char *buf, unsigned int bufsize, unsigned int index) const {
    if (!bufsize || !buf)
        return;
    if (index >= len()) {
        buf[0] = 0;
        return;
    }
    unsigned int n = bufsize - 1;
    if (n > len() - index)
        n = len() - index;
    strncpy((char *)buf, buffer() + index, n);
    buf[n] = 0;
}

/*********************************************/
/*  Search                                   */
/*********************************************/

int String::indexOf(char ch, unsigned int fromIndex) const {
    if (fromIndex >= len())
        return -1;
    const char *temp = strchr(buffer() + fromIndex, ch);
    if (temp == NULL)
        return -1;
    return temp - buffer();
}

int String::indexOf(const String &s2) const {
    return indexOf(s2, 0);
}

int String::indexOf(const String &s2, unsigned int fromIndex) const {
    if (fromIndex >= len())
        return -1;
    const char *found = strstr(buffer() + fromIndex, s2.buffer());
    if (found == NULL)
        return -1;
    return found - buffer();
}

// TODO write test cases to verify this is working correctly
int String::_lastIndexOf_P(PGM_P find, size_t fromIndex, size_t findLen) const
{
    size_t len;
    if (!find || !(len = length())) {
        return -1;
    }
    if (fromIndex == ~0U) {
        fromIndex = len;
    }
    // else if (fromIndex < findLen || fromIndex > len) {
    //     return -1;
    // }
    auto ptr = __strrstr_P(wbuffer(), fromIndex + findLen, find, findLen);
    if (!ptr) {
        return -1;
    }
    return ptr - buffer();
}

// TODO write test cases to verify this is working correctly
// same method as String::_lastIndexOf_P(PGM_P find, size_t fromIndex, size_t findLen) const
int String::_lastIndexOf(const char *find, size_t fromIndex, size_t findLen) const
{
    size_t len;
    if (!find || !(len = length())) {
        return -1;
    }
    if (fromIndex == ~0U) {
        fromIndex = len;
    }
    // else if (fromIndex < findLen || fromIndex > len) {
    //     return -1;
    // }
    auto ptr = __strrstr(wbuffer(), fromIndex + findLen, find, findLen);
    if (!ptr) {
        return -1;
    }
    return ptr - buffer();
}

String String::substring(unsigned int left, unsigned int right) const
{
    if (left > right) {
        unsigned int temp = right;
        right = left;
        left = temp;
    }
    String out;
    if (left >= len())
        return out;
    if (right > len())
        right = len();
#if _MSC_VER
    out.concat(buffer() + left, right - left + 1);
#else
    char *writeTo = wbuffer();
    char tempchar = writeTo[right]; // save the replaced character
    writeTo[right] = '\0';
    out = writeTo + left; // pointer arithmetic
    writeTo[right] = tempchar; // restore character
#endif
    return out;
}

/*********************************************/
/*  Modification                             */
/*********************************************/

bool String::replace(char find, char replace)
{
    if (!buffer())
        return true;
    for (char *p = wbuffer(); *p; p++) {
        if (*p == find) {
            *p = replace;
        }
    }
    return true;
}

// TODO write test cases to verify this is working correctly
bool String::_replace(PGM_P findStr, size_t findLen, PGM_P replaceStr, size_t replaceLen)
{
    if (length() == 0 || findLen == 0 || !findStr) {
        return false;
    }
    int diff = replaceLen - findLen;
    char *readFrom = wbuffer();
    char *foundAt;
    if (diff == 0) {
        while ((foundAt = strstr_P(readFrom, findStr)) != nullptr) {
            memmove_P(foundAt, replaceStr, replaceLen);
            readFrom = foundAt + replaceLen;
        }
    }
    else if (diff < 0) {
        char *writeTo = wbuffer();
        while ((foundAt = strstr_P(readFrom, findStr)) != nullptr) {
            unsigned int n = foundAt - readFrom;
            memmove(writeTo, readFrom, n);
            writeTo += n;
            memmove_P(writeTo, replaceStr, replaceLen);
            writeTo += replaceLen;
            readFrom = foundAt + findLen;
            setLen(len() + diff);
        }
        memmove(writeTo, readFrom, strlen(readFrom) + 1);
    }
    else {
        unsigned int size = len(); // compute size needed for result
        while ((foundAt = strstr_P(readFrom, findStr)) != nullptr) {
            readFrom = foundAt + findLen;
            size += diff;
        }
        if (size == len()) {
            return false;
        }
        if (size > capacity() && !changeBuffer(size)) {
            return false;
        }
        int index = len() - 1;
        while (index >= 0 && (index = _lastIndexOf_P(findStr, index, findLen)) >= 0) {
            readFrom = wbuffer() + index + findLen;
            memmove(readFrom + diff, readFrom, len() - (readFrom - buffer()));
            int newLen = len() + diff;
            memmove_P(wbuffer() + index, replaceStr, replaceLen);
            setLen(newLen);
            wbuffer()[newLen] = 0;
            index--;
        }
    }
    return true;
}

void String::remove(unsigned int index, unsigned int count) {
    if (index >= len()) {
        return;
    }
    if (count <= 0) {
        return;
    }
    if (count > len() - index) {
        count = len() - index;
    }
    char *writeTo = wbuffer() + index;
    unsigned int newlen = len() - count;
    setLen(newlen);
    memmove_P(writeTo, wbuffer() + index + count, newlen - index);
    wbuffer()[newlen] = 0;
}

String &String::toLowerCase(void) {
    if (!buffer())
        return *this;
    for (char *p = wbuffer(); *p; p++) {
        *p = tolower(*p);
    }
    return *this;
}

String &String::toUpperCase(void) {
    if (!buffer())
        return *this;
    for (char *p = wbuffer(); *p; p++) {
        *p = toupper(*p);
    }
    return *this;
}

// new trim functions

String &String::_trim(TrimType type)
{
    auto len = length();
    if (len == 0) {
        return *this;
    }
    char *begin = wbuffer();
    if (static_cast<uint8_t>(type) & static_cast<uint8_t>(TrimType::LEFT)) {
        while (isspace(*begin)) {
            begin++;
        }
    }
    char *end = wbuffer() + len - 1;
    if (static_cast<uint8_t>(type) & static_cast<uint8_t>(TrimType::RIGHT)) {
        while (isspace(*end) && end >= begin) {
            end--;
        }
    }
    unsigned int newlen = end + 1 - begin;
    if (begin > buffer()) {
        memmove(wbuffer(), begin, newlen);
    }
    setLen(newlen);
    wbuffer()[newlen] = 0;
    return *this;
}

String &String::_trim(TrimType type, PGM_P characters, size_t charLen)
{
    auto len = length();
    if (!len || !characters || !charLen) {
        return *this;
    }
    charLen++; // use memchr instead of strchr to avoid additional NUL byte check
    if (static_cast<uint8_t>(type) & static_cast<uint8_t>(TrimType::RIGHT)) {
        while (len && memchr_P(characters, buffer()[len - 1], charLen)) {
            len--;
        }
        setLen(len);
    }

    if (static_cast<uint8_t>(type) & static_cast<uint8_t>(TrimType::LEFT)) {
        size_t remove = 0;
        while (memchr_P(characters, buffer()[remove], charLen)) {
            remove++;
        }
        this->remove(0, remove); // remove adds NUL byte
    }
    else {
        wbuffer()[len] = 0;
    }
    return *this;
}

/*********************************************/
/*  Parsing / Conversion                     */
/*********************************************/

long String::toInt(void) const {
    if (buffer())
        return atol(buffer());
    return 0;
}

float String::toFloat(void) const {
    if (buffer())
        return atof(buffer());
    return 0.0F;
}

double String::toDouble(void) const {
    if (buffer())
        return atof(buffer());
    return 0.0;
}

// global empty string to allow returning const String& with nothing

const String emptyString;

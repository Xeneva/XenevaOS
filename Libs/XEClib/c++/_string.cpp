/**
* BSD 2-Clause License
*
* Copyright (c) 2022-2023, Manas Kamal Choudhury
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*
* 1. Redistributions of source code must retain the above copyright notice, this
*    list of conditions and the following disclaimer.
*
* 2. Redistributions in binary form must reproduce the above copyright notice,
*    this list of conditions and the following disclaimer in the documentation
*    and/or other materials provided with the distribution.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
* AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
* IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
* DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
* FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
* DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
* SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
* CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
* OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
* OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*
**/

#include <string>
#include <ostream>

using namespace std;

template <class charT, class sizeT> static sizeT stringLen(const charT* s) {
	sizeT len = 0;

	while (static_cast<unsigned>(s[len]))
		len += 1;

	return (len);
}

template <class inCharT, class Alloc, class outCharT, class sizeT>
static outCharT* stringAlloc(const inCharT* s, const Alloc& a, sizeT& dataLen) {
	outCharT* data = 0;
	sizeT count = 0;

	dataLen = (stringLen<inCharT, sizeT>(s) + 1);

	data = a.allocate(dataLen);

	for (count = 0; count < (dataLen - 1); count++)
		data[count] = static_cast<outCharT>(s[count]);

	data[dataLen - 1] = static_cast<outCharT>(0);

	return (data);
}

template <class charT, class Alloc, class sizeT>
static void stringFree(charT*& data, const Alloc& a, sizeT& dataLen) {
	if (data) {
		a.deallocate(data, dataLen);
		data = 0;
		dataLen = 0;
	}
}

namespace std {

template <class charT, class traits, class Alloc>
basic_string<charT, traits, Alloc>::basic_string(const Alloc& a)
	: _alloc(a), _data(0), _dataLen(0) {}

template <class charT, class traits, class Alloc>
basic_string<charT, traits, Alloc>::basic_string(const charT* s, const Alloc& a)
	: _alloc(a), _data(0), _dataLen(0) {
	_data = stringAlloc<charT, Alloc, charT, size_type>(s, _alloc, _dataLen);
}

template <class charT, class traits, class Alloc>
const charT* basic_string<charT, traits, Alloc>::c_str() const {
	return (_data);
}

template <class charT, class traits, class Alloc>
typename basic_string<charT, traits, Alloc>::size_type
basic_string<charT, traits, Alloc>::length() const {
	return (_dataLen - 1);
}

template <class charT, class traits, class Alloc>
basic_string<charT, traits, Alloc>&
basic_string<charT, traits, Alloc>::operator=(const basic_string<charT, traits, Alloc>& s) {
	stringFree<charT, Alloc, size_type>(_data, _alloc, _dataLen);

	_data = stringAlloc<charT, Alloc, charT, size_type>(s._data, _alloc, _dataLen);

	return (*this);
}

template <class charT, class traits, class Alloc>
basic_string<charT, traits, Alloc>& basic_string<charT, traits, Alloc>::operator=(const charT* s) {
	stringFree<charT, Alloc, size_type>(_data, _alloc, _dataLen);

	_data = stringAlloc<charT, Alloc, charT, size_type>(s, _alloc, _dataLen);

	return (*this);
}

template <class charT, class traits, class Alloc>
basic_string<charT, traits, Alloc>& basic_string<charT, traits, Alloc>::operator=(charT c) {
	charT s[] = {c, static_cast<charT>(0)};

	stringFree<charT, Alloc, size_type>(_data, _alloc, _dataLen);

	_data = stringAlloc<charT, Alloc, charT, size_type>(s, _alloc, _dataLen);

	return (*this);
}

template <class charT, class traits, class Alloc>
typename basic_string<charT, traits, Alloc>::size_type
basic_string<charT, traits, Alloc>::size() const {
	return (_dataLen - 1);
}

template <> istream& operator>>(istream& is, string& s) {
	//int dataLen = 32 + 1; //(visopsys::textInputCount() + 1);
	char data[32 + 1];

	is >> data;
	s = data;

	return (is);
}

template <> ostream& operator<<(ostream& os, const string& s) {
	return (os << s.c_str());
}

template class basic_string<char, char_traits<char>, allocator<char>>;
} // namespace std

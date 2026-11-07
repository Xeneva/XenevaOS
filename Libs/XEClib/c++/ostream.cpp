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

#include <ostream>

using namespace std;

namespace std {

template <class charT, class traits>
basic_ostream<charT, traits>::basic_ostream(basic_streambuf<charT, traits>*) {}

template <class charT, class traits> basic_ostream<charT, traits>::~basic_ostream() {}

template <class charT, class traits>
basic_ostream<charT, traits>& basic_ostream<charT, traits>::operator<<(
	basic_ostream<charT, traits>& (*pf)(basic_ostream<charT, traits>&)) {
	return (pf(*this));
}

template <> ostream& ostream::operator<<(char c) {
	//visopsys::textPutc(static_cast<int>(c));
	return (*this);
}

template <> ostream& ostream::operator<<(const char* s) {
	//visopsys::textPrint(s);
	return (*this);
}

ostream& endl(ostream& os) {
	//visopsys::textNewline();
	return (os);
}

template class basic_ostream<char, char_traits<char>>;
} // namespace std

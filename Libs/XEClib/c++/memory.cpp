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

#include <memory>
#include <cstdlib>

namespace std {

template <class T> allocator<T>::allocator() throw() {}

template <class T> allocator<T>::allocator(const allocator<T>&) throw() {}

template <class T> allocator<T>::~allocator() throw() {}

template <class T> typename allocator<T>::pointer allocator<T>::address(reference ref) const {
	return (&ref);
}

template <class T>
typename allocator<T>::const_pointer allocator<T>::address(const_reference ref) const {
	return (&ref);
}

template <class T>
typename allocator<T>::pointer allocator<T>::allocate(size_type size,
													  allocator<void>::const_pointer hint) const {
	if (hint) { /* not implemented */
	}

	return (new T[size]);
}

template <class T> void allocator<T>::deallocate(pointer ptr, size_type size) const {
	if (size)
		return (delete[] ptr);
}

template <class T> typename allocator<T>::size_type allocator<T>::max_size() const throw() {
	return (static_cast<size_type>(-1));
}

template class allocator<char>;
} // namespace std

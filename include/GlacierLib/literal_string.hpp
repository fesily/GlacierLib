#ifndef Literal_String
#define Literal_String

#include <algorithm>

namespace literal
{
	template <size_t N>
	struct string
	{
		char str_[N + 1] = {};

		constexpr string(void) noexcept = default;
		constexpr string(const char* str, size_t n) noexcept
		{
			for (size_t i = 0; i < std::min(N, n); ++i)
				str_[i] = str[i];
		}

		template <size_t N_>
		constexpr string(const char(&str)[N_]) noexcept
			: string{ str, N_ }
		{}

		constexpr auto operator[](size_t n) const noexcept
		{
			if (n >= N) return '\0';
			return str_[n];
		}

		constexpr size_t size(void) const noexcept
		{
			return N;
		}

		constexpr size_t length(void) const noexcept
		{
			size_t r = 0;
			for (; r < N; ++r)
				if (str_[r] == '\0') break;
			return r;
		}

		constexpr auto begin(void) const noexcept
		{
			return str_;
		}

		constexpr auto end(void) const noexcept
		{
			return str_ + length();
		}

		constexpr const char* c_str(void) const noexcept
		{
			return str_;
		}

		constexpr size_t count(char c) const noexcept
		{
			size_t r = 0;
			for (size_t i = 0; i < length(); ++i)
				if (c == str_[i]) ++r;
			return r;
		}

		static const size_t npos = -1;

		constexpr string substr(size_t pos, size_t len = -1) const noexcept
		{
			return { str_ + pos, std::min(length() - pos, len) };
		}

		constexpr size_t find(char c, size_t pos = 0) const noexcept
		{
			for (size_t i = pos; i < length(); ++i)
				if (c == str_[i]) return i;
			return npos;
		}
	};
	template <size_t N>
	constexpr string<N> make(const char(&str)[N]) noexcept
	{
		return { str };
	}
	template <size_t M, size_t N>
	struct string_array
	{
		typedef string<N> string_t;
		string_t arr_[M] = {};

	public:
		constexpr string_array(void) noexcept = default;

		constexpr const string_t& operator[](size_t n) const noexcept
		{
			return arr_[n];
		}

		constexpr const string_t* begin(void) const noexcept
		{
			return arr_;
		}

		constexpr const string_t* end(void) const noexcept
		{
			return arr_ + M;
		}

	};
	template <size_t M, size_t N>
	constexpr string_array<M, N> _split(char delimiter, const char(&str)[N]) noexcept
	{
		string_array<M, N> r;
		auto s = make(str);
		size_t start = 0, end = s.find(delimiter);
		for (size_t i = 0; i < M; ++i)
		{
			r.arr_[i] = s.substr(start, end - start);
			if (end == string<N>::npos) break;
			start = end + 1;
			end = s.find(delimiter, start);
		}
		return r;
	}
	template <size_t N>
	constexpr size_t count(char delimiter, const char(&str)[N]) noexcept
	{
		return literal::make(str).count(delimiter) + 1;
	}
#define literal_split(c,s) literal::_split<literal::count(c,s)>(c,s)
} // namespace literal

#endif

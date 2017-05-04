#ifndef Class_Meta
#define Class_Meta

#include <array>
#include <tuple>

#include "literal_string.hpp"

namespace Meta
{
	template<size_t N,typename _Type>
	std::pair<const char* const,_Type&&> make_pair(const literal::string<N>& key, _Type&& value)
	{
		return { key.c_str(),std::forward<_Type>(value) };
	}
	template <size_t... Sizes,size_t M, size_t N, typename...Args>
	auto _Make_meta(std::index_sequence<Sizes...>,const literal::string_array<M, N>& ar, std::tuple<Args...>&& tuple)
	{
		return std::make_tuple(make_pair(ar[Sizes], std::get<Sizes>(tuple))...);
	}

	template <size_t M, size_t N,typename..._Args>
	auto make_meta(const literal::string_array<M, N>& ar, _Args&... args)
	{
		static_assert(M == sizeof...(_Args), "error input");
		return _Make_meta(std::make_index_sequence<M>(), ar, std::tuple<const _Args&...>{args...});
	}

#define META(...) auto Meta(){ constexpr auto ar = literal_split(',',#__VA_ARGS__); return Meta::make_meta(ar, __VA_ARGS__);} 
}


#endif
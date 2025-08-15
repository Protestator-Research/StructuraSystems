#pragma once

#include <optional>
#include <sysmlv2/rest/entities/IEntity.h>


namespace StructuraSystems::Server
{
	template<typename T, typename = void>
	class CrudApi
	{
	};

	//template<typename T>
	//class CrudApi<T, std::enable_if<std::is_base<SysMLv2::REST::IEntity>>>
	//{
	//public:
	//	explicit CrudApi();

	//private:
	//	IdMap<
	//};
}

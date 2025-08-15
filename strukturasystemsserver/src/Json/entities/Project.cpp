#include "Project.h"

#include <boost/uuid/uuid_io.hpp>
#include <sysmlv2/rest/entities/JSONEntities.h>

namespace StructuraSystems::Server
{
	Project::Project(SysMLv2::REST::Project& other)
		: SysMLv2::REST::Project(other)
	{	}

	Project::Project(const std::string& JsonString)
		: SysMLv2::REST::Project(JsonString)
	{	}

	Project::Project(const std::string& projectName, const std::string& projectDescription,
		const std::string& branchName)
		: SysMLv2::REST::Project(projectName, projectDescription, branchName)
	{
	}

	QJsonObject Project::toJson() const
	{
		//QString* aliases = new QString[Alias.size()];
		//for (int i = 0; i < Alias.size(); i++)
		//	aliases[i] = QString::fromStdString(Alias[i]);



		return QJsonObject{
			{QString::fromStdString(SysMLv2::REST::JSON_ID_ENTITY),QString::fromStdString(boost::uuids::to_string(Id))},
			{QString::fromStdString(SysMLv2::REST::JSON_TYPE_ENTITY),QString::fromStdString(Type)},
			{QString::fromStdString(SysMLv2::REST::JSON_NAME_ENTITY), QString::fromStdString(Name)},
			//{QString::fromStdString(SysMLv2::REST::JSON_ALIAS_ENTITY), aliases}
		};
	}
}

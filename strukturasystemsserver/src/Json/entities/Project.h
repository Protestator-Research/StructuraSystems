#pragma once

#include <sysmlv2/rest/entities/Project.h>


#include "../Jsonable.hpp"
#include "../Updatable.hpp"

namespace StructuraSystems::Server
{
	class Project : public SysMLv2::REST::Project, public Jsonable, public Updatable
	{
	public:
		Project() = delete;

		explicit Project(SysMLv2::REST::Project& other);

		explicit Project(const std::string& JsonString);

		~Project() override = default;

		Project(const std::string& projectName, const std::string& projectDescription, const std::string& branchName);

		QJsonObject toJson() const override;

	private:


	};

}

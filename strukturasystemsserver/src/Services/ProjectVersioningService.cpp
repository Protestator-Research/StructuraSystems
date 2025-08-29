#include "ProjectVersioningService.h"

#include <sysmlv2/rest/entities/Branch.h>
#include <sysmlv2/rest/entities/Commit.h>
#include <sysmlv2/rest/entities/Project.h>
#include <sysmlv2/rest/entities/ChangeType.h>

namespace StructuraSystems::Server
{
	ProjectVersioningService::ProjectVersioningService()
	{
	}

	std::vector<std::shared_ptr<SysMLv2::REST::Commit>> ProjectVersioningService::getCommits(
		std::shared_ptr<SysMLv2::REST::Project> project)
	{
		return ProjectIdCommitMap[project->getId()];
	}

	std::shared_ptr<SysMLv2::REST::Commit> ProjectVersioningService::getHeadCommit(
		std::shared_ptr<SysMLv2::REST::Project>, std::shared_ptr<SysMLv2::REST::Branch> branch)
	{
		return branch->getHead();
	}

	std::shared_ptr<SysMLv2::REST::Commit> ProjectVersioningService::getCommitById(
		std::shared_ptr<SysMLv2::REST::Project> project, boost::uuids::uuid commitId)
	{
		const auto projectCommits = ProjectIdCommitMap[project->getId()];
		for (const auto& commit:projectCommits)
		{
			if (commit->getId() == commitId)
				return commit;
		}
	}

	std::shared_ptr<SysMLv2::REST::Commit> ProjectVersioningService::createCommit(
		std::shared_ptr<SysMLv2::REST::DataVersion> change, std::shared_ptr<SysMLv2::REST::Branch> branch,
		std::vector<std::shared_ptr<SysMLv2::REST::Commit>> previousCommits,
		std::shared_ptr<SysMLv2::REST::Project> project)
	{
		const auto commit = std::make_shared<SysMLv2::REST::Commit>("", "", project, previousCommits);
		commit->addChange(change);

		ProjectIdCommitMap[project->getId()].push_back(commit);
		branch->setHead(commit);

		return commit;
	}

	std::shared_ptr<SysMLv2::REST::Commit> ProjectVersioningService::createCommit(
		std::shared_ptr<SysMLv2::REST::DataVersion> change, std::shared_ptr<SysMLv2::REST::Branch> branch,
		std::shared_ptr<SysMLv2::REST::Project> project)
	{
		const auto commit = std::make_shared<SysMLv2::REST::Commit>("", "", project);
		commit->addChange(change);

		ProjectIdCommitMap[project->getId()].push_back(commit);
		branch->setHead(commit);

		return commit;
	}

	std::shared_ptr<SysMLv2::REST::Commit> ProjectVersioningService::createCommit(
		std::shared_ptr<SysMLv2::REST::DataVersion> change,
		std::vector<std::shared_ptr<SysMLv2::REST::Commit>> previousCommits,
		std::shared_ptr<SysMLv2::REST::Project> project)
	{
		const auto commit = std::make_shared<SysMLv2::REST::Commit>("", "", project, previousCommits);
		commit->addChange(change);

		ProjectIdCommitMap[project->getId()].push_back(commit);
		project->getDefaultBranch()->setHead(commit);

		return commit;
	}

	std::shared_ptr<SysMLv2::REST::Commit> ProjectVersioningService::createCommit(
		std::shared_ptr<SysMLv2::REST::DataVersion> change, std::shared_ptr<SysMLv2::REST::Project> project)
	{
		const auto commit = std::make_shared<SysMLv2::REST::Commit>("", "", project);
		commit->addChange(change);

		ProjectIdCommitMap[project->getId()].push_back(commit);
		project->getDefaultBranch()->setHead(commit);

		return commit;
	}

	std::vector<std::shared_ptr<SysMLv2::REST::DataVersion>> ProjectVersioningService::getCommitChange(
		std::shared_ptr<SysMLv2::REST::Project> project, std::shared_ptr<SysMLv2::REST::Commit> commit,
		std::vector<SysMLv2::REST::ChangeType> changetype)
	{
		std::vector<std::shared_ptr<SysMLv2::REST::DataVersion>> returnValue;

		

		return returnValue;
	}

	std::shared_ptr<SysMLv2::REST::DataVersion> ProjectVersioningService::getCommitChangeById(
		std::shared_ptr<SysMLv2::REST::Project> project, std::shared_ptr<SysMLv2::REST::Commit> commit,
		boost::uuids::uuid changeId)
	{
	}

	std::vector<std::shared_ptr<SysMLv2::REST::Branch>> ProjectVersioningService::getBranches(
		std::shared_ptr<SysMLv2::REST::Project> project)
	{
	}

	std::shared_ptr<SysMLv2::REST::Branch> ProjectVersioningService::getBranchById(
		std::shared_ptr<SysMLv2::REST::Project> project, boost::uuids::uuid branchId)
	{
	}

	std::shared_ptr<SysMLv2::REST::Branch> ProjectVersioningService::getDefaultBranch(
		std::shared_ptr<SysMLv2::REST::Project> project)
	{
	}

	std::shared_ptr<SysMLv2::REST::Project> ProjectVersioningService::setDefaultBranch(
		std::shared_ptr<SysMLv2::REST::Project> project, boost::uuids::uuid branchId)
	{
	}

	std::shared_ptr<SysMLv2::REST::Branch> ProjectVersioningService::createBranch(
		std::shared_ptr<SysMLv2::REST::Project> project, std::string branchName,
		std::shared_ptr<SysMLv2::REST::Commit> head)
	{
	}

	std::shared_ptr<SysMLv2::REST::Branch> ProjectVersioningService::deleteBranch(
		std::shared_ptr<SysMLv2::REST::Project> project, boost::uuids::uuid branchId)
	{
	}

	std::vector<std::shared_ptr<SysMLv2::REST::Tag>> ProjectVersioningService::getTags(
		std::shared_ptr<SysMLv2::REST::Project> project)
	{
	}

	std::shared_ptr<SysMLv2::REST::Tag> ProjectVersioningService::getTagById(
		std::shared_ptr<SysMLv2::REST::Project> project, boost::uuids::uuid tagId)
	{
	}

	std::shared_ptr<SysMLv2::REST::Commit> ProjectVersioningService::getTaggedCommit(
		std::shared_ptr<SysMLv2::REST::Project> project, std::shared_ptr<SysMLv2::REST::Tag> tag)
	{
	}

	std::shared_ptr<SysMLv2::REST::Tag> ProjectVersioningService::createTag(
		std::shared_ptr<SysMLv2::REST::Project> project, std::string tagName,
		std::shared_ptr<SysMLv2::REST::Commit> taggedCommit)
	{
	}

	std::shared_ptr<SysMLv2::REST::MergeResult> ProjectVersioningService::mergeIntoBranch(
		std::shared_ptr<SysMLv2::REST::Branch> baseBranch,
		std::vector<std::shared_ptr<SysMLv2::REST::Commit>> commitsToMerge,
		std::shared_ptr<SysMLv2::REST::Data> resolution, std::string description)
	{
	}

	std::vector<std::shared_ptr<SysMLv2::REST::DataDifference>> ProjectVersioningService::diffCommits(
		std::shared_ptr<SysMLv2::REST::Commit> baseCommit, std::shared_ptr<SysMLv2::REST::Commit> compareCommit,
		std::vector<SysMLv2::REST::ChangeType> changeType)
	{
	}
}

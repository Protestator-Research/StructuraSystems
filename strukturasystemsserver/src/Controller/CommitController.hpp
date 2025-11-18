#pragma once

#include <sysmlv2/rest/entities/Branch.h>
#include <sysmlv2/rest/entities/Commit.h>
#include <sysmlv2/rest/entities/CommitRequest.h>
#include <sysmlv2/rest/entities/DataVersion.h>
#include <sysmlv2/rest/entities/Project.h>
#include <sysmlv2/rest/entities/BranchRequest.h>
#include <sysmlv2/rest/entities/Tag.h>

#include "BaseController.hpp"
#include "../Services/ProjectVersioningService.h"
#include "../Services/ServerProjectService.h"

namespace StructuraSystems::Server
{
	class CommitController : public BaseController
	{
	public:
		CommitController() = delete;
		CommitController(QHttpServer *httpServer) : BaseController(httpServer)
		{
			generateRoutes();
			ProjectNavigationService = ServerProjectService::getInstance();
			ProjectVerService = ProjectVersioningService::getInstance();
		}

	protected:
		void generateRoutes() override
		{
			HttpServer->route("/projects/<arg>/commits", QHttpServerRequest::Method::Get, [this](QString projectId, const QHttpServerRequest& request)->QHttpServerResponse { return getCommits(projectId, request); } );
			HttpServer->route("/projects/<arg>/commits/", QHttpServerRequest::Method::Get, [this](QString projectId, const QHttpServerRequest& request)->QHttpServerResponse { return getCommits(projectId, request); } );

			HttpServer->route("/projects/<arg>/commits/<arg>", QHttpServerRequest::Method::Get, [this](QString projectId, QString commitId, const QHttpServerRequest& request)->QHttpServerResponse { return getCommitId(projectId, commitId, request); });
			HttpServer->route("/projects/<arg>/commits/<arg>/", QHttpServerRequest::Method::Get, [this](QString projectId, QString commitId, const QHttpServerRequest& request)->QHttpServerResponse { return getCommitId(projectId, commitId, request); });

			HttpServer->route("/projects/<arg>/commits", QHttpServerRequest::Method::Post, [this](QString projectId, const QHttpServerRequest& request)->QHttpServerResponse { return postCommit(projectId, request); });
			HttpServer->route("/projects/<arg>/commits/", QHttpServerRequest::Method::Post, [this](QString projectId, const QHttpServerRequest& request)->QHttpServerResponse { return postCommit(projectId, request); });

			HttpServer->route("/projects/<arg>/commits/<arg>/changes", QHttpServerRequest::Method::Get, [this](QString projectId, QString commitId, const QHttpServerRequest& request)->QHttpServerResponse { return getCommitChange(projectId, commitId, request); });
			HttpServer->route("/projects/<arg>/commits/<arg>/changes/", QHttpServerRequest::Method::Get, [this](QString projectId, QString commitId, const QHttpServerRequest& request)->QHttpServerResponse { return getCommitChange(projectId, commitId, request); });

			HttpServer->route("/projects/<arg>/commits/<arg>/changes/<arg>", QHttpServerRequest::Method::Get, [this](QString projectId, QString commitId, QString changeId, const QHttpServerRequest& request)->QHttpServerResponse { return getCommitChangeById(projectId, commitId, changeId, request); });
			HttpServer->route("/projects/<arg>/commits/<arg>/changes/<arg>/", QHttpServerRequest::Method::Get, [this](QString projectId, QString commitId, QString changeId, const QHttpServerRequest& request)->QHttpServerResponse { return getCommitChangeById(projectId, commitId, changeId, request); });

			HttpServer->route("/projects/<arg>/branches", QHttpServerRequest::Method::Post, [this](QString projectId, const QHttpServerRequest& request)->QHttpServerResponse { return postBranch(projectId, request); });
			HttpServer->route("/projects/<arg>/branches/", QHttpServerRequest::Method::Post, [this](QString projectId, const QHttpServerRequest& request)->QHttpServerResponse { return postBranch(projectId, request); });

			HttpServer->route("/projects/<arg>/branches/<arg>", QHttpServerRequest::Method::Get, [this](QString projectId, QString branchId, const QHttpServerRequest& request)->QHttpServerResponse { return getBranchWithId(projectId, branchId, request); });
			HttpServer->route("/projects/<arg>/branches/<arg>/", QHttpServerRequest::Method::Get, [this](QString projectId, QString branchId, const QHttpServerRequest& request)->QHttpServerResponse { return getBranchWithId(projectId, branchId, request); });

			HttpServer->route("/projects/<arg>/branches/<arg>", QHttpServerRequest::Method::Delete, [this](QString projectId, QString branchId, const QHttpServerRequest& request)->QHttpServerResponse { return deleteBranch(projectId, branchId, request); });
			HttpServer->route("/projects/<arg>/branches/<arg>/", QHttpServerRequest::Method::Delete, [this](QString projectId, QString branchId, const QHttpServerRequest& request)->QHttpServerResponse { return deleteBranch(projectId, branchId, request); });

			HttpServer->route("/projects/<arg>/tags", QHttpServerRequest::Method::Get, [this](QString projectId, const QHttpServerRequest& request)->QHttpServerResponse { return getTags(projectId, request); });
			HttpServer->route("/projects/<arg>/tags/", QHttpServerRequest::Method::Get, [this](QString projectId, const QHttpServerRequest& request)->QHttpServerResponse { return getTags(projectId, request); });

			HttpServer->route("/projects/<arg>/tags/<arg>", QHttpServerRequest::Method::Get, [this](QString projectId, QString tagId, const QHttpServerRequest& request)->QHttpServerResponse { return getTagsById(projectId, tagId, request); });
			HttpServer->route("/projects/<arg>/tags/<arg>", QHttpServerRequest::Method::Get, [this](QString projectId, QString tagId, const QHttpServerRequest& request)->QHttpServerResponse { return getTagsById(projectId, tagId, request); });

		}

	private:
		QHttpServerResponse getCommits(const QString& projectId,const QHttpServerRequest&)
		{
			const auto& project = ProjectNavigationService->getProjectById(boost::uuids::string_generator()(projectId.toStdString()));
			const auto& commits = ProjectVerService->getCommits(project);
			std::string returnValue = "[\r\n";
			for (size_t i = 0; i < commits.size(); i++)
			{
				returnValue += commits[i]->serializeToJson();
				if (i < (commits.size() - 1))
					returnValue += ",\r\n";
			}
			returnValue += "]";
			QHttpServerResponse response(QString::fromStdString(returnValue));
			return response;
		}

		QHttpServerResponse postCommit(const QString& projectId, const QHttpServerRequest& request)
		{
			const auto commitRequest = std::make_shared<SysMLv2::REST::CommitRequest>(request.body().toStdString());
			const auto project = ProjectNavigationService->getProjectById(boost::uuids::string_generator()(projectId.toStdString()));
			auto branch = project->getDefaultBranch();
			if (request.query().hasQueryItem("branch"))
			{
				const auto branches = ProjectVerService->getBranches(project);
				for (const auto& _branch : branches)
					if (QString::fromStdString(_branch->getName())==request.query().queryItemValue("branch"))
						branch = _branch;
			}
			const auto commit = ProjectVerService->createCommit(commitRequest->description(), commitRequest->changeRequested(), project, branch);
			auto response = QHttpServerResponse(QString::fromStdString(commit->serializeToJson()),QHttpServerResponder::StatusCode::Created);
			return response;
		}

		QHttpServerResponse getBranchWithId(const QString& projectId, const QString& branchId, const QHttpServerRequest&)
		{
			const auto project = ProjectNavigationService->getProjectById(boost::uuids::string_generator()(projectId.toStdString()));
			auto _branchId = boost::uuids::string_generator()(branchId.toStdString());
			const auto branch = ProjectVerService->getBranchById(project, _branchId);
			return QHttpServerResponse(QString::fromStdString(branch->serializeToJson()));
		}

		QHttpServerResponse getCommitId(const QString& projectId, const QString& commitId, const QHttpServerRequest&)
		{
			const auto project = ProjectNavigationService->getProjectById(boost::uuids::string_generator()(projectId.toStdString()));
			auto _commitId = boost::uuids::string_generator()(commitId.toStdString());
			const auto commit = ProjectVerService->getCommitById(project, _commitId);
			return QHttpServerResponse(QString::fromStdString(commit->serializeToJson()));
		}

		QHttpServerResponse getCommitChange(const QString& projectId, const QString& commitId, const QHttpServerRequest& request)
		{
			const auto project = ProjectNavigationService->getProjectById(boost::uuids::string_generator()(projectId.toStdString()));
			auto _commitId = boost::uuids::string_generator()(commitId.toStdString());
			const auto commit = ProjectVerService->getCommitById(project, _commitId);
			std::vector<std::shared_ptr<SysMLv2::REST::DataVersion>> change;
			if (request.query().hasQueryItem("change_type")&&(!request.query().queryItemValue("change_type").isEmpty())) {
				std::vector<SysMLv2::REST::ChangeType> changeTypes;
				if (request.query().queryItemValue("change_type").contains("CREATED"))
					changeTypes.push_back(SysMLv2::REST::CREATED);
				if (request.query().queryItemValue("change_type").contains("UPDATED"))
					changeTypes.push_back(SysMLv2::REST::UPDATED);
				if (request.query().queryItemValue("change_type").contains("DELETED"))
					changeTypes.push_back(SysMLv2::REST::DELETED);
				change = ProjectVerService->getCommitChange(project,commit,changeTypes);
			}else {
				change = ProjectVerService->getCommitChange(project,commit,{SysMLv2::REST::CREATED,SysMLv2::REST::DELETED,SysMLv2::REST::UPDATED});
			}
			std::string returnValue = "[\r\n";
			for (size_t i = 0; i < change.size(); i++)
			{
				returnValue += change[i]->serializeToJson();
				if (i < (change.size() - 1))
					returnValue += ",\r\n";
			}
			returnValue += "]";
			return QHttpServerResponse(QString::fromStdString(returnValue));
		}

		QHttpServerResponse getCommitChangeById(const QString& projectId, const QString& commitId,QString changeId, const QHttpServerRequest&)
		{
			const auto project = ProjectNavigationService->getProjectById(boost::uuids::string_generator()(projectId.toStdString()));
			auto _commitId = boost::uuids::string_generator()(commitId.toStdString());
			const auto commit = ProjectVerService->getCommitById(project, _commitId);
			auto _changeId = boost::uuids::string_generator()(changeId.toStdString());
			const auto commitChange = ProjectVerService->getCommitChangeById(project,commit,_changeId);
			return QHttpServerResponse(QString::fromStdString(commitChange->serializeToJson()));
		}

		QHttpServerResponse postBranch(const QString& projectId, const QHttpServerRequest& request)
		{
			const auto branchReq = std::make_shared<SysMLv2::REST::BranchRequest>(request.body().toStdString());
			const auto& project = ProjectNavigationService->getProjectById(boost::uuids::string_generator()(projectId.toStdString()));
			const auto& branch = ProjectVerService->createBranch(project, branchReq->getName(),branchReq->getHead());
			return QHttpServerResponse(QString::fromStdString(branch->serializeToJson()));
		}

		QHttpServerResponse deleteBranch(const QString& projectId, const QString& branchId, const QHttpServerRequest&)
		{
			const auto& project = ProjectNavigationService->getProjectById(boost::uuids::string_generator()(projectId.toStdString()));
			auto branchID = boost::uuids::string_generator()(branchId.toStdString());
			const auto branch = ProjectVerService->deleteBranch(project,branchID);
			return QHttpServerResponse(QString::fromStdString(branch->serializeToJson()));
		}

		QHttpServerResponse getTags(const QString& projectId, const QHttpServerRequest&)
		{
			const auto& project = ProjectNavigationService->getProjectById(boost::uuids::string_generator()(projectId.toStdString()));
			auto tags = ProjectVerService->getTags(project);

			std::string returnValue = "[\r\n";
			for (size_t i = 0; i < tags.size(); i++)
			{
				returnValue += tags[i]->serializeToJson();
				if (i < (tags.size() - 1))
					returnValue += ",\r\n";
			}
			returnValue += "]";
			return QHttpServerResponse(QString::fromStdString(returnValue));
		}

		QHttpServerResponse getTagsById(const QString& projectId, const QString& tagId, const QHttpServerRequest&)
		{
			const auto& project = ProjectNavigationService->getProjectById(boost::uuids::string_generator()(projectId.toStdString()));
			auto tagID = boost::uuids::string_generator()(tagId.toStdString());
			const auto& tag = ProjectVerService->getTagById(project,tagID);
			return QHttpServerResponse(QString::fromStdString(tag->serializeToJson()));
		}

		std::shared_ptr<ProjectVersioningService> ProjectVerService;
		std::shared_ptr<ServerProjectService> ProjectNavigationService;
	};
}

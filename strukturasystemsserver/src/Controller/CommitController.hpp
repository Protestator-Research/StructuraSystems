#pragma once

#include "BaseController.hpp"

namespace StructuraSystems::Server
{
	class CommitController : public BaseController
	{
	public:
		CommitController() = delete;
		CommitController(QHttpServer *httpServer) : BaseController(httpServer)
		{
			generateRoutes();
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
		QHttpServerResponse getCommits(const QString& projectId,const QHttpServerRequest& request)
		{
			
		}

		QHttpServerResponse postCommit(const QString& projectId, const QHttpServerRequest& request)
		{

		}

		QHttpServerResponse getBranchWithId(const QString& projectId, const QString& branchId, const QHttpServerRequest& request)
		{

		}

		QHttpServerResponse getCommitId(const QString& projectId, const QString& commitId, const QHttpServerRequest& request)
		{

		}

		QHttpServerResponse getCommitId(const QString& projectId, const QString& commitId, const QHttpServerRequest& request)
		{

		}

		QHttpServerResponse getCommitChange(const QString& projectId, const QString& commitId, const QHttpServerRequest& request)
		{

		}

		QHttpServerResponse getCommitChangeById(const QString& projectId, const QString& commitId, const QHttpServerRequest& request)
		{

		}

		QHttpServerResponse postBranch(const QString& projectId, const QHttpServerRequest& request)
		{

		}

		QHttpServerResponse deleteBranch(const QString& projectId, const QString& branchId, const QHttpServerRequest& request)
		{

		}

		QHttpServerResponse getTags(const QString& projectId, const QHttpServerRequest& request)
		{

		}

		QHttpServerResponse getTagsById(const QString& projectId, const QString& tagId, const QHttpServerRequest& request)
		{

		}
	};
}

//---------------------------------------------------------
// The changes of a commit, computed from the state on the server and the state of a document.
//---------------------------------------------------------
#pragma once
//---------------------------------------------------------
// External Classes
//---------------------------------------------------------
#include <memory>
#include <string>
#include <vector>

namespace SysMLv2::REST {
    class CommitRequest;
    class DataVersion;
}

namespace KerML::Entities {
    class Element;
}

namespace StructuraSystems::Client {
    /**
     * The difference between the elements of a branch head on the server (the base) and the elements of a document.
     * Since the SysML v2 API a commit only contains what is created, changed or deleted.
     */
    struct CommitDelta {
        /** Creations (payload only), updates (identity and payload) and deletions (identity only), in this order. */
        std::vector<std::shared_ptr<SysMLv2::REST::DataVersion>> Changes;
        int Created = 0;
        int Updated = 0;
        int Deleted = 0;

        /** True if there is nothing to commit. The server rejects a commit without changes with 400. */
        [[nodiscard]] bool empty() const { return Changes.empty(); }
    };

    /**
     * Computes the delta that turns the base into the elements of the document.
     * - An element of the document whose id is not in the base is a creation.
     * - An element that is in both is an update if its serialization differs. Both sides are serialized by
     *   Element::serializeToJson and compared as JSON values, so the order of the keys does not matter.
     * - An element of the base that the document does not hold is a deletion, but only if it is a TextualRepresentation. The
     *   document shows (and can remove) only these; all other elements of the commit are kept in the document untouched, so a
     *   missing one can only be a defect of the client, which must not delete data of the server.
     * @param base The elements at the head of the branch on the server.
     * @param document The elements of the document.
     */
    CommitDelta computeCommitDelta(const std::vector<std::shared_ptr<KerML::Entities::Element>> &base,
                                   const std::vector<std::shared_ptr<KerML::Entities::Element>> &document);

    /**
     * Creates the request for a delta. The DataVersions carry no "@id" (the server assigns it) and the previous commit is the
     * head of the branch, which the server adds itself.
     * @param description The description of the commit.
     * @param delta The changes.
     */
    std::shared_ptr<SysMLv2::REST::CommitRequest> buildCommitRequest(const std::string &description, const CommitDelta &delta);

    /**
     * The endpoint of the commits of a project that receives a commit on one branch (POST projects/{projectId}/commits?branchId={branchId}).
     */
    std::string commitEndpoint(const std::string &projectId, const std::string &branchId);
}

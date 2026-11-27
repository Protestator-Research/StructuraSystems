//
// Merged view of the element trees of the cards of one document.
//

#ifndef STRUCTURASYSTEMS_DOCUMENTMERGE_H
#define STRUCTURASYSTEMS_DOCUMENTMERGE_H

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include "../../ViewModels/ElementTreeModel.h"

namespace StructuraSystems::Client {
    /**
     * Plain data, no Qt GUI and no KerML types: the parser library gives every card its own root namespace, so a package that is
     * continued in a later card is a different element in the model. This unit builds ONE logical tree from the per-card trees
     * (in card order) without touching the model. It is only a view; the model itself still knows one namespace per card.
     */

    /** The element tree of one card. Nodes are a flat list with parents before their children (see ElementTreeNode). */
    struct MergeCard {
        QString ElementId;                ///< Id of the card (TextualRepresentation)
        QList<ElementTreeNode> Nodes;     ///< Empty if the card has no tree (no code, syntax errors, not parsed)
    };

    /** One node of the merged tree. */
    struct MergedNode {
        QString Type;
        QString Name;                     ///< Empty if the element has no name
        QString QualifiedName;            ///< Names along the path joined by "::" (display only: the merge compares paths, a name may contain "::"), the document root excluded; empty for the root, unnamed nodes and nodes below them. Children of a clashing node have one
        int Parent = -1;                  ///< Index of the parent in MergedModel::Nodes (always smaller than the own index), -1 for the document root
        /**
         * Ids of the cards that declare the element, in card order, without repeats. Exactly one for every element but the
         * document root and merged packages; for those every card that contributes.
         */
        QStringList CardIds;
        bool IsPackage = false;           ///< Type is a package (see DocumentMerge::isPackageType)
        /**
         * True for the root and for named nodes whose ancestors are all mergeable. A node that clashes with an earlier declaration
         * and everything below it is not mergeable, nor are unnamed nodes and what is below them; only mergeable nodes are merged
         * and checked for duplicates.
         */
        bool Mergeable = false;

        friend bool operator==(const MergedNode &, const MergedNode &) = default;
    };

    /** A name that a later card declares although an earlier one already does. */
    struct MergeFinding {
        QString QualifiedName;
        QString CardId;                   ///< The later card, which gets the finding
        QString FirstCardId;              ///< The first card in document order that declares the name

        friend bool operator==(const MergeFinding &, const MergeFinding &) = default;
    };

    /** A package of a card that was declared by an earlier card first. */
    struct ContinuedPackage {
        QString QualifiedName;
        QString FirstCardId;              ///< The first card that declares the package

        friend bool operator==(const ContinuedPackage &, const ContinuedPackage &) = default;
    };

    struct MergedModel {
        /**
         * Pre-order list; empty if no card has a tree. Index 0 is the document root (type of the root of the first card with a
         * tree, no name, all cards with a tree as contributors).
         */
        QList<MergedNode> Nodes;
        /** Ordered by card order, then by position in the card; one finding per (name, card). Duplicates inside one card are not reported. */
        QList<MergeFinding> Findings;
        /** Per card id (only cards that continue a package): the packages it continues, in the order of the card's tree. */
        QHash<QString, QList<ContinuedPackage>> Continued;

        friend bool operator==(const MergedModel &, const MergedModel &) = default;
    };

    namespace DocumentMerge {
        /**
         * The Type strings of the KerML parser for packages: "Package" and "LibraryPackage". (A "Namespace", which the parser
         * uses for the root namespace, is not a package.)
         */
        [[nodiscard]] bool isPackageType(const QString &type);

        /**
         * Merges the trees of the cards, given in document order.
         *
         * - The roots (Parent == -1) of all cards are one document root, whatever their name.
         * - Packages with the same qualified name become one node that lists all contributing cards; their children are merged
         *   recursively. A package that appears twice in one card is one node with that card once.
         * - Any other named element whose qualified name another (earlier) card declares is a duplicate: it keeps its own node,
         *   attributed to its card, is reported on the later card and is not merged further (its children stay below it).
         *   This includes package against non-package. The first declaration in card order is the one the others refer to.
         * - A node without a name is not mergeable, nor are its descendants; they stay below their parent, attributed to their card.
         */
        [[nodiscard]] MergedModel merge(const QList<MergeCard> &cards);
    }
}

#endif //STRUCTURASYSTEMS_DOCUMENTMERGE_H

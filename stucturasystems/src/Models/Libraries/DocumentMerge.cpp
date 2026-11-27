//
// Merged view of the element trees of the cards of one document.
//

#include "DocumentMerge.h"

#include <vector>

#include <QSet>

namespace StructuraSystems::Client {
    namespace DocumentMerge {
        bool isPackageType(const QString &type) {
            return type == QLatin1String("Package") || type == QLatin1String("LibraryPackage");
        }

        namespace {
            /** The first declaration of a qualified name in card order. */
            struct Declaration {
                int Node = -1;        ///< Index in the (not yet ordered) merged list
                int Card = -1;
                bool IsPackage = false;
            };

            /** Merged node plus its children, only while merging. */
            struct Draft {
                MergedNode Node;
                QList<int> Children;
            };

            void appendCard(QStringList &cards, const QString &id) {
                if (!cards.contains(id))
                    cards.append(id);
            }

            /** Writes the drafts in pre-order (children in the order they were added) and remaps the parents. */
            QList<MergedNode> inPreOrder(const std::vector<Draft> &drafts) {
                QList<MergedNode> ordered;
                ordered.reserve(qsizetype(drafts.size()));
                // Iterative: the depth of a tree is not ours to bound.
                struct Frame {
                    int Draft;
                    int Parent;
                };
                std::vector<Frame> stack{{0, -1}};
                while (!stack.empty()) {
                    const Frame frame = stack.back();
                    stack.pop_back();
                    MergedNode node = drafts[size_t(frame.Draft)].Node;
                    node.Parent = frame.Parent;
                    const int index = int(ordered.size());
                    ordered.append(std::move(node));
                    const auto &children = drafts[size_t(frame.Draft)].Children;
                    for (qsizetype i = children.size() - 1; i >= 0; i--)
                        stack.push_back({children.at(i), index});
                }
                return ordered;
            }
        }

        MergedModel merge(const QList<MergeCard> &cards) {
            MergedModel model;
            std::vector<Draft> drafts;
            // Identity of a declaration is the PATH (list of names), not the "::"-joined display string: a name may contain "::".
            QHash<QStringList, Declaration> declared;          // path -> first declaration; only used for lookups
            QHash<QString, QSet<QStringList>> continuedSeen;   // card id -> paths already listed in Continued
            QHash<QString, QSet<QStringList>> reported;        // card id -> paths already reported: one finding per name and card

            for (int cardIndex = 0; cardIndex < cards.size(); cardIndex++) {
                const MergeCard &card = cards.at(cardIndex);
                if (card.Nodes.isEmpty())
                    continue;

                if (drafts.empty()) {
                    Draft root;
                    root.Node.Type = card.Nodes.first().Type;
                    root.Node.Mergeable = true;
                    drafts.push_back(std::move(root));
                }
                appendCard(drafts[0].Node.CardIds, card.ElementId);

                // Per node of the card: the merged node it ended up in, whether its qualified name is defined (named, with named
                // ancestors), whether it takes part in the merge (known, and not a clash or below one) and its path (mergeable only).
                std::vector<int> mergedOf(size_t(card.Nodes.size()), 0);
                std::vector<bool> known(size_t(card.Nodes.size()), false);
                std::vector<bool> mergeable(size_t(card.Nodes.size()), false);
                std::vector<QStringList> pathOf(size_t(card.Nodes.size()));

                for (int i = 0; i < card.Nodes.size(); i++) {
                    const ElementTreeNode &node = card.Nodes.at(i);
                    if (node.Parent == -1) {
                        // A root: every root of every card is the document root.
                        mergedOf[size_t(i)] = 0;
                        known[size_t(i)] = true;
                        mergeable[size_t(i)] = true;
                        continue;
                    }

                    // Malformed input (a parent that does not precede its child, or a negative index other than -1): the node is
                    // taken as a direct child of the document root instead of being dropped.
                    const bool validParent = node.Parent >= 0 && node.Parent < i;
                    const int parentDraft = validParent ? mergedOf[size_t(node.Parent)] : 0;
                    const bool parentKnown = validParent ? bool(known[size_t(node.Parent)]) : true;
                    const bool parentMergeable = validParent ? bool(mergeable[size_t(node.Parent)]) : true;
                    const QStringList parentPath = validParent ? pathOf[size_t(node.Parent)] : QStringList();
                    const bool isPackage = isPackageType(node.Type);

                    const auto addNode = [&](bool isKnown, bool canMerge, const QString &qualified) {
                        Draft draft;
                        draft.Node.Type = node.Type;
                        draft.Node.Name = node.Name;
                        draft.Node.QualifiedName = qualified;
                        draft.Node.Parent = parentDraft;
                        draft.Node.CardIds = {card.ElementId};
                        draft.Node.IsPackage = isPackage;
                        draft.Node.Mergeable = canMerge;
                        const int index = int(drafts.size());
                        drafts.push_back(std::move(draft));
                        drafts[size_t(parentDraft)].Children.append(index);
                        mergedOf[size_t(i)] = index;
                        known[size_t(i)] = isKnown;
                        mergeable[size_t(i)] = canMerge;
                        return index;
                    };

                    if (node.Name.isEmpty() || !parentKnown) {
                        addNode(false, false, QString());
                        continue;
                    }

                    const QString &parentName = drafts[size_t(parentDraft)].Node.QualifiedName;
                    const QString qualified = parentName.isEmpty() ? node.Name : parentName + QLatin1String("::") + node.Name;
                    if (!parentMergeable) {
                        // Below a clash: the display name is defined, but nothing is merged or reported here.
                        addNode(true, false, qualified);
                        continue;
                    }

                    QStringList path = parentPath;
                    path.append(node.Name);
                    const auto found = declared.constFind(path);
                    if (found == declared.constEnd()) {
                        const int index = addNode(true, true, qualified);
                        pathOf[size_t(i)] = path;
                        declared.insert(path, {index, cardIndex, isPackage});
                        continue;
                    }

                    const Declaration first = found.value();
                    if (first.IsPackage && isPackage) {
                        // The same logical package: this card contributes to it.
                        appendCard(drafts[size_t(first.Node)].Node.CardIds, card.ElementId);
                        mergedOf[size_t(i)] = first.Node;
                        known[size_t(i)] = true;
                        mergeable[size_t(i)] = true;
                        pathOf[size_t(i)] = path;
                        if (first.Card != cardIndex && !continuedSeen[card.ElementId].contains(path)) {
                            continuedSeen[card.ElementId].insert(path);
                            model.Continued[card.ElementId].append({qualified, cards.at(first.Card).ElementId});
                        }
                        continue;
                    }

                    // A clash (or a repetition inside one card, which is not ours to report): its own node, not merged further.
                    addNode(true, false, qualified);
                    if (first.Card != cardIndex && !reported[card.ElementId].contains(path)) {
                        reported[card.ElementId].insert(path);
                        model.Findings.append({qualified, card.ElementId, cards.at(first.Card).ElementId});
                    }
                }
            }

            if (!drafts.empty())
                model.Nodes = inPreOrder(drafts);
            return model;
        }
    }
}

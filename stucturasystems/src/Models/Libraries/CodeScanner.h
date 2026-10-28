//
// Created by Moritz Herzog on 24.10.26.
//

#ifndef STRUCTURASYSTEMS_CODESCANNER_H
#define STRUCTURASYSTEMS_CODESCANNER_H

#include <QList>
#include <QString>
#include <QStringList>

namespace StructuraSystems::Client {
    /** A package declared on the top level of a SysML / KerML text. */
    struct DeclaredPackage {
        QString Name;            ///< Without the quotes of a quoted name.
        bool IsLibrary = false;  ///< Declared as `library package` or `standard library package`.
    };

    /** Result of the lightweight scan of one text. */
    struct ScanResult {
        QList<DeclaredPackage> Packages;
        /** First segments of all qualified names (`A::b::c` gives `A`), without the quotes of quoted names. Without duplicates. */
        QStringList References;
    };

    /** One SysML or KerML code block of a project file. */
    struct CodeBlock {
        QString Language;   ///< "SysMLv2" or "KerML"
        QString Text;
    };

    /**
     * Lightweight lexical scan of SysML v2 / KerML texts and project files. It does not parse: it only tokenizes, skips
     * comments and finds the top level package declarations and the root segments of qualified names. Thread safe.
     */
    class CodeScanner {
    public:
        /** Scans one text. Unbalanced braces or unknown input never fail, the scan simply finds what it can. */
        static ScanResult scan(const QString &text);

        /**
         * Reads the SysML / KerML code blocks of a project file. A .sysml file is one SysMLv2 block, a .kerml
         * file one KerML block and a .md file contributes its fenced code blocks of those languages (a fence without
         * language counts as SysMLv2, like in the editor).
         * @return the blocks, empty if the file cannot be read or has another suffix.
         */
        static QList<CodeBlock> codeBlocksOfFile(const QString &absolutePath);

        /** @return "SysMLv2", "KerML" or an empty string for languages that are no SysML / KerML (Markdown, YAML, ...). */
        static QString codeLanguage(const QString &language);

        /** @return true for the suffixes of the files that can contain SysML / KerML (sysml, kerml, md). */
        static bool isProjectFile(const QString &fileName);

        /** Scan of all code blocks of a file, combined. */
        static ScanResult scanFile(const QString &absolutePath);
    };
}

#endif //STRUCTURASYSTEMS_CODESCANNER_H

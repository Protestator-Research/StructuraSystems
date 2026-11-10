//
// Created by Moritz Herzog on 30.10.26.
//

#ifndef STRUCTURASYSTEMS_LANGUAGEKEYWORDS_H
#define STRUCTURASYSTEMS_LANGUAGEKEYWORDS_H

#include <QSet>
#include <QString>

namespace StructuraSystems::Client {
    /**
     * The keywords of SysMLv2, read once from the vocabulary of the ANTLR lexer (all KEYWORD_* literals). Thread safe.
     */
    const QSet<QString> &sysmlKeywords();

    /** The keywords of KerML, read once from the vocabulary of the ANTLR lexer (all KEYWORD_* literals). Thread safe. */
    const QSet<QString> &kermlKeywords();
}

#endif //STRUCTURASYSTEMS_LANGUAGEKEYWORDS_H

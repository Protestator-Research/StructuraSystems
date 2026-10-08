//
// Lets CHECK_EQ print Qt strings.
//

#ifndef STRUCTURASYSTEMS_QTPRINTING_H
#define STRUCTURASYSTEMS_QTPRINTING_H

#include <QString>
#include <QStringList>

#include <ostream>

inline std::ostream &operator<<(std::ostream &stream, const QString &text) {
    return stream << text.toStdString();
}

inline std::ostream &operator<<(std::ostream &stream, const QStringList &list) {
    return stream << list.join(QStringLiteral("\n")).toStdString();
}

#endif //STRUCTURASYSTEMS_QTPRINTING_H

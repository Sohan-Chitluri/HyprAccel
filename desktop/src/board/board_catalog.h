#pragma once

#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

namespace Hypr {

struct Pin {
    QString name;
    // Scalar capabilities: gpio/pwm/adc. Bus signals: type.instance.role.
    QStringList functions;
};

struct Resource {
    QString id;
    QString type;
    QMap<QString, QString> signals;
};

struct Board {
    QString id;
    QString name;
    QString architecture;
    int clockMHz = 0;
    QList<Pin> pins;
    QList<Resource> resources;
};

class BoardCatalog {
public:
    // Descriptor capabilities only: pins do not imply physical accessibility or
    // package geometry. Throws std::runtime_error for I/O or invalid descriptors.
    static QList<Board> load(const QString &path);
};

} // namespace Hypr

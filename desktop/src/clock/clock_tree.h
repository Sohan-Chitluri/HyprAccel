#pragma once

#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

// CONTRACT (owned by the integrator; implement in clock_tree.cpp). Clock data
// lives in boards.yaml under boards.<id>.clocks; the schema is yours to define
// but must map onto these structs.
namespace Hypr {

struct ClockNode {
    enum class Kind { Source, Mux, Pll, Divider, Gate, Output };
    QString id;              // unique within the board, [a-z0-9_]+
    QString label;           // e.g. "XTAL", "CPU_CLK", "APB_CLK"
    Kind kind = Kind::Source;
    QStringList inputs;      // Source: empty. Mux: candidate inputs. Others: exactly one.
    double freqMHz = 0;      // Source only.
    QList<double> options;   // Pll multipliers / Divider divisors / Mux: unused. Empty = fixed.
    QString defaultValue;    // Mux: input id. Pll/Divider: number as text. Else empty.
    bool editable = false;   // false when the datasheet fixes it or it is unverified.
    double maxMHz = 0;       // 0 = no documented limit.
    QString reference;       // datasheet/TRM citation, shown in the UI.
    QString note;            // caveats, e.g. "unverified", "fixed by ROM bootloader".
    // Hardware-coupled selection (Mux/Divider only, never editable): the value
    // is looked up in followMap by the effective selection of node `follows`
    // (an earlier Mux/Pll/Divider). E.g. ESP32 APB_CLK tracks the CPU source.
    QString follows;
    QMap<QString, QString> followMap;
};

struct ClockTree {
    QString boardId;
    QList<ClockNode> nodes;  // topologically ordered: inputs precede consumers.
    QStringList peripheralOutputs; // Output node ids shown as "peripheral clocks".
};

// Persisted state: nodeId -> chosen value (Mux: input id; Pll/Divider: number as text).
// Only editable nodes appear; absent entries mean the node's defaultValue.
struct ClockConfig {
    QMap<QString, QString> selections;
    bool operator==(const ClockConfig &other) const { return selections == other.selections; }
};

struct ClockResult {
    QMap<QString, double> freqMHz;  // every node id -> resulting frequency
    QStringList errors;             // e.g. "CPU_CLK 260 MHz exceeds max 240 MHz"
};

// Uses selections for editable nodes, defaults otherwise; invalid selections
// fall back to defaults and add an error.
ClockResult computeClocks(const ClockTree &tree, const ClockConfig &config);
// Default config (empty selections is equivalent, but this is explicit).
ClockConfig defaultClockConfig(const ClockTree &tree);

class ClockCatalog {
public:
    // Parses boards.<id>.clocks for every board that has one. Boards without a
    // clocks section are absent from the map. Throws std::runtime_error with a
    // "boards.<id>.clocks..." location on malformed data or unknown/cyclic inputs.
    static QMap<QString, ClockTree> load(const QString &yamlPath);
};

} // namespace Hypr

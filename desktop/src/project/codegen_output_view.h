#pragma once

// Displays a GraphCodegenRunner result: the generated C on success, the
// error text on failure, a status line, and the D3 notice (desktop/docs/
// mbd_graph_contract.md decision D3) that desktop Generate skips the
// server's validateGraphHardwareResources checks.

#include "graph_codegen_runner.h"

#include <QWidget>

class QLabel;
class QPlainTextEdit;

namespace Hypr {

class CodegenOutputView : public QWidget {
    Q_OBJECT
public:
    explicit CodegenOutputView(QWidget *parent = nullptr);

    // The exact sentence appended to desktop/README.md's "Known gaps" (D3).
    static QString knownGapsNotice();

public Q_SLOTS:
    void showResult(const Hypr::GraphCodegenResult &result);
    void showRunning();
    // Back to the idle "no generation run yet" state (e.g. on graph switch).
    void clear();

private:
    QLabel *statusLabel_ = nullptr;
    QPlainTextEdit *outputEdit_ = nullptr;
    QLabel *noticeLabel_ = nullptr;
};

} // namespace Hypr

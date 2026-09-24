#include "codegen_output_view.h"

#include <QFont>
#include <QFontDatabase>
#include <QLabel>
#include <QPlainTextEdit>
#include <QVBoxLayout>

namespace Hypr {

QString CodegenOutputView::knownGapsNotice()
{
    return QStringLiteral(
        "Desktop Generate does not yet check for a stale target board or "
        "unconfigured hardware resources, which the web server refuses "
        "before codegen.");
}

CodegenOutputView::CodegenOutputView(QWidget *parent) : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);

    statusLabel_ = new QLabel(tr("No generation run yet."), this);
    layout->addWidget(statusLabel_);

    outputEdit_ = new QPlainTextEdit(this);
    outputEdit_->setReadOnly(true);
    outputEdit_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    outputEdit_->setLineWrapMode(QPlainTextEdit::NoWrap);
    layout->addWidget(outputEdit_, 1);

    noticeLabel_ = new QLabel(knownGapsNotice(), this);
    noticeLabel_->setWordWrap(true);
    layout->addWidget(noticeLabel_);
}

void CodegenOutputView::clear()
{
    statusLabel_->setText(tr("No generation run yet."));
    outputEdit_->clear();
}

void CodegenOutputView::showRunning()
{
    statusLabel_->setText(tr("Generating…"));
    outputEdit_->clear();
}

void CodegenOutputView::showResult(const GraphCodegenResult &result)
{
    if (!result.unavailableReason.isEmpty()) {
        statusLabel_->setText(tr("Generate unavailable: %1").arg(result.unavailableReason));
        outputEdit_->setPlainText(result.stdErr);
        return;
    }
    if (result.ok) {
        statusLabel_->setText(tr("Generated (exit code %1).").arg(result.exitCode));
        outputEdit_->setPlainText(result.source);
    } else {
        statusLabel_->setText(tr("Generation failed (exit code %1).").arg(result.exitCode));
        outputEdit_->setPlainText(result.stdErr);
    }
}

} // namespace Hypr

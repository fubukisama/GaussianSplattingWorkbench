#pragma once

// QA only. Read public control geometry, real resolved text fonts and the
// exposed window's client pixels. Do not draw a synthetic indicator here.
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFontInfo>
#include <QFontMetricsF>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QPixmap>
#include <QScreen>
#include <QStyleOptionButton>
#include <QStyleOptionComboBox>
#include <QStyleOptionSpinBox>
#include <QWindow>
#include <cmath>

namespace gsw::grid_qa {
inline QJsonArray rect(const QWidget &owner, const QWidget &widget, const QRect &local) {
  const QPoint origin = owner.mapFromGlobal(widget.mapToGlobal(local.topLeft()));
  return {origin.x(), origin.y(), local.width(), local.height()};
}

inline QJsonArray indicator(const QWidget &owner, const QCheckBox &box) {
  QStyleOptionButton option;
  option.initFrom(&box);
  option.text = box.text();
  option.state |= box.isChecked() ? QStyle::State_On : QStyle::State_Off;
  return rect(owner, box, box.style()->subElementRect(QStyle::SE_CheckBoxIndicator, &option, &box));
}

inline QJsonArray arrow(const QWidget &owner, const QComboBox &box) {
  QStyleOptionComboBox option;
  option.initFrom(&box);
  option.currentText = box.currentText();
  option.editable = box.isEditable();
  return rect(owner, box, box.style()->subControlRect(QStyle::CC_ComboBox, &option, QStyle::SC_ComboBoxArrow, &box));
}

inline QJsonArray step(const QWidget &owner, const QDoubleSpinBox &box, QStyle::SubControl which) {
  QStyleOptionSpinBox option;
  option.initFrom(&box);
  option.buttonSymbols = box.buttonSymbols();
  option.stepEnabled = QAbstractSpinBox::StepUpEnabled | QAbstractSpinBox::StepDownEnabled;
  return rect(owner, box, box.style()->subControlRect(QStyle::CC_SpinBox, &option, which, &box));
}

inline bool key(int value, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
  QWidget *focus = QApplication::focusWidget();
  if (!focus) return false;
  QKeyEvent down(QEvent::KeyPress, value, modifiers);
  QApplication::sendEvent(focus, &down);
  focus = QApplication::focusWidget();
  if (!focus) return false;
  QKeyEvent up(QEvent::KeyRelease, value, modifiers);
  QApplication::sendEvent(focus, &up);
  return true;
}

inline QJsonObject fontFacts(const QFont &font, const QPaintDevice &device) {
  const QFontInfo info(font);
  const QFontMetricsF metrics(font, &device);
  return {{QStringLiteral("family"), info.family()}, {QStringLiteral("pt"), info.pointSizeF()},
          {QStringLiteral("narrowLatin"), metrics.horizontalAdvance(QStringLiteral("iiii"))},
          {QStringLiteral("wideLatin"), metrics.horizontalAdvance(QStringLiteral("WWWW"))}};
}

inline bool screenshot(QDialog &dialog, const QDir &out, const QString &file) {
  if (!dialog.isVisible() || !dialog.isActiveWindow() || !dialog.windowHandle() ||
      !dialog.windowHandle()->isExposed() || !dialog.screen() ||
      !dialog.screen()->availableGeometry().contains(
          QRect(dialog.mapToGlobal(QPoint()), dialog.size()))) return false;
  const QPixmap pixels = dialog.screen()->grabWindow(dialog.winId());
  const qreal dpr = dialog.devicePixelRatioF();
  if (pixels.isNull() || std::abs(pixels.width() - dialog.width() * dpr) > 1 ||
      std::abs(pixels.height() - dialog.height() * dpr) > 1) return false;
  return pixels.save(out.filePath(file), "PNG");
}

inline bool writeFacts(const QDir &out, const QJsonObject &facts) {
  QFile file(out.filePath(QStringLiteral("facts.json")));
  const QByteArray bytes = QJsonDocument(facts).toJson();
  return file.open(QIODevice::WriteOnly | QIODevice::NewOnly) && file.write(bytes) == bytes.size() && file.flush();
}
} // namespace gsw::grid_qa

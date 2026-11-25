#pragma once

#include <QAbstractScrollArea>
#include <QScrollBar>
#include <QStyle>
#include <QStyleOptionSlider>

namespace gsw::tests {

struct ScrollBarClearance {
  QRect groove;
  QRect thumb;
  int contentGap = 0;
};

// Measure the actual themed track, not the containing widget's rectangle:
// the latter includes the non-interactive space requested from the layout.
inline ScrollBarClearance measureScrollBarClearance(const QAbstractScrollArea &area,
                                                    const QScrollBar &bar) {
  QStyleOptionSlider option;
  option.initFrom(&bar);
  option.orientation = bar.orientation();
  option.minimum = bar.minimum();
  option.maximum = bar.maximum();
  option.sliderPosition = bar.sliderPosition();
  option.sliderValue = bar.value();
  option.singleStep = bar.singleStep();
  option.pageStep = bar.pageStep();
  option.upsideDown = bar.invertedAppearance();
  if (bar.orientation() == Qt::Horizontal) option.state |= QStyle::State_Horizontal;
  option.subControls = QStyle::SC_All;
  const auto subControl = [&](QStyle::SubControl control) {
    return bar.style()->subControlRect(QStyle::CC_ScrollBar, &option, control, &bar);
  };
  ScrollBarClearance result{subControl(QStyle::SC_ScrollBarGroove),
                            subControl(QStyle::SC_ScrollBarSlider)};
  if (result.groove.isEmpty()) return result;
  const QRect view(bar.mapFromGlobal(area.viewport()->mapToGlobal(QPoint())),
                   area.viewport()->size());
  if (bar.orientation() == Qt::Horizontal) {
    result.contentGap = result.groove.top() - view.bottom() - 1;
  } else if (area.isRightToLeft()) {
    result.contentGap = view.left() - result.groove.right() - 1;
  } else {
    result.contentGap = result.groove.left() - view.right() - 1;
  }
  return result;
}

} // namespace gsw::tests

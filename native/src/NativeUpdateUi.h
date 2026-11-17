// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
class QMenu;
class QWidget;
namespace gsw {
// Adds one Help entry and a background controller. No startup popup, foreground
// activation, application restart or training operation is performed.
void installNativeUpdateUi(QMenu *helpMenu, QWidget *window);
// Arm the shutdown installer only when the workbench's existing exit and
// project/task guards accept a normal close. An ignored close disarms it.
void setNativeUpdateCloseAccepted(QWidget *window, bool accepted);
}

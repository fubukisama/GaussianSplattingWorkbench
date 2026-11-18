#pragma once

class QString;

namespace gsw {
class NativeViewport;
bool runTrainingIsolationSmokeTest(NativeViewport &viewport);
bool runLichtFeldPreviewSmokeTest(NativeViewport &viewport);
bool runLiveTrainingSmokeTest(NativeViewport &viewport, const QString &configPath);
}

#pragma once
#include <QDialog>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <memory>
namespace gsw {
class ReconstructionReviewDialog final : public QDialog {
  Q_OBJECT
public:
  ReconstructionReviewDialog(const QString &dataset, const QString &python,
      const QString &backendRoot, const QProcessEnvironment &environment,
      const QString &cameraDataset = {}, const QString &identity = {}, QWidget *parent = nullptr);
  ~ReconstructionReviewDialog() override;
  [[nodiscard]] QJsonObject report() const;
  [[nodiscard]] bool busy() const;
  void inspect(const QString &group = {});
  void setModelDirectory(const QString &directory);
signals:
  void inspectionFinished();
protected:
  void hideEvent(QHideEvent *) override;
  void resizeEvent(QResizeEvent *) override;
private:
  class State; std::unique_ptr<State> m;
  void cancel();
  void request(const QString &group, const QString &action = {}, const QString &rowId = {});
  void refreshLanguage();
  void selectCurrent();
  void loadImage(bool fullSize);
  void locate();
  bool stale() const;
  void invalidate();
};
}

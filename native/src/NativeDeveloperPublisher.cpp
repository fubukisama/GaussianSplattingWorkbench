// SPDX-License-Identifier: GPL-3.0-or-later
#include "NativeActivationCore.h"
#include "NativeUnanchoredBootstrap.h"
namespace gsw {
NativeActivationResult NativeDeveloperPublisher::publish(const QString &root,
    const QString &archive, const QByteArray &offer, const NativeInstallationLock &lock) {
  try {
    if (!lock.locked() || lock.installationRoot() != QDir::cleanPath(root))
      activation::fail("activation_lock_required");
    requireUnanchoredBootstrap(root);
    return NativeActivationCore::publish(root, archive, offer, lock, NativeActivationCore::Admission::LocalDeveloper);
  } catch (const activation::Failure &error) { return {false, error.code, {}}; }
    catch (const activation::Json::exception &) { return {false, QStringLiteral("activation_json_invalid"), {}}; }
    catch (...) { return {false, QStringLiteral("developer_publication_failed"), {}}; }
}
} // namespace gsw

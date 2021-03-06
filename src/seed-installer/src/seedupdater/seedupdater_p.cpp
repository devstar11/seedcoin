#include "seedupdater.h"
#include "seedupdater_p.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QFileInfo>
#include <QtCore/QDir>
#include <QtCore/QXmlStreamReader>
#include <QtCore/QTimer>

#define REPOSITORY_URL "https://github.com/SEED-Core/seed/releases.atom"

std::atomic<bool> isUpdaterRunning(false);

using namespace QtSeedUpdater;

Q_LOGGING_CATEGORY(logSeedUpdater, "SeedUpdater")

SeedUpdaterPrivate::SeedUpdaterPrivate(SeedUpdater *q_ptr) :
	QObject(nullptr),
	q(q_ptr),
	currentVersion(),
	updateInfos(),
	normalExit(true),
	lastErrorCode(0),
	lastErrorLog(),
	atomFeeder(nullptr),
	currentVersionPos(-1),
	downloadManager(nullptr),
	scheduler(new SimpleScheduler(this)),
	runOnExit(false),
	runArguments(),
	adminAuth(nullptr)
{
	isUpdaterRunning = false;
	connect(qApp, &QCoreApplication::aboutToQuit,
			this, &SeedUpdaterPrivate::onAppAboutToExit,
			Qt::DirectConnection);
	connect(scheduler, &SimpleScheduler::scheduleTriggered,
			this, &SeedUpdaterPrivate::startUpdateCheck);
}

SeedUpdaterPrivate::~SeedUpdaterPrivate()
{
	if (runOnExit)
		qCWarning(logSeedUpdater) << "SeedUpdater destroyed with run on exit active before the application quit";

	if (atomFeeder) {
		delete atomFeeder;
		atomFeeder = nullptr;
	}

	if (downloadManager) {
		delete downloadManager;
		downloadManager = nullptr;
	}
}

bool SeedUpdaterPrivate::startUpdateCheck()
{
	if (isUpdaterRunning) {
		return false;
	}

	updateInfos.clear();
	normalExit = true;
	lastErrorCode = 0;
	lastErrorLog.clear();
	currentVersionPos = -1;

	atomFeeder = new AtomFeeder(REPOSITORY_URL);

	connect(atomFeeder, &AtomFeeder::getVersionListDone,
			this, &SeedUpdaterPrivate::onUpdaterReady);

	atomFeeder->start();

	isUpdaterRunning = true;

	emit q->updateInfoChanged(updateInfos);
	emit q->runningChanged(true);
	return true;
}

void SeedUpdaterPrivate::stopUpdateCheck(int delay, bool async)
{
	if (atomFeeder) {
		if (delay > 0) {
			atomFeeder->stop();
			if (async){
				QTimer::singleShot(delay, this, [this](){
					stopUpdateCheck(0, false);
				});
			} else {
				atomFeeder->stop();
			}
		} else {
			atomFeeder->stop();
		}
	}
}

QString SeedUpdaterPrivate::getDownloadUrl(QString version)
{
#if defined(Q_OS_WIN32)
	//QString fileName = "seed-qt-win32.zip";
	QString fileName = "seed-qt-win.zip";
#elif defined(Q_OS_WIN)
	//QString fileName = "seed-qt-win64.zip";
	QString fileName = "seed-qt-win.zip";
#elif defined(Q_OS_OSX)
	QString fileName = "seed-qt-mac.dmg";
#else
	QString fileName = 1 ? "seed-qt-linux-16.zip" : "seed-qt-linux-18.zip";
#endif
	return "https://github.com/SEED-Core/seed/releases/download/" + version + "/" + fileName;
}

void SeedUpdaterPrivate::onDownloadProgress(DownloadManager::DownloadProgress progress)
{

}

void SeedUpdaterPrivate::onDownloadFinished(DownloadManager::DownloadProgress progress)
{

}

void SeedUpdaterPrivate::onDownloadCheckSize(DownloadManager::DownloadProgress progress)
{
	if (progress.totalSize > 0) {
		SeedUpdater::SeedUpdateInfo updateInfo("Seed wallet", newVersion, progress.totalSize);
		updateInfos.append(updateInfo);

		if (downloadManager) {
			downloadManager->deleteLater();
			downloadManager = nullptr;
		}
		if (atomFeeder) {
			atomFeeder->deleteLater();
			atomFeeder = nullptr;
		}
		emit q->runningChanged(false);
		if (!updateInfos.isEmpty())
			emit q->updateInfoChanged(updateInfos);
		emit q->checkUpdatesDone(!updateInfos.isEmpty(), false);

		isUpdaterRunning = false;
	} else {
		onUpdaterReady();
	}
}

void SeedUpdaterPrivate::onUpdaterReady()
{
	if (atomFeeder) {
		normalExit = true;
		lastErrorCode = 0;
		lastErrorLog = "";
		currentVersionPos++;
		if (currentVersionPos < atomFeeder->getVersionListCount()) {
			QString version = atomFeeder->getVersion(currentVersionPos);
			//if (version != currentVersion)
			{
				if (downloadManager == nullptr) {
					downloadManager = new DownloadManager(this);
					connect(downloadManager, &DownloadManager::downloadFinished,
							this, &SeedUpdaterPrivate::onDownloadCheckSize);
				}
				newVersion = version;
				downloadManager->append(getDownloadUrl(version), true);
				return;
			}
		}
		if (downloadManager) {
			downloadManager->deleteLater();
			downloadManager = nullptr;
		}
		atomFeeder->deleteLater();
		atomFeeder = nullptr;
		emit q->runningChanged(false);
		if (!updateInfos.isEmpty())
			emit q->updateInfoChanged(updateInfos);
		emit q->checkUpdatesDone(!updateInfos.isEmpty(), false);

		isUpdaterRunning = false;
	}
}

void SeedUpdaterPrivate::updaterError()
{
	if (atomFeeder) {
		normalExit = false;
		lastErrorCode = -1;
		lastErrorLog = "Error!";
		atomFeeder->deleteLater();
		atomFeeder = nullptr;

		isUpdaterRunning = false;
		emit q->runningChanged(false);
		emit q->checkUpdatesDone(false, true);
	}
}

void SeedUpdaterPrivate::onAppAboutToExit()
{
	if (runOnExit) {
		QFileInfo appInfo(QCoreApplication::applicationFilePath());
		auto ok = false;
		if (adminAuth && !adminAuth->hasAdminRights())
			ok = adminAuth->executeAsAdmin(appInfo.absoluteFilePath(), runArguments);
		else {
			ok = QProcess::startDetached(appInfo.absoluteFilePath(),
										 runArguments,
										 appInfo.absolutePath());
		}

		if (!ok) {
			qCWarning(logSeedUpdater) << "Unable to start" << appInfo.absoluteFilePath()
										<< "with arguments" << runArguments
										<< "as" << (adminAuth ? "admin/root" : "current user");
		}

		runOnExit = false;//prevent warning
	}
}

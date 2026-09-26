#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QIcon>
#include <QFile>
#include <QCoreApplication>
#include <QEvent>
#include <QFileOpenEvent>

#if defined(FAEDITOR_PRODUCT_FANTOM)
#include "app/FantomAppController.h"
#include "model/FantomSceneModel.h"
#include "midi/MidiDeviceModel.h"
#else
#include "app/AppController.h"
#include "model/PartModel.h"
#include "model/EffectsModel.h"
#include "model/StudioSetModel.h"
#include "model/ToneBrowserModel.h"
#include "model/StudioSetBrowserModel.h"
#include "model/AudioFxModel.h"
#include "model/MfxModel.h"
#include "model/MfxTapDelayModel.h"
#include "model/MfxParamListModel.h"
#include "model/MfxUiHelpers.h"
#include "midi/MidiDeviceModel.h"
#include "project/ProjectStore.h"
#include "project/SvdStudioSetOrder.h"
#include "undo/UndoController.h"
#endif

#include <qqml.h>

#if !defined(FAEDITOR_PRODUCT_FANTOM)
class LaunchUrlFilter : public QObject
{
public:
    explicit LaunchUrlFilter(AppController *controller, QObject *parent = nullptr)
        : QObject(parent)
        , m_controller(controller)
    {
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::FileOpen) {
            const auto *open = static_cast<QFileOpenEvent *>(event);
            if (m_controller && m_controller->handleLaunchUrl(open->url()))
                return true;
        }
        return QObject::eventFilter(watched, event);
    }

private:
    AppController *m_controller = nullptr;
};
#endif

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral(FAEDITOR_ORGANIZATION_NAME));
    app.setOrganizationDomain(QStringLiteral(FAEDITOR_ORGANIZATION_DOMAIN));
    app.setApplicationName(QStringLiteral(FAEDITOR_APPLICATION_NAME));
#if defined(FAEDITOR_PRODUCT_FANTOM)
    app.setApplicationVersion(QStringLiteral(FAEDITOR_APPLICATION_VERSION));
#else
    // Keep the FA release version explicit and regression-tested.
    app.setApplicationVersion(QStringLiteral("1.2"));
#endif

    // Prefer bundle .icns (Dock/Finder), fall back to embedded PNG.
    QIcon appIcon;
#if defined(Q_OS_MACOS)
    {
        const QString icns = QCoreApplication::applicationDirPath()
                             + QStringLiteral("/../Resources/AppIcon.icns");
        if (QFile::exists(icns))
            appIcon.addFile(icns);
    }
#endif
    if (appIcon.isNull() && QStringLiteral(FAEDITOR_PRODUCT_KEY) == QStringLiteral("FA"))
        appIcon.addFile(QStringLiteral(":/qt/qml/FAEditor/resources/icons/appicon.png"));
    if (!appIcon.isNull())
        app.setWindowIcon(appIcon);

    // Qt Quick Controls 2: prefer native macOS style when available.
#if defined(Q_OS_MACOS)
    QQuickStyle::setStyle(app.arguments().contains(QStringLiteral("--mobile-ui"))
                         ? QStringLiteral("Fusion") : QStringLiteral("macOS"));
#else
    QQuickStyle::setStyle(QStringLiteral("Fusion"));
#endif

#if defined(FAEDITOR_PRODUCT_FANTOM)
    qmlRegisterUncreatableType<FantomAppController>("FantomEditor", 1, 0, "FantomAppController",
                                                    QStringLiteral("Use App context property"));
    qmlRegisterUncreatableType<FantomSceneModel>("FantomEditor",1,0,"FantomSceneModel",QStringLiteral("Use App.scene"));
    qmlRegisterUncreatableType<MidiDeviceModel>("FantomEditor",1,0,"MidiDeviceModel",QStringLiteral("Use App.midi"));
    FantomAppController controller;
#else
    qmlRegisterUncreatableType<PartModel>("FAEditor", 1, 0, "PartModel",
                                          QStringLiteral("Obtained from StudioSetModel"));
    qmlRegisterUncreatableType<EffectsModel>("FAEditor", 1, 0, "EffectsModel",
                                             QStringLiteral("Obtained from StudioSetModel"));
    qmlRegisterUncreatableType<StudioSetModel>("FAEditor", 1, 0, "StudioSetModel",
                                               QStringLiteral("Obtained from AppController"));
    qmlRegisterUncreatableType<ToneBrowserModel>("FAEditor", 1, 0, "ToneBrowserModel",
                                                 QStringLiteral("Obtained from AppController"));
    qmlRegisterUncreatableType<StudioSetBrowserModel>("FAEditor", 1, 0, "StudioSetBrowserModel",
                                                      QStringLiteral("Obtained from AppController"));
    qmlRegisterUncreatableType<AudioFxModel>("FAEditor", 1, 0, "AudioFxModel",
                                             QStringLiteral("Obtained from AppController"));
    qmlRegisterUncreatableType<MidiDeviceModel>("FAEditor", 1, 0, "MidiDeviceModel",
                                                QStringLiteral("Obtained from AppController"));
    qmlRegisterUncreatableType<ProjectStore>("FAEditor", 1, 0, "ProjectStore",
                                             QStringLiteral("Obtained from AppController"));
    qmlRegisterUncreatableType<SvdStudioSetOrderModel>("FAEditor", 1, 0, "SvdStudioSetOrderModel",
                                                   QStringLiteral("Use App.studioSetOrder"));
    qmlRegisterUncreatableType<UndoController>("FAEditor", 1, 0, "UndoController",
                                               QStringLiteral("Obtained from AppController"));
    qmlRegisterUncreatableType<AppController>("FAEditor", 1, 0, "AppController",
                                              QStringLiteral("Use App context property"));
    qmlRegisterUncreatableType<FantomSceneModel>("FAEditor",1,0,"FantomSceneModel",
                                                 QStringLiteral("Obtained from AppController"));
    qmlRegisterUncreatableType<MfxModel>("FAEditor", 1, 0, "MfxModel",
                                         QStringLiteral("Obtained from TemporaryToneModel"));
    qmlRegisterUncreatableType<MfxTapDelayModel>("FAEditor", 1, 0, "MfxTapDelayModel",
                                                 QStringLiteral("Obtained from MfxModel.tapDelay"));
    qmlRegisterUncreatableType<MfxParamListModel>("FAEditor", 1, 0, "MfxParamListModel",
                                                  QStringLiteral("Obtained from MfxModel.paramList"));
    qmlRegisterSingletonInstance("FAEditor", 1, 0, "MfxUiFamily", MfxUiHelpers::instance());

    AppController controller;
#endif

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("App"), &controller);
#if !defined(FAEDITOR_PRODUCT_FANTOM)
    app.installEventFilter(new LaunchUrlFilter(&controller, &app));
#endif

    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        []() { QCoreApplication::exit(-1); }, Qt::QueuedConnection);
    #if !defined(FAEDITOR_PRODUCT_FANTOM)
    const bool mobileUi = QGuiApplication::platformName() == QStringLiteral("ios")
        || app.arguments().contains(QStringLiteral("--mobile-ui"));
    engine.loadFromModule(QStringLiteral(FAEDITOR_QML_URI),
                          mobileUi ? QStringLiteral("MobileMain") : QStringLiteral(FAEDITOR_QML_MAIN));
#else
    engine.loadFromModule(QStringLiteral(FAEDITOR_QML_URI), QStringLiteral(FAEDITOR_QML_MAIN));
#endif

    if (engine.rootObjects().isEmpty())
        return -1;

    return app.exec();
}

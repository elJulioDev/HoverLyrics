#include "setup.h"
#include "config.h"
#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QDesktopServices>
#include <QUrl>

namespace setup {

QString clientIdDialog() {
    QDialog dialog;
    dialog.setWindowTitle("Configurar HoverLyrics");
    dialog.setMinimumWidth(560);

    auto* layout = new QVBoxLayout(&dialog);

    auto* title = new QLabel("<h2>Conectá tu Spotify</h2>");
    layout->addWidget(title);

    auto* steps = new QLabel(QString(
        "<p>Necesitás un <b>Client ID</b> gratuito de Spotify. Se hace una sola vez:</p>"
        "<ol>"
        "<li>Entrá a <a href=\"https://developer.spotify.com/dashboard\">"
        "developer.spotify.com/dashboard</a> e iniciá sesión.</li>"
        "<li>Hacé clic en <b>Create app</b>. El nombre y la descripción son lo que quieras.</li>"
        "<li>En <b>Redirect URIs</b> agregá exactamente:<br>"
        "<code>http://127.0.0.1:8888/callback</code></li>"
        "<li>En <b>APIs used</b> marcá <b>Web API</b> y guardá.</li>"
        "<li>Copiá el <b>Client ID</b> y pegalo acá abajo.</li>"
        "</ol>"
        "<p>El <b>Client Secret no se usa</b>: no lo pegues en ningún lado.</p>"));
    steps->setOpenExternalLinks(true);
    steps->setWordWrap(true);
    steps->setTextInteractionFlags(Qt::TextBrowserInteraction);
    layout->addWidget(steps);

    auto* openButton = new QPushButton("Abrir el panel de Spotify en el navegador");
    QObject::connect(openButton, &QPushButton::clicked, []() {
        QDesktopServices::openUrl(QUrl("https://developer.spotify.com/dashboard"));
    });
    layout->addWidget(openButton);

    layout->addWidget(new QLabel("Pegá el Client ID acá:"));
    auto* input = new QLineEdit;
    input->setPlaceholderText("por ejemplo: 1a2b3c4d5e6f7a8b9c0d1e2f3a4b5c6d");
    input->setMinimumWidth(480);
    layout->addWidget(input);

    auto* buttons = new QHBoxLayout;
    buttons->addStretch();
    auto* quit = new QPushButton("Salir");
    auto* save = new QPushButton("Guardar y continuar");
    save->setDefault(true);
    buttons->addWidget(quit);
    buttons->addWidget(save);
    layout->addLayout(buttons);

    QObject::connect(quit, &QPushButton::clicked, &dialog, &QDialog::reject);
    QObject::connect(save, &QPushButton::clicked, &dialog, &QDialog::accept);

    if (dialog.exec() != QDialog::Accepted) return {};
    const QString id = input->text().trimmed();
    if (id.isEmpty()) return {};
    config::saveClientId(id);
    return id;
}

}

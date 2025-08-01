// Copyright (c) 2011-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include <config/mytherra-config.h>
#endif

#include <qt/optionsdialog.h>
#include <qt/forms/ui_optionsdialog.h>

#include <qt/mytherraunits.h>
#include <qt/clientmodel.h>
#include <qt/guiconstants.h>
#include <qt/guiutil.h>
#include <qt/optionsmodel.h>

#include <interfaces/node.h>
#include <validation.h> // for DEFAULT_SCRIPTCHECK_THREADS and MAX_SCRIPTCHECK_THREADS
#include <netbase.h>
#include <txdb.h> // for -dbcache defaults
#include <util/system.h>

#include <chrono>

#include <QDataWidgetMapper>
#include <QDir>
#include <QIntValidator>
#include <QLocale>
#include <QMessageBox>
#include <QSystemTrayIcon>
#include <QTimer>

OptionsDialog::OptionsDialog(QWidget* parent, bool enableWallet)
    : QDialog(parent, GUIUtil::dialog_flags),
      ui(new Ui::OptionsDialog)
{
    ui->setupUi(this);

    /* Main elements init */
    ui->databaseCache->setMinimum(nMinDbCache);
    ui->databaseCache->setMaximum(nMaxDbCache);
    ui->threadsScriptVerif->setMinimum(-GetNumCores());
    ui->threadsScriptVerif->setMaximum(MAX_SCRIPTCHECK_THREADS);
    ui->pruneWarning->setVisible(false);
    ui->pruneWarning->setStyleSheet("QLabel { color: red; }");

    ui->pruneSize->setEnabled(false);
    connect(ui->prune, &QPushButton::toggled, ui->pruneSize, &QWidget::setEnabled);

    /* Network elements init */
#ifndef USE_UPNP
    ui->mapPortUpnp->setEnabled(false);
#endif
#ifndef USE_NATPMP
    ui->mapPortNatpmp->setEnabled(false);
#endif

    ui->proxyIp->setEnabled(false);
    ui->proxyPort->setEnabled(false);
    ui->proxyPort->setValidator(new QIntValidator(1, 65535, this));

    ui->proxyIpTor->setEnabled(false);
    ui->proxyPortTor->setEnabled(false);
    ui->proxyPortTor->setValidator(new QIntValidator(1, 65535, this));

    connect(ui->connectSocks, &QPushButton::toggled, ui->proxyIp, &QWidget::setEnabled);
    connect(ui->connectSocks, &QPushButton::toggled, ui->proxyPort, &QWidget::setEnabled);
    connect(ui->connectSocks, &QPushButton::toggled, this, &OptionsDialog::updateProxyValidationState);

    connect(ui->connectSocksTor, &QPushButton::toggled, ui->proxyIpTor, &QWidget::setEnabled);
    connect(ui->connectSocksTor, &QPushButton::toggled, ui->proxyPortTor, &QWidget::setEnabled);
    connect(ui->connectSocksTor, &QPushButton::toggled, this, &OptionsDialog::updateProxyValidationState);

    /* Window elements init */
#ifdef Q_OS_MACOS
    /* remove Window tab on Mac */
    ui->tabWidget->removeTab(ui->tabWidget->indexOf(ui->tabWindow));
    /* hide launch at startup option on macOS */
    ui->mytherraAtStartup->setVisible(false);
    ui->verticalLayout_Main->removeWidget(ui->mytherraAtStartup);
    ui->verticalLayout_Main->removeItem(ui->horizontalSpacer_0_Main);
#endif

    /* remove Wallet tab and 3rd party-URL textbox in case of -disablewallet */
    if (!enableWallet) {
        ui->tabWidget->removeTab(ui->tabWidget->indexOf(ui->tabWallet));
        ui->thirdPartyTxUrlsLabel->setVisible(false);
        ui->thirdPartyTxUrls->setVisible(false);
    }

#ifdef ENABLE_EXTERNAL_SIGNER
    ui->externalSignerPath->setToolTip(ui->externalSignerPath->toolTip().arg(PACKAGE_NAME));
#else
    //: "External signing" means using devices such as hardware wallets.
    ui->externalSignerPath->setToolTip(tr("Compiled without external signing support (required for external signing)"));
    ui->externalSignerPath->setEnabled(false);
#endif
    /* Display elements init */
    QDir translations(":translations");

    ui->mytherraAtStartup->setToolTip(ui->mytherraAtStartup->toolTip().arg(PACKAGE_NAME));
    ui->mytherraAtStartup->setText(ui->mytherraAtStartup->text().arg(PACKAGE_NAME));

    ui->openMytherraConfButton->setToolTip(ui->openMytherraConfButton->toolTip().arg(PACKAGE_NAME));

    ui->lang->setToolTip(ui->lang->toolTip().arg(PACKAGE_NAME));
    ui->lang->addItem(QString("(") + tr("default") + QString(")"), QVariant(""));
    for (const QString &langStr : translations.entryList())
    {
        QLocale locale(langStr);

        /** check if the locale name consists of 2 parts (language_country) */
        if(langStr.contains("_"))
        {
            /** display language strings as "native language - native country (locale name)", e.g. "Deutsch - Deutschland (de)" */
            ui->lang->addItem(locale.nativeLanguageName() + QString(" - ") + locale.nativeCountryName() + QString(" (") + langStr + QString(")"), QVariant(langStr));
        }
        else
        {
            /** display language strings as "native language (locale name)", e.g. "Deutsch (de)" */
            ui->lang->addItem(locale.nativeLanguageName() + QString(" (") + langStr + QString(")"), QVariant(langStr));
        }
    }
    ui->unit->setModel(new MytherraUnits(this));

    /* Widget-to-option mapper */
    mapper = new QDataWidgetMapper(this);
    mapper->setSubmitPolicy(QDataWidgetMapper::ManualSubmit);
    mapper->setOrientation(Qt::Vertical);

    GUIUtil::ItemDelegate* delegate = new GUIUtil::ItemDelegate(mapper);
    connect(delegate, &GUIUtil::ItemDelegate::keyEscapePressed, this, &OptionsDialog::reject);
    mapper->setItemDelegate(delegate);

    /* setup/change UI elements when proxy IPs are invalid/valid */
    ui->proxyIp->setCheckValidator(new ProxyAddressValidator(parent));
    ui->proxyIpTor->setCheckValidator(new ProxyAddressValidator(parent));
    connect(ui->proxyIp, &QValidatedLineEdit::validationDidChange, this, &OptionsDialog::updateProxyValidationState);
    connect(ui->proxyIpTor, &QValidatedLineEdit::validationDidChange, this, &OptionsDialog::updateProxyValidationState);
    connect(ui->proxyPort, &QLineEdit::textChanged, this, &OptionsDialog::updateProxyValidationState);
    connect(ui->proxyPortTor, &QLineEdit::textChanged, this, &OptionsDialog::updateProxyValidationState);

    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        ui->showTrayIcon->setChecked(false);
        ui->showTrayIcon->setEnabled(false);
        ui->minimizeToTray->setChecked(false);
        ui->minimizeToTray->setEnabled(false);
    }

    QFont embedded_font{GUIUtil::fixedPitchFont(true)};
    ui->embeddedFont_radioButton->setText(ui->embeddedFont_radioButton->text().arg(QFontInfo(embedded_font).family()));
    embedded_font.setWeight(QFont::Bold);
    ui->embeddedFont_label_1->setFont(embedded_font);
    ui->embeddedFont_label_9->setFont(embedded_font);

    QFont system_font{GUIUtil::fixedPitchFont(false)};
    ui->systemFont_radioButton->setText(ui->systemFont_radioButton->text().arg(QFontInfo(system_font).family()));
    system_font.setWeight(QFont::Bold);
    ui->systemFont_label_1->setFont(system_font);
    ui->systemFont_label_9->setFont(system_font);
    // Checking the embeddedFont_radioButton automatically unchecks the systemFont_radioButton.
    ui->systemFont_radioButton->setChecked(true);

    GUIUtil::handleCloseWindowShortcut(this);
}

OptionsDialog::~OptionsDialog()
{
    delete ui;
}

void OptionsDialog::setClientModel(ClientModel* client_model)
{
    m_client_model = client_model;
}

void OptionsDialog::setModel(OptionsModel *_model)
{
    this->model = _model;

    if(_model)
    {
        /* check if client restart is needed and show persistent message */
        if (_model->isRestartRequired())
            showRestartWarning(true);

        // Prune values are in GB to be consistent with intro.cpp
        static constexpr uint64_t nMinDiskSpace = (MIN_DISK_SPACE_FOR_BLOCK_FILES / GB_BYTES) + (MIN_DISK_SPACE_FOR_BLOCK_FILES % GB_BYTES) ? 1 : 0;
        ui->pruneSize->setRange(nMinDiskSpace, std::numeric_limits<int>::max());

        QString strLabel = _model->getOverriddenByCommandLine();
        if (strLabel.isEmpty())
            strLabel = tr("none");
        ui->overriddenByCommandLineLabel->setText(strLabel);

        mapper->setModel(_model);
        setMapper();
        mapper->toFirst();

        updateDefaultProxyNets();
    }

    /* warn when one of the following settings changes by user action (placed here so init via mapper doesn't trigger them) */

    /* Main */
    connect(ui->prune, &QCheckBox::clicked, this, &OptionsDialog::showRestartWarning);
    connect(ui->prune, &QCheckBox::clicked, this, &OptionsDialog::togglePruneWarning);
    connect(ui->pruneSize, qOverload<int>(&QSpinBox::valueChanged), this, &OptionsDialog::showRestartWarning);
    connect(ui->databaseCache, qOverload<int>(&QSpinBox::valueChanged), this, &OptionsDialog::showRestartWarning);
    connect(ui->externalSignerPath, &QLineEdit::textChanged, [this]{ showRestartWarning(); });
    connect(ui->threadsScriptVerif, qOverload<int>(&QSpinBox::valueChanged), this, &OptionsDialog::showRestartWarning);
    /* Wallet */
    connect(ui->spendZeroConfChange, &QCheckBox::clicked, this, &OptionsDialog::showRestartWarning);
    /* Network */
    connect(ui->allowIncoming, &QCheckBox::clicked, this, &OptionsDialog::showRestartWarning);
    connect(ui->enableServer, &QCheckBox::clicked, this, &OptionsDialog::showRestartWarning);
    connect(ui->connectSocks, &QCheckBox::clicked, this, &OptionsDialog::showRestartWarning);
    connect(ui->connectSocksTor, &QCheckBox::clicked, this, &OptionsDialog::showRestartWarning);
    /* Display */
    connect(ui->lang, qOverload<>(&QValueComboBox::valueChanged), [this]{ showRestartWarning(); });
    connect(ui->thirdPartyTxUrls, &QLineEdit::textChanged, [this]{ showRestartWarning(); });
}

void OptionsDialog::setCurrentTab(OptionsDialog::Tab tab)
{
    QWidget *tab_widget = nullptr;
    if (tab == OptionsDialog::Tab::TAB_NETWORK) tab_widget = ui->tabNetwork;
    if (tab == OptionsDialog::Tab::TAB_MAIN) tab_widget = ui->tabMain;
    if (tab_widget && ui->tabWidget->currentWidget() != tab_widget) {
        ui->tabWidget->setCurrentWidget(tab_widget);
    }
}

void OptionsDialog::setMapper()
{
    /* Main */
    mapper->addMapping(ui->mytherraAtStartup, OptionsModel::StartAtStartup);
    mapper->addMapping(ui->threadsScriptVerif, OptionsModel::ThreadsScriptVerif);
    mapper->addMapping(ui->databaseCache, OptionsModel::DatabaseCache);
    mapper->addMapping(ui->prune, OptionsModel::Prune);
    mapper->addMapping(ui->pruneSize, OptionsModel::PruneSize);

    /* Wallet */
    mapper->addMapping(ui->spendZeroConfChange, OptionsModel::SpendZeroConfChange);
    mapper->addMapping(ui->coinControlFeatures, OptionsModel::CoinControlFeatures);
    mapper->addMapping(ui->subFeeFromAmount, OptionsModel::SubFeeFromAmount);
    mapper->addMapping(ui->externalSignerPath, OptionsModel::ExternalSignerPath);
    mapper->addMapping(ui->m_enable_psbt_controls, OptionsModel::EnablePSMYTontrols);

    /* Network */
    mapper->addMapping(ui->mapPortUpnp, OptionsModel::MapPortUPnP);
    mapper->addMapping(ui->mapPortNatpmp, OptionsModel::MapPortNatpmp);
    mapper->addMapping(ui->allowIncoming, OptionsModel::Listen);
    mapper->addMapping(ui->enableServer, OptionsModel::Server);

    mapper->addMapping(ui->connectSocks, OptionsModel::ProxyUse);
    mapper->addMapping(ui->proxyIp, OptionsModel::ProxyIP);
    mapper->addMapping(ui->proxyPort, OptionsModel::ProxyPort);

    mapper->addMapping(ui->connectSocksTor, OptionsModel::ProxyUseTor);
    mapper->addMapping(ui->proxyIpTor, OptionsModel::ProxyIPTor);
    mapper->addMapping(ui->proxyPortTor, OptionsModel::ProxyPortTor);

    /* Window */
#ifndef Q_OS_MACOS
    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        mapper->addMapping(ui->showTrayIcon, OptionsModel::ShowTrayIcon);
        mapper->addMapping(ui->minimizeToTray, OptionsModel::MinimizeToTray);
    }
    mapper->addMapping(ui->minimizeOnClose, OptionsModel::MinimizeOnClose);
#endif

    /* Display */
    mapper->addMapping(ui->lang, OptionsModel::Language);
    mapper->addMapping(ui->unit, OptionsModel::DisplayUnit);
    mapper->addMapping(ui->thirdPartyTxUrls, OptionsModel::ThirdPartyTxUrls);
    mapper->addMapping(ui->embeddedFont_radioButton, OptionsModel::UseEmbeddedMonospacedFont);
}

void OptionsDialog::setOkButtonState(bool fState)
{
    ui->okButton->setEnabled(fState);
}

void OptionsDialog::on_resetButton_clicked()
{
    if (model) {
        // confirmation dialog
        /*: Text explaining that the settings changed will not come into effect
            until the client is restarted. */
        QString reset_dialog_text = tr("Client restart required to activate changes.") + "<br><br>";
        /*: Text explaining to the user that the client's current settings
            will be backed up at a specific location. %1 is a stand-in
            argument for the backup location's path. */
        reset_dialog_text.append(tr("Current settings will be backed up at \"%1\".").arg(m_client_model->dataDir()) + "<br><br>");
        /*: Text asking the user to confirm if they would like to proceed
            with a client shutdown. */
        reset_dialog_text.append(tr("Client will be shut down. Do you want to proceed?"));
        //: Window title text of pop-up window shown when the user has chosen to reset options.
        QMessageBox::StandardButton btnRetVal = QMessageBox::question(this, tr("Confirm options reset"),
            reset_dialog_text, QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);

        if (btnRetVal == QMessageBox::Cancel)
            return;

        /* reset all options and close GUI */
        model->Reset();
        close();
        Q_EMIT quitOnReset();
    }
}

void OptionsDialog::on_openMytherraConfButton_clicked()
{
    QMessageBox config_msgbox(this);
    config_msgbox.setIcon(QMessageBox::Information);
    //: Window title text of pop-up box that allows opening up of configuration file.
    config_msgbox.setWindowTitle(tr("Configuration options"));
    /*: Explanatory text about the priority order of instructions considered by client.
        The order from high to low being: command-line, configuration file, GUI settings. */
    config_msgbox.setText(tr("The configuration file is used to specify advanced user options which override GUI settings. "
                             "Additionally, any command-line options will override this configuration file."));

    QPushButton* open_button = config_msgbox.addButton(tr("Continue"), QMessageBox::ActionRole);
    config_msgbox.addButton(tr("Cancel"), QMessageBox::RejectRole);
    open_button->setDefault(true);

    config_msgbox.exec();

    if (config_msgbox.clickedButton() != open_button) return;

    /* show an error if there was some problem opening the file */
    if (!GUIUtil::openMytherraConf())
        QMessageBox::critical(this, tr("Error"), tr("The configuration file could not be opened."));
}

void OptionsDialog::on_okButton_clicked()
{
    mapper->submit();
    accept();
    updateDefaultProxyNets();
}

void OptionsDialog::on_cancelButton_clicked()
{
    reject();
}

void OptionsDialog::on_showTrayIcon_stateChanged(int state)
{
    if (state == Qt::Checked) {
        ui->minimizeToTray->setEnabled(true);
    } else {
        ui->minimizeToTray->setChecked(false);
        ui->minimizeToTray->setEnabled(false);
    }
}

void OptionsDialog::togglePruneWarning(bool enabled)
{
    ui->pruneWarning->setVisible(!ui->pruneWarning->isVisible());
}

void OptionsDialog::showRestartWarning(bool fPersistent)
{
    ui->statusLabel->setStyleSheet("QLabel { color: red; }");

    if(fPersistent)
    {
        ui->statusLabel->setText(tr("Client restart required to activate changes."));
    }
    else
    {
        ui->statusLabel->setText(tr("This change would require a client restart."));
        // clear non-persistent status label after 10 seconds
        // Todo: should perhaps be a class attribute, if we extend the use of statusLabel
        QTimer::singleShot(10s, this, &OptionsDialog::clearStatusLabel);
    }
}

void OptionsDialog::clearStatusLabel()
{
    ui->statusLabel->clear();
    if (model && model->isRestartRequired()) {
        showRestartWarning(true);
    }
}

void OptionsDialog::updateProxyValidationState()
{
    QValidatedLineEdit *pUiProxyIp = ui->proxyIp;
    QValidatedLineEdit *otherProxyWidget = (pUiProxyIp == ui->proxyIpTor) ? ui->proxyIp : ui->proxyIpTor;
    if (pUiProxyIp->isValid() && (!ui->proxyPort->isEnabled() || ui->proxyPort->text().toInt() > 0) && (!ui->proxyPortTor->isEnabled() || ui->proxyPortTor->text().toInt() > 0))
    {
        setOkButtonState(otherProxyWidget->isValid()); //only enable ok button if both proxys are valid
        clearStatusLabel();
    }
    else
    {
        setOkButtonState(false);
        ui->statusLabel->setStyleSheet("QLabel { color: red; }");
        ui->statusLabel->setText(tr("The supplied proxy address is invalid."));
    }
}

void OptionsDialog::updateDefaultProxyNets()
{
    CNetAddr ui_proxy_netaddr;
    LookupHost(ui->proxyIp->text().toStdString(), ui_proxy_netaddr, /*fAllowLookup=*/false);
    const CService ui_proxy{ui_proxy_netaddr, ui->proxyPort->text().toUShort()};

    Proxy proxy;
    bool has_proxy;

    has_proxy = model->node().getProxy(NET_IPV4, proxy);
    ui->proxyReachIPv4->setChecked(has_proxy && proxy.proxy == ui_proxy);

    has_proxy = model->node().getProxy(NET_IPV6, proxy);
    ui->proxyReachIPv6->setChecked(has_proxy && proxy.proxy == ui_proxy);

    has_proxy = model->node().getProxy(NET_ONION, proxy);
    ui->proxyReachTor->setChecked(has_proxy && proxy.proxy == ui_proxy);
}

ProxyAddressValidator::ProxyAddressValidator(QObject *parent) :
QValidator(parent)
{
}

QValidator::State ProxyAddressValidator::validate(QString &input, int &pos) const
{
    Q_UNUSED(pos);
    // Validate the proxy
    CService serv(LookupNumeric(input.toStdString(), DEFAULT_GUI_PROXY_PORT));
    Proxy addrProxy = Proxy(serv, true);
    if (addrProxy.IsValid())
        return QValidator::Acceptable;

    return QValidator::Invalid;
}
